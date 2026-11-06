// Damage numbers: a number (the game's damage times 10) pops up where each hit on an enemy lands,
// flies off in one of several styles, and fades out.
//
// How it works:
// - The game posts each hit as a record in a ring of 64 (0x40 bytes each: the hit point at +0x00,
//   the direction at +0x10, the attacker at +0x20, the mask at +0x24, the damage at +0x2C, the
//   victim moby at +0x34), and moves a write index on (+0x14 of the collision results record).
//   The mod reads the records written since the last tick. It does not use on_moby_hit: the port
//   tells mods only the hits of PostHitMessage and record_moby_hit, while the wrench's swing and
//   the sphere tests (bombs) are written by the collision code itself (the port's
//   rac1/host/collision.c, hit_record), and only one mod can have that callback anyway.
//   Addresses (OpenRAC port/game/rac1/host/collision.c and world.c): PAL ring L00 0x00178200,
//   results L00 0x00173F40; NTSC-U 0x00178100 and 0x00173E40.
// - The same hit is posted again while it lasts: a wrench swing tests its sweep each frame it is
//   active and posts to every enemy still in it (the PAL decompilation's wrench.c,
//   sweep_hit_segment; the game's record refuses only a hit with less damage than the one it
//   holds), though the enemy takes it once. So a post from the same attacker to the same victim
//   within kRepeatSeconds of the last is the same hit, not a new one.
// - Each number stays where its hit landed in the world; its enemy (moby_list) is only watched to
//   see it die. Each game tick (on_frame) the numbers age and are drawn as props (1.12): each
//   glyph a card (assets/<style>/*.glb, their texture assets/<style>/glyphs.png, made by
//   tools/make_digits.py; the "style" setting picks the set) the renderer draws in the level,
//   turned to face the game's camera (camera_view) and tilted along the number's flight. The mods
//   share 256 props, so many numbers show at once. If the digit props cannot be made (no file, no
//   room), no numbers show, and the log says why.
// - Critical hits: the game has none yet. is_critical() is where a rule goes (per weapon, from a
//   hit's attacker and mask); for now it only answers the "critical preview" setting, a share of
//   hits marked critical so their look can be seen and tuned.
// - The game's camera (docs/engine/systems/CAMERA_FOG.md): rows forward, left and up. Camera x is
//   -left . v (right), y is -up . v (down), depth forward . v. Cutscene cameras are the game's
//   own, so numbers only show in play.
#include "openrac/mod.hpp"

#include "openrac/games/rac1.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>

// For the mod's own folder (mod_folder): where its library was loaded from.
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

constexpr float kPi = 3.14159265f;
// A post from the same attacker to the same victim within this many seconds of the last is the
// same hit posted again (a wrench swing posts each frame its sweep is on the enemy).
constexpr float kRepeatSeconds = 0.15f;
// How long a number takes to pop from big to its size, in seconds.
constexpr float kPopSeconds = 0.15f;
// How far a number flies over its life, in world units (Ratchet is about one unit tall), and the
// widest angle from straight up a fanned-out number may fly at, either side (radians; 1.2 is
// about 70 degrees).
constexpr float kTravel = 1.4f;
constexpr float kSpread = 1.2f;
// The widest a number tilts from upright, either side (radians).
constexpr float kMaxTilt = 1.2f;
// A hit point further than this from its victim is not where the victim is (some hits carry the
// attacker's position or none): the number goes over the victim instead.
constexpr float kFarHit = 6.0f;
// How far over a victim's origin a number starts when the hit point is not used.
constexpr float kOverHead = 1.2f;
// The kinds of hit (the ring record's mask) enemies take damage from: the player's attacks. Most
// enemies read their hits with 0x330000 (the wrench's is 0x10000), some with 0x800000 too
// (the NTSC decompilation's find_moby_hit calls); enemies' own attacks, as a frog's bite (1), have
// none of these bits.
constexpr std::uint32_t kDamageMask = 0x330000u | 0x800000u;

// Props: the most this mod makes (of the 256 all mods share), a glyph's height in world units at
// size 1 and close by (it grows with distance, up to kFarGrowth times, so far numbers stay
// readable), and how far a number is drawn towards the camera from where it is, so the enemy's
// own body does not hide it.
constexpr int kMaxProps = 200;
constexpr float kDigitHeight = 0.32f;
constexpr float kNearDepth = 7.0f;
constexpr float kFarGrowth = 2.5f;
constexpr float kTowardCamera = 0.7f;
// The brightness boost: a prop's tint times this, against the level's lighting (a tint above 1
// brightens the texture; where the port caps it at 1, the boost does nothing).
constexpr float kBoost = 1.6f;

// The glyphs, in each style's texture order (tools/make_digits.py): the digits, then '!', the
// critical mark. Each is assets/<style>/<name>.glb.
constexpr int kGlyphs = 11;
constexpr int kBang = 10;
constexpr const char* kGlyphFiles[kGlyphs] = {
    "digit_0", "digit_1", "digit_2", "digit_3", "digit_4", "digit_5",
    "digit_6", "digit_7", "digit_8", "digit_9", "bang",
};

// The styles: their folder under assets/, and the step from one glyph to the next as a share of
// the glyph's height (less than the card's width, so the outlines overlap and the glyphs sit
// close: the pixel card is 0.78 wide, the original 1.125 (squashed wide), the comic 0.91 with its
// slant and offset outline).
enum class Style { Pixel, Original, Comic };
struct StyleInfo {
    const char* folder;
    float advance;
    float card;  // the card's width for a height of 1 (tools/make_digits.py's cell)
};
constexpr StyleInfo kStyles[] = {
    {"pixel", 0.62f, 56.0f / 72.0f},
    {"original", 0.8f, 144.0f / 128.0f},
    {"comic", 0.72f, 116.0f / 128.0f},
};
// Each style's glyph texture, beside its meshes in assets/<style>/.
constexpr const char* kTextureFile = "glyphs.png";

// Glow in the dark: glyph meshes the mod writes for it at the start, one set for each colour a
// number can have (assets/<style>/tinted/<RRGGBB>/), whose material lights itself in that colour
// (its emission: the glyph texture times the colour times kGlowStrength), which OpenRAC's
// enhanced rendering adds after the level's lighting. A new mesh makes the port read the level
// again, so they are all made at the start of a level, and the heat colours (hit colour to
// many-hits colour) come in at most kGlowHeatSteps steps. The palette: the heat steps, then the
// kill colour, then the critical colour.
constexpr float kGlowStrength = 0.8f;
constexpr int kGlowHeatSteps = 6;
constexpr int kPaletteMax = kGlowHeatSteps + 2;

// The hit ring.
constexpr int kHitRecords = 64;
constexpr openrac_gaddr kHitSize = 0x40;
constexpr openrac_gaddr kHitPoint = 0x00;
constexpr openrac_gaddr kHitAttacker = 0x20;
constexpr openrac_gaddr kHitMask = 0x24;
constexpr openrac_gaddr kHitDamage = 0x2C;
constexpr openrac_gaddr kHitVictim = 0x34;
constexpr openrac_gaddr kResultsIndex = 0x14;
// A moby's size in the level's moby array (the port's rac1/host/world.c, MOBY_SIZE), and the
// most slots the mod works one out for itself.
constexpr openrac_gaddr kMobySize = 0x100;
constexpr openrac_gaddr kMaxSlots = 8192;

// The numbers show the game's damage times this (the game's are small: 1 for a wrench swing).
constexpr float kShownScale = 10.0f;
// A critical number: how much bigger it is, and how far it shakes (a share of its height,
// fading out with it).
constexpr float kCriticalScale = 1.3f;
constexpr float kCriticalShake = 0.08f;
// The kill effects: how long they last (from the kill, whatever the number's own time left), how
// hard things fall (world units a second squared), how fast a number or a digit spins (radians a
// second), and how high it is thrown first (world units a second).
constexpr float kKillSeconds = 0.9f;
// Before its effect a killed number holds still, whole, for this long (seconds), popping big as
// it stops: the kill reads before the number breaks up or falls.
constexpr float kKillPause = 0.3f;
constexpr float kKillPop = 1.25f;
constexpr float kGravity = 9.0f;
constexpr float kSpin = 9.0f;
constexpr float kThrow = 2.2f;

enum class Flight { Fan, Straight, Arc, Bounce, Random };
enum class KillEffect { Spin, Shatter, None };

struct Rgb {
    std::uint8_t r, g, b;
};

// The settings (openrac-mod.json), read at load.
struct Settings {
    float duration = 0.8f;
    float size = 1.0f;
    bool creatures_only = true;
    bool log_hits = false;
    bool glow = false;  // the self-lit glyphs (assets/<style>/glow/)
    Style style = Style::Pixel;
    Flight flight = Flight::Fan;
    KillEffect kill_effect = KillEffect::Spin;
    float opacity = 0.8f;
    float pop = 1.5f;  // how big a number starts, times its size
    bool boost = true;
    Rgb hit{255, 215, 0};      // bright yellow: one hit
    Rgb hot{255, 30, 0};       // red: a number of `heat_hits` hits or more
    int heat_hits = 6;         // the hits a number takes to go from `hit` to `hot`
    bool kill_colour = false;  // the hit that killed in `killed`, else in its heat colour
    Rgb killed{255, 40, 40};   // red: the hit that killed
    Rgb critical{60, 230, 255};  // light blue
    bool critical_mark = true;
    float critical_preview = 0.0f;  // the share of hits shown as critical, 0 to 1
};

struct Number {
    int slot = -1;           // the victim's slot, -1 once it is gone
    float pos[3] = {};       // where its last hit landed in the world (before it flies off)
    Flight flight = Flight::Fan;  // never Random: a random number picks one when it appears
    float dir[2] = {0.0f, 1.0f};  // fan: where it flies on the screen, right and up, unit
    float side = 0.0f;       // arc and bounce: which way it drifts, -1 to 1
    float shake = 0.0f;      // a critical number's shake phase
    float damage = 0.0f;
    int hits = 1;            // the hits added up in it, for its colour
    float age = 0.0f;        // since its flight began: it appeared, or a hit added to it
    float pop = 0.0f;        // since it last popped big (the same)
    bool killed = false;
    bool critical = false;
    // The kill effect: since it began (-1 before), and where the number was then and its tilt.
    float kill_age = -1.0f;
    float kill_from[3] = {};
    float kill_tilt = 0.0f;
    // Props: the text its glyph props show, and the props (-1 for a glyph without one).
    std::string shown;
    int shown_colour = 0;  // the palette colour its props are in (0 without the glow)
    std::vector<int> props;
};

// A post the mod counted: from `attacker` to `victim` (moby addresses), last seen at `time`.
struct Post {
    openrac_gaddr victim;
    openrac_gaddr attacker;
    float time;
};

struct State {
    openrac::Mod* mod = nullptr;
    Settings s;
    float tick_seconds = 1.0f / 60.0f;
    // The hit ring and the collision results record (the write index), in the L00 program.
    openrac_gaddr hit_ring = 0;
    openrac_gaddr results = 0;
    // The ring's write index as the last tick left it; -1 to take it as it is.
    int hit_index = -1;
    // Where the level's moby array starts (0 before the mod has seen it): read each tick from the
    // game's pointer to it (`moby_array_pointer`, an executable global: PAL D_00160018, NTSC-U
    // 0x0015FF18, as the port's rac1/host/world.c reads it), else learned from any moby the port
    // places (moby_slot_of). The port's slot is (address - start) / kMobySize, and its
    // moby_slot_of answers -1 for a moby deleted since, as an enemy one shot kills is.
    openrac_gaddr moby_array = 0;
    openrac_gaddr moby_array_pointer = 0;
    // Seconds of play, for the repeated posts; the posts counted lately.
    float clock = 0.0f;
    std::vector<Post> posts;
    // The enemies whose death the game recorded lately (Moby_RecordDeath): their slots and when.
    // A death recorded before the killing hit's number is made still makes that number killed.
    std::vector<std::pair<int, float>> deaths;

    std::uint32_t loads = ~0u;
    std::uint32_t random = 0x9E3779B9u;
    std::vector<Number> numbers;
    // This tick's mobys, and where each slot is in it (-1 for none); and the last tick's, for a
    // hit on a moby deleted since (an enemy one shot kills is gone by the time its hit is read).
    std::vector<openrac_moby> mobys;
    std::vector<int> by_slot;
    std::vector<openrac_moby> last_mobys;
    std::vector<int> last_by_slot;

    // Props of this level load: each glyph's spare ones (hidden), how many the mod made, whether
    // they were tried (the first tick in the level) and whether they work.
    // Spares by palette colour and glyph: spare[colour * kGlyphs + glyph].
    std::vector<int> spare[kGlyphs * kPaletteMax];
    // The glow's palette (from the settings) and how many heat steps it has, and whether its
    // meshes are written (once a run).
    std::vector<Rgb> palette;
    int heat_steps = 0;
    int glow_meshes = -1;  // -1 not tried, 0 failed, 1 written
    int props_made = 0;
    bool props_tried = false;
    bool use_props = false;
    bool warned = false;
    // Whether this level's props are the glow copies (the setting, and enhanced rendering on),
    // and what the log last said about it (-1 nothing yet).
    bool glow_active = false;
    int glow_said = -1;
};

State g;

const openrac_moby* find_moby(const std::vector<openrac_moby>& mobys,
                              const std::vector<int>& by_slot, int slot) {
    if (slot < 0 || slot >= static_cast<int>(by_slot.size()) || by_slot[slot] < 0) {
        return nullptr;
    }
    return &mobys[static_cast<std::size_t>(by_slot[slot])];
}

// The moby in `slot` this tick, or nullptr.
const openrac_moby* moby_at(int slot) {
    return find_moby(g.mobys, g.by_slot, slot);
}

// The moby a hit was on: as it was last tick, when it was there then (the hit landed during the
// tick since, and the moby may be gone now, or another made in its slot), else as it is now (one
// made since).
const openrac_moby* hit_moby(int slot) {
    const openrac_moby* then = find_moby(g.last_mobys, g.last_by_slot, slot);
    return then != nullptr ? then : moby_at(slot);
}

bool alive(const openrac_moby& m) {
    return (m.flags & OPENRAC_MOBY_DEAD) == 0 && m.state < 0x80;
}

void list_mobys() {
    g.last_mobys.swap(g.mobys);
    g.last_by_slot.swap(g.by_slot);
    g.mobys = g.mod->moby_list();
    g.by_slot.assign(g.by_slot.size(), -1);
    for (std::size_t i = 0; i < g.mobys.size(); ++i) {
        const int slot = g.mobys[i].slot;
        if (slot < 0) {
            continue;
        }
        if (slot >= static_cast<int>(g.by_slot.size())) {
            g.by_slot.resize(static_cast<std::size_t>(slot) + 1, -1);
        }
        g.by_slot[static_cast<std::size_t>(slot)] = static_cast<int>(i);
    }
}

// A number from -1 to 1 (xorshift; the look of the numbers only, never the game's own random).
float random_signed() {
    std::uint32_t x = g.random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g.random = x;
    return static_cast<float>(x) / 2147483648.0f - 1.0f;
}

// A number from 0 to 1.
float random_unit() {
    return random_signed() * 0.5f + 0.5f;
}

template <class T>
T read(openrac_gaddr address) {
    T value;
    std::memcpy(&value, g.mod->api().memory + address, sizeof value);
    return value;
}

// ---- props ------------------------------------------------------------------------------------

// The glyph a character of a number's text shows.
int glyph_of(char c) {
    return c >= '0' && c <= '9' ? c - '0' : kBang;
}

// The colour `t` of the way (0 to 1) from the hit colour to the many-hits colour.
Rgb heat_colour(float t) {
    const auto mix = [t](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(std::lround(a + (b - a) * t));
    };
    return {mix(g.s.hit.r, g.s.hot.r), mix(g.s.hit.g, g.s.hot.g), mix(g.s.hit.b, g.s.hot.b)};
}

// The glow's palette, from the settings: the heat steps (one a hit up to the hits to full
// colour, at most kGlowHeatSteps), the kill colour, the critical colour.
void make_palette() {
    g.heat_steps = std::clamp(g.s.heat_hits, 2, kGlowHeatSteps);
    g.palette.clear();
    for (int i = 0; i < g.heat_steps; ++i) {
        g.palette.push_back(heat_colour(static_cast<float>(i) / static_cast<float>(g.heat_steps - 1)));
    }
    g.palette.push_back(g.s.killed);
    g.palette.push_back(g.s.critical);
}

// The mod's own folder, where its props' meshes must be: its library is
// <folder>/lib/<platform>/<name>, so the folder is three levels up from it. Empty when unknown.
std::filesystem::path mod_folder() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(reinterpret_cast<void*>(&mod_folder)),
                            &module)) {
        return {};
    }
    std::wstring name(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));
    if (length == 0) {
        return {};
    }
    name.resize(length);
    const std::filesystem::path library(name);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&mod_folder), &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    const std::filesystem::path library(info.dli_fname);
#endif
    return library.parent_path().parent_path().parent_path();
}

// A glyph card as a .glb, as tools/make_digits.py writes them: glyph `index` of the style's
// texture (`texture`, its path from the mesh), `card` heights wide, both faces, its material lit
// by itself in `emission` (0 to 1 a channel).
std::string glyph_glb(int index, float card, const std::string& texture, const float emission[3]) {
    const float w = card / 2.0f;
    const float u0 = static_cast<float>(index) / static_cast<float>(kGlyphs);
    const float u1 = static_cast<float>(index + 1) / static_cast<float>(kGlyphs);
    const float corners[4][4] = {
        {-w, 0.5f, u0, 0.0f}, {w, 0.5f, u1, 0.0f}, {w, -0.5f, u1, 1.0f}, {-w, -0.5f, u0, 1.0f}};
    std::vector<float> positions, normals, uvs;
    for (const float normal_y : {-1.0f, 1.0f}) {  // the face towards the camera (-y), the back
        for (const auto& c : corners) {
            positions.insert(positions.end(), {c[0], 0.0f, c[1]});
            normals.insert(normals.end(), {0.0f, normal_y, 0.0f});
            uvs.insert(uvs.end(), {c[2], c[3]});
        }
    }
    const std::uint16_t indices[12] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7};

    std::string blob;
    const auto add = [&blob](const void* data, std::size_t size) {
        while (blob.size() % 4) {
            blob.push_back('\0');
        }
        const std::size_t at = blob.size();
        blob.append(static_cast<const char*>(data), size);
        return at;
    };
    const std::size_t pos_at = add(positions.data(), positions.size() * sizeof(float));
    const std::size_t nrm_at = add(normals.data(), normals.size() * sizeof(float));
    const std::size_t uv_at = add(uvs.data(), uvs.size() * sizeof(float));
    const std::size_t idx_at = add(indices, sizeof indices);
    while (blob.size() % 4) {
        blob.push_back('\0');
    }

    char json[2048];
    std::snprintf(
        json, sizeof json,
        "{\"asset\":{\"version\":\"2.0\",\"generator\":\"damage-numbers\"},\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"name\":\"%s\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
        "\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
        "\"materials\":[{\"name\":\"glyphs\",\"pbrMetallicRoughness\":{\"baseColorTexture\":"
        "{\"index\":0},\"metallicFactor\":0.0},\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5,"
        "\"doubleSided\":true,\"emissiveTexture\":{\"index\":0},"
        "\"emissiveFactor\":[%.4f,%.4f,%.4f]}],"
        "\"textures\":[{\"source\":0,\"sampler\":0}],"
        "\"samplers\":[{\"magFilter\":9729,\"minFilter\":9729,\"wrapS\":33071,\"wrapT\":33071}],"
        "\"images\":[{\"uri\":\"%s\"}],\"buffers\":[{\"byteLength\":%u}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":96,\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":96,\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":64,\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":24,\"target\":34963}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\","
        "\"min\":[%.6f,0.0,-0.5],\"max\":[%.6f,0.0,0.5]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":8,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5123,\"count\":12,\"type\":\"SCALAR\"}]}",
        kGlyphFiles[index], emission[0], emission[1], emission[2], texture.c_str(),
        static_cast<unsigned>(blob.size()), static_cast<unsigned>(pos_at),
        static_cast<unsigned>(nrm_at), static_cast<unsigned>(uv_at), static_cast<unsigned>(idx_at),
        -w, w);
    std::string text = json;
    while (text.size() % 4) {
        text.push_back(' ');
    }
    const auto u32 = [](std::string& out, std::uint32_t v) {
        const unsigned char bytes[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                        static_cast<unsigned char>(v >> 16),
                                        static_cast<unsigned char>(v >> 24)};
        out.append(reinterpret_cast<const char*>(bytes), 4);
    };
    std::string out;
    u32(out, 0x46546C67u);  // "glTF"
    u32(out, 2u);
    u32(out, static_cast<std::uint32_t>(12 + 8 + text.size() + 8 + blob.size()));
    u32(out, static_cast<std::uint32_t>(text.size()));
    u32(out, 0x4E4F534Au);  // "JSON"
    out += text;
    u32(out, static_cast<std::uint32_t>(blob.size()));
    u32(out, 0x004E4942u);  // "BIN"
    out += blob;
    return out;
}

// The folder (in the mod's, under assets/) of the glow meshes of palette colour `colour`.
std::string tinted_folder(int colour) {
    const Rgb c = g.palette[static_cast<std::size_t>(colour)];
    char folder[64];
    std::snprintf(folder, sizeof folder, "assets/%s/tinted/%02X%02X%02X",
                  kStyles[static_cast<int>(g.s.style)].folder, c.r, c.g, c.b);
    return folder;
}

// Writes the glow meshes of every palette colour into the mod's folder (those not there yet);
// whether they all are there, else why not.
bool write_glow_meshes(std::string& why_not) {
    const std::filesystem::path folder = mod_folder();
    if (folder.empty()) {
        why_not = "the mod's own folder could not be found";
        return false;
    }
    const float card = kStyles[static_cast<int>(g.s.style)].card;
    for (int colour = 0; colour < static_cast<int>(g.palette.size()); ++colour) {
        const std::filesystem::path dir = folder / std::filesystem::u8path(tinted_folder(colour));
        std::error_code error;
        std::filesystem::create_directories(dir, error);
        const Rgb c = g.palette[static_cast<std::size_t>(colour)];
        const float emission[3] = {c.r / 255.0f * kGlowStrength, c.g / 255.0f * kGlowStrength,
                                   c.b / 255.0f * kGlowStrength};
        for (int glyph = 0; glyph < kGlyphs; ++glyph) {
            const std::filesystem::path file = dir / (std::string(kGlyphFiles[glyph]) + ".glb");
            if (std::filesystem::exists(file, error)) {
                continue;
            }
            std::ofstream out(file, std::ios::binary);
            out << glyph_glb(glyph, card, std::string("../../") + kTextureFile, emission);
            if (!out) {
                why_not = "could not write " + file.u8string();
                return false;
            }
        }
    }
    return true;
}

// A prop showing `glyph` in palette colour `colour` (0 without the glow): a spare one shown
// again, else a new one; -1 when none can be had.
int take_prop(int glyph, int colour) {
    std::vector<int>& spare = g.spare[colour * kGlyphs + glyph];
    if (!spare.empty()) {
        const int id = spare.back();
        spare.pop_back();
        g.mod->prop_hide(id, false);
        return id;
    }
    if (g.props_made >= kMaxProps) {
        return -1;
    }
    // With the glow, the mesh lit by itself in its colour; else the style's own.
    const std::string path =
        (g.glow_active ? tinted_folder(colour)
                       : std::string("assets/") + kStyles[static_cast<int>(g.s.style)].folder)
        + "/" + kGlyphFiles[glyph] + ".glb";
    const int id = g.mod->prop_create(path.c_str());
    if (id >= 0) {
        ++g.props_made;
    }
    return id;
}

// Hides a number's props and keeps them for later numbers.
void give_back_props(Number& n) {
    for (std::size_t i = 0; i < n.props.size() && i < n.shown.size(); ++i) {
        const int id = n.props[i];
        if (id >= 0) {
            g.mod->prop_hide(id, true);
            g.spare[n.shown_colour * kGlyphs + glyph_of(n.shown[i])].push_back(id);
        }
    }
    n.props.clear();
    n.shown.clear();
}

// Makes one prop of `glyph` in `colour` now and keeps it spare; whether it could be made.
bool warm(int glyph, int colour) {
    const int id = take_prop(glyph, colour);
    if (id < 0) {
        return false;
    }
    g.mod->prop_hide(id, true);
    g.spare[colour * kGlyphs + glyph].push_back(id);
    return true;
}

// Whether OpenRAC draws with its enhanced rendering, which the glow copies need (it adds a
// material's emission; the port turns it on only with Vulkan): the game draws with Vulkan, and
// the port's settings file has enhanced_rendering = 1. That file is <game>-<disc>.ini in the
// settings folder beside mod-data/ in OpenRAC's data folder (the mod's data_dir is
// mod-data/<id>/), and the port writes it again when the Video options change. Empty why_not
// when it is on.
bool enhanced_rendering_on(std::string& why_not) {
    if (g.mod->graphics().backend != OPENRAC_GRAPHICS_VULKAN) {
        why_not = "the game does not draw with Vulkan";
        return false;
    }
    std::string dir = g.mod->data_dir();
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) {
        dir.pop_back();
    }
    if (dir.empty()) {
        why_not = "no data folder to find the settings from";
        return false;
    }
    const std::filesystem::path file = std::filesystem::u8path(dir).parent_path().parent_path()
                                       / "settings"
                                       / (std::string(g.mod->game()) + "-" + g.mod->api().version + ".ini");
    std::ifstream in(file);
    if (!in) {
        why_not = "no settings file at " + file.u8string();
        return false;
    }
    for (std::string line; std::getline(in, line);) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        key.erase(std::remove_if(key.begin(), key.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }),
                  key.end());
        if (key != "enhanced_rendering") {
            continue;
        }
        const std::size_t value = line.find_first_not_of(" \t", eq + 1);
        if (value != std::string::npos && line[value] == '1') {
            return true;
        }
        why_not = "enhanced rendering is off";
        return false;
    }
    why_not = "enhanced rendering is off";
    return false;
}

// The first tick in a level: one prop of each glyph (and with the glow, of each colour) the
// settings use, made now (the port reads the level again with a new mesh, a hitch, so it comes
// with the level's start rather than at the first hit) and kept spare. Without all ten digits the
// props do not work here, and no numbers show.
void try_props() {
    g.props_tried = true;
    // Glow in the dark: the glow meshes only with enhanced rendering (without it they would draw
    // as the regular ones anyway), written once a run in the settings' colours.
    g.glow_active = false;
    if (g.s.glow) {
        std::string why_not;
        g.glow_active = enhanced_rendering_on(why_not);
        if (g.glow_active && g.glow_meshes < 0) {
            g.glow_meshes = write_glow_meshes(why_not) ? 1 : 0;
        }
        if (g.glow_meshes == 0) {
            g.glow_active = false;
        }
        if (g.glow_said != (g.glow_active ? 1 : 0)) {
            g.glow_said = g.glow_active ? 1 : 0;
            g.mod->info(g.glow_active ? "glow in the dark: on (enhanced rendering)"
                                      : "glow in the dark: regular numbers, " + why_not);
        }
    }
    const int colours = g.glow_active ? static_cast<int>(g.palette.size()) : 1;
    int made = 0;
    for (int d = 0; d < 10; ++d) {
        made += warm(d, 0) ? 1 : 0;
    }
    for (int colour = 1; colour < colours; ++colour) {
        for (int d = 0; d < 10; ++d) {
            warm(d, colour);
        }
    }
    g.use_props = made == 10;
    if (!g.use_props) {
        if (!g.warned) {
            g.warned = true;
            g.mod->warn(std::string("the digit props could not be made (assets/")
                        + kStyles[static_cast<int>(g.s.style)].folder
                        + "/digit_N.glb): no numbers in this level");
        }
        return;
    }
    if (g.s.critical_mark && g.s.critical_preview > 0.0f) {
        // A critical number is in the critical colour, or the kill colour once it killed.
        warm(kBang, g.glow_active ? colours - 1 : 0);
        if (g.glow_active) {
            warm(kBang, colours - 2);
        }
    }
}

// ---- the numbers ------------------------------------------------------------------------------

void drop_number(Number& n) {
    give_back_props(n);
}

// Ends every number. With `level_gone` the props went with the level: nothing to hide or keep.
void clear_numbers(bool level_gone) {
    if (level_gone) {
        for (std::vector<int>& spare : g.spare) {
            spare.clear();
        }
        g.props_made = 0;
        g.props_tried = false;
        g.use_props = false;
    } else {
        for (Number& n : g.numbers) {
            drop_number(n);
        }
    }
    g.numbers.clear();
    g.posts.clear();
    g.deaths.clear();
}

// One hit as the ring holds it.
struct Hit {
    int slot;            // the victim's slot
    openrac_gaddr victim;  // the victim moby's address, as the record holds it
    openrac_gaddr attacker;
    std::uint32_t mask;  // the game's weapon or attack bits
    float damage;
    float point[3];
};

// Whether a hit is critical. The game has none: this is where a rule goes when weapons get
// critical hits (from the hit's attacker and mask). For now, the "critical preview" setting's
// share of hits, to see the look.
bool is_critical(const Hit& hit) {
    (void)hit;
    return g.s.critical_preview > 0.0f && random_unit() < g.s.critical_preview;
}

// A new number's flight: the setting's, or one picked at random; its direction.
void pick_flight(Number& n) {
    n.flight = g.s.flight;
    if (n.flight == Flight::Random) {
        const int pick = std::min(3, static_cast<int>(random_unit() * 4.0f));
        n.flight = static_cast<Flight>(pick);
    }
    const float angle = random_signed() * kSpread;
    n.dir[0] = std::sin(angle);
    n.dir[1] = std::cos(angle);
    n.side = random_signed();
    n.shake = random_unit() * 2.0f * kPi;
}

// Shows a hit; nullptr when it shows, else why not (for the "log hits" setting).
int slot_of(openrac_gaddr address);  // below, with the hit ring

// The game recorded the death of the moby at `moby` (Moby_RecordDeath: it drops its bolts, at
// the killing blow, before an enemy knocked away flies off and is deleted when it lands, as the
// peckbot and the frogs do): its numbers are killed from now.
void note_death(openrac_gaddr moby) {
    const int slot = slot_of(moby);
    if (slot < 0) {
        return;
    }
    for (Number& n : g.numbers) {
        if (n.slot == slot) {
            n.killed = true;
        }
    }
    g.deaths.emplace_back(slot, g.clock);
}

const char* add_hit(const Hit& hit) {
    if (!(hit.damage > 0.0f) || !std::isfinite(hit.damage)) {
        return "no damage";
    }
    if (hit.slot < 0) {
        return "no moby slot";
    }
    // Only the kinds of hit enemies take damage from (the player's attacks). A sphere attack
    // posts to every moby in it (a Blarg Station frog's bite, mask 1, reaches the frogs around
    // it), and those hits do nothing to their victims.
    if ((hit.mask & kDamageMask) == 0) {
        return "not a damaging kind";
    }
    // Hits on Ratchet are not shown.
    if (hit.slot == g.mod->figure_slot(-1)) {
        return "on Ratchet";
    }
    const int slot = hit.slot;
    const openrac_moby* victim = hit_moby(slot);
    if (victim == nullptr) {
        return "victim not listed";
    }
    if (g.s.creatures_only && (victim->flags & OPENRAC_MOBY_CREATURE) == 0) {
        return "not a creature";
    }
    const bool critical = is_critical(hit);

    // Where a number for this hit starts: the hit point, or over the victim's head when the hit
    // carries no point near it (the sphere tests leave it at 0).
    float start[3];
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = hit.point[i] - victim->position[i];
        d2 += d * d;
    }
    const bool close_by = std::isfinite(d2) && d2 <= kFarHit * kFarHit;
    for (int i = 0; i < 3; ++i) {
        start[i] = close_by ? hit.point[i] : victim->position[i];
    }
    if (!close_by) {
        start[2] += kOverHead;
    }

    // Hits on the same enemy add up into its number while that is still showing. Each one
    // starts it again as a new number would (at this hit, a flight of its own, whole and
    // popping) with the added-up value and one more hit, so it grows hotter. A critical hit
    // stands on its own.
    if (!critical) {
        for (Number& n : g.numbers) {
            if (n.slot == slot && !n.killed && !n.critical) {
                std::copy(start, start + 3, n.pos);
                pick_flight(n);
                n.damage += hit.damage;
                n.hits += 1;
                n.age = 0.0f;
                n.pop = 0.0f;
                return nullptr;
            }
        }
    }

    Number n;
    n.slot = slot;
    std::copy(start, start + 3, n.pos);
    pick_flight(n);
    n.damage = hit.damage;
    n.critical = critical;
    for (const auto& death : g.deaths) {
        if (death.first == slot) {
            n.killed = true;  // the game recorded its death already, this tick
        }
    }
    g.numbers.push_back(n);
    return nullptr;
}

// The slot of the moby at `address`, -1 for none. The port's answer where it has one (and the
// array's start learned from it); else, for a moby deleted since (the port answers -1 for it),
// worked out as the port does it, from where the array starts.
int slot_of(openrac_gaddr address) {
    if (address == 0) {
        return -1;
    }
    const int slot = g.mod->api().moby_slot_of(address);
    if (slot >= 0) {
        g.moby_array = address - static_cast<openrac_gaddr>(slot) * kMobySize;
        return slot;
    }
    if (g.moby_array == 0 || address < g.moby_array || (address - g.moby_array) % kMobySize != 0) {
        return -1;
    }
    const openrac_gaddr worked_out = (address - g.moby_array) / kMobySize;
    return worked_out < kMaxSlots ? static_cast<int>(worked_out) : -1;
}

// A moby's class (oclass) and creature flag as last listed, for the log; -1 when not listed.
int class_of(int slot, bool* creature) {
    const openrac_moby* m = hit_moby(slot);
    if (creature != nullptr) {
        *creature = m != nullptr && (m->flags & OPENRAC_MOBY_CREATURE) != 0;
    }
    return m != nullptr ? m->oclass : -1;
}

// One line in the game's log for a hit read from the ring (the "log hits" setting): its victim,
// its kind (mask), its damage, its attacker, and whether it showed or why not.
void log_hit(const Hit& hit, const char* skipped) {
    bool creature = false;
    const int victim_class = class_of(hit.slot, &creature);
    const int attacker_slot = slot_of(hit.attacker);
    const int attacker_class = attacker_slot >= 0 ? class_of(attacker_slot, nullptr) : -1;
    char line[400];
    int used = std::snprintf(
        line, sizeof line,
        "hit: victim slot %d class %d%s, mask 0x%06X, damage %.2f, attacker slot %d class %d: %s"
        " | victim 0x%08X attacker 0x%08X point %.1f %.1f %.1f",
        hit.slot, victim_class, creature ? " (creature)" : "", static_cast<unsigned>(hit.mask),
        hit.damage, attacker_slot, attacker_class, skipped != nullptr ? skipped : "shown",
        static_cast<unsigned>(hit.victim), static_cast<unsigned>(hit.attacker), hit.point[0],
        hit.point[1], hit.point[2]);
    // A victim the mod could not place: what its own memory says (the moby's class at +0xA6 and
    // place at +0x10, as the port's world.c reads them), and the listed moby nearest the hit.
    if (hit.slot < 0 && used > 0 && used < static_cast<int>(sizeof line)) {
        if (hit.victim != 0 && hit.victim < 0x02000000u) {
            used += std::snprintf(line + used, sizeof line - static_cast<std::size_t>(used),
                                  " | its class %d at %.1f %.1f %.1f",
                                  read<std::int16_t>(hit.victim + 0xA6),
                                  read<float>(hit.victim + 0x10), read<float>(hit.victim + 0x14),
                                  read<float>(hit.victim + 0x18));
        }
        const std::vector<openrac_moby>& mobys = g.last_mobys.empty() ? g.mobys : g.last_mobys;
        const openrac_moby* nearest = nullptr;
        float best = 1e30f;
        for (const openrac_moby& m : mobys) {
            float d2 = 0.0f;
            for (int i = 0; i < 3; ++i) {
                const float d = m.position[i] - hit.point[i];
                d2 += d * d;
            }
            if (d2 < best && m.slot != g.mod->figure_slot(-1)) {
                best = d2;
                nearest = &m;
            }
        }
        if (nearest != nullptr && used > 0 && used < static_cast<int>(sizeof line)) {
            std::snprintf(line + used, sizeof line - static_cast<std::size_t>(used),
                          " | nearest listed: slot %d class %d%s, %.1f away", nearest->slot,
                          nearest->oclass,
                          (nearest->flags & OPENRAC_MOBY_CREATURE) != 0 ? " (creature)" : "",
                          std::sqrt(best));
        }
    }
    g.mod->info(line);
}

// Whether a post is the same hit posted again (same attacker, same victim, lately); notes it.
bool repeated(openrac_gaddr victim, openrac_gaddr attacker) {
    if (attacker == 0) {
        return false;
    }
    for (Post& p : g.posts) {
        if (p.victim == victim && p.attacker == attacker) {
            const bool again = g.clock - p.time < kRepeatSeconds;
            p.time = g.clock;
            return again;
        }
    }
    g.posts.push_back({victim, attacker, g.clock});
    return false;
}

// The ring's write index now, 0 to 63; -1 without the L00 program.
int ring_index() {
    const openrac_gaddr results = g.mod->api().relocate_level_data(0, g.results);
    if (results == 0) {
        return -1;
    }
    return static_cast<int>(read<std::uint32_t>(results + kResultsIndex) & 0x3Fu);
}

// The records the game wrote since the last tick, oldest first. With `take` false the index is
// only noted (under a menu, in a scene, a new level).
void read_hits(bool take) {
    const int now = ring_index();
    if (now < 0) {
        g.hit_index = -1;
        return;
    }
    if (!take || g.hit_index < 0) {
        g.hit_index = now;
        return;
    }
    const openrac_gaddr ring = g.mod->api().relocate_level_data(0, g.hit_ring);
    for (int i = g.hit_index; i != now; i = (i + 1) % kHitRecords) {
        const openrac_gaddr record = ring + static_cast<openrac_gaddr>(i) * kHitSize;
        const openrac_gaddr victim = read<std::uint32_t>(record + kHitVictim);
        if (victim == 0) {
            continue;
        }
        Hit hit{};
        hit.victim = victim;
        hit.attacker = read<std::uint32_t>(record + kHitAttacker);
        slot_of(hit.attacker);  // to learn where the moby array starts
        hit.slot = slot_of(victim);
        hit.mask = read<std::uint32_t>(record + kHitMask);
        hit.damage = read<float>(record + kHitDamage);
        for (int k = 0; k < 3; ++k) {
            hit.point[k] = read<float>(record + kHitPoint + 4u * static_cast<openrac_gaddr>(k));
        }
        const char* skipped = repeated(victim, hit.attacker) ? "repeated post" : add_hit(hit);
        if (g.s.log_hits) {
            log_hit(hit, skipped);
        }
    }
    g.hit_index = now;
    // The posts not seen again for a while are other hits now.
    // Deaths are only kept for the hits of the same moment.
    g.deaths.erase(std::remove_if(g.deaths.begin(), g.deaths.end(),
                                  [](const std::pair<int, float>& d) { return g.clock - d.second > kRepeatSeconds; }),
                   g.deaths.end());
    g.posts.erase(std::remove_if(g.posts.begin(), g.posts.end(),
                                 [](const Post& p) { return g.clock - p.time >= kRepeatSeconds; }),
                  g.posts.end());
}

// A number's text: the damage as shown (times 10), rounded to a whole number (6.6 is 7, 6.5 is
// 7), at least 1; for a critical hit, '!' after it.
std::string text_of(const Number& n) {
    char digits[16];
    std::snprintf(digits, sizeof digits, "%ld", std::max(1L, std::lround(n.damage * kShownScale)));
    std::string text = digits;
    if (n.critical && g.s.critical_mark) {
        text += '!';
    }
    return text;
}

// ---- drawing ------------------------------------------------------------------------------------

// Where a number has flown by `t` (0 to 1 of its life), on the screen: right and up, in world
// units.
void flight_at(const Number& n, float t, float& right, float& up) {
    switch (n.flight) {
    case Flight::Straight: {
        right = 0.0f;
        up = kTravel * (1.0f - (1.0f - t) * (1.0f - t));
        return;
    }
    case Flight::Arc: {
        // Thrown up and to the side, falling back past where it started.
        right = n.side * kTravel * 0.7f * t;
        up = kTravel * (2.4f * t - 3.0f * t * t);
        return;
    }
    case Flight::Bounce: {
        // Up, down onto where it started, and smaller bounces after, drifting to the side.
        right = n.side * kTravel * 0.35f * t;
        up = kTravel * 0.8f * std::fabs(std::sin(t * kPi * 2.5f)) * (1.0f - t);
        return;
    }
    case Flight::Fan:
    case Flight::Random:
    default: {
        // Fast at first, slowing down, at the number's own angle.
        const float travel = kTravel * (1.0f - (1.0f - t) * (1.0f - t));
        right = travel * n.dir[0];
        up = travel * n.dir[1];
        return;
    }
    }
}

// A number now: where it is, how far through its life (0 to 1), its opacity (the setting's as
// it appears, then less and less as it flies, nothing at the end), how big it pops, and its tilt
// (radians, positive to the right): along where it is flying, never more than kMaxTilt.
struct Placed {
    float p[3];
    float t;
    float alpha;
    float scale;
    float tilt;
};

Placed place(const Number& n, const openrac_camera& cam) {
    Placed out{};
    out.t = std::clamp(n.age / g.s.duration, 0.0f, 1.0f);
    float right = 0.0f;
    float up = 0.0f;
    flight_at(n, out.t, right, up);
    float right_next = 0.0f;
    float up_next = 0.0f;
    flight_at(n, std::min(1.0f, out.t + 0.02f), right_next, up_next);
    const float vx = right_next - right;
    const float vy = up_next - up;
    out.tilt = std::fabs(vx) + std::fabs(vy) > 1e-6f
                   ? std::clamp(std::atan2(vx, std::fabs(vy) + 0.02f), -kMaxTilt, kMaxTilt)
                   : 0.0f;
    for (int i = 0; i < 3; ++i) {
        out.p[i] = n.pos[i] - cam.left[i] * right + cam.up[i] * up;
    }
    out.alpha = g.s.opacity * (1.0f - out.t);
    out.scale = n.pop < kPopSeconds ? g.s.pop - (g.s.pop - 1.0f) * (n.pop / kPopSeconds) : 1.0f;
    if (n.critical) {
        out.scale *= kCriticalScale;
    }
    return out;
}

// How hot a number is, 0 (one hit) to 1 (the hits to full colour or more).
float heat_of(const Number& n) {
    return g.s.heat_hits > 1 ? std::clamp(static_cast<float>(n.hits - 1)
                                              / static_cast<float>(g.s.heat_hits - 1),
                                          0.0f, 1.0f)
                             : 1.0f;
}

// A number's palette colour with the glow (its glow meshes'): the kill colour (when on), the critical
// colour, else the heat step nearest its heat; 0 without the glow.
int palette_of(const Number& n) {
    if (!g.glow_active || g.palette.empty()) {
        return 0;
    }
    if (n.killed && g.s.kill_colour) {
        return g.heat_steps;
    }
    if (n.critical) {
        return g.heat_steps + 1;
    }
    return static_cast<int>(std::lround(heat_of(n) * static_cast<float>(g.heat_steps - 1)));
}

// A number's colour: the kill colour once its enemy died (with "Kill colour on"), the critical
// colour for a critical hit, else from the hit colour (one hit) to the many-hits colour
// (heat_hits hits), a step hotter with each hit added. With the glow, the colour of its glow
// meshes' step, so the two agree.
Rgb colour_of(const Number& n) {
    if (n.killed && g.s.kill_colour) {
        return g.s.killed;
    }
    if (n.critical) {
        return g.s.critical;
    }
    if (g.glow_active && !g.palette.empty()) {
        return g.palette[static_cast<std::size_t>(palette_of(n))];
    }
    return heat_colour(heat_of(n));
}

float dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// A number from -1 to 1 for glyph `i` of a number, the same every frame (from its shake phase,
// picked when it appeared): which way the glyph flies and spins when it breaks apart.
float glyph_random(const Number& n, int i, int k) {
    const float x = std::sin(n.shake * 12.9898f + static_cast<float>(i) * 78.233f
                             + static_cast<float>(k) * 37.719f)
                    * 43758.5453f;
    return (x - std::floor(x)) * 2.0f - 1.0f;
}

// Whether a number is done. A killed number lasts from its kill: its kill effect, or without
// one the whole duration again; any other its own time.
bool done(const Number& n) {
    if (n.kill_age < 0.0f) {
        return n.age >= g.s.duration;
    }
    return n.kill_age >= (g.s.kill_effect == KillEffect::None ? g.s.duration
                                                              : kKillPause + kKillSeconds);
}

// Each number's glyphs as props facing the camera.
void draw_props(const openrac_camera& cam) {
    const float right[3] = {-cam.left[0], -cam.left[1], -cam.left[2]};
    for (Number& n : g.numbers) {
        // Its glyphs, in its colour's props (with the glow, a hit that heats it or the kill
        // changes them).
        const std::string text = text_of(n);
        const int palette = palette_of(n);
        if (n.shown != text || n.shown_colour != palette) {
            give_back_props(n);
            n.shown = text;
            n.shown_colour = palette;
            for (const char c : text) {
                n.props.push_back(take_prop(glyph_of(c), palette));
            }
        }

        const Placed at = place(n, cam);

        // A killed number's time runs from its kill (kill_age): it plays the kill effect from
        // where it was when its enemy died, or without one flies on and fades out over the whole
        // duration from the kill (whatever time it had left).
        if (n.killed && n.kill_age < 0.0f) {
            n.kill_age = 0.0f;
            std::copy(at.p, at.p + 3, n.kill_from);
            n.kill_tilt = at.tilt;
        }
        const bool dying = n.killed && g.s.kill_effect != KillEffect::None;
        // Seconds into the kill effect, after the pause (0 while it holds still).
        const float kt = dying ? std::max(0.0f, n.kill_age - kKillPause) : 0.0f;
        float base[3];
        std::copy(dying ? n.kill_from : at.p, (dying ? n.kill_from : at.p) + 3, base);
        const float tilt = dying ? n.kill_tilt : at.tilt;
        const float alpha =
            dying      ? g.s.opacity * std::max(0.0f, 1.0f - kt / kKillSeconds)
            : n.killed ? g.s.opacity * std::max(0.0f, 1.0f - n.kill_age / g.s.duration)
                       : at.alpha;

        float v[3];
        for (int i = 0; i < 3; ++i) {
            v[i] = base[i] - cam.eye[i];
        }
        float depth = dot(v, cam.forward);
        const bool visible = depth > 0.6f;
        // Towards the camera, out of the enemy's body.
        const float pull = visible ? std::min(kTowardCamera, depth - 0.5f) : 0.0f;
        depth -= pull;
        // A killed number pops big as its pause begins and settles back by its end.
        const float kill_scale =
            dying && n.kill_age < kKillPause
                ? kKillPop - (kKillPop - 1.0f) * (n.kill_age / kKillPause)
                : 1.0f;
        const float h = kDigitHeight * g.s.size * (dying ? kill_scale : at.scale)
                        * std::clamp(depth / kNearDepth, 1.0f, kFarGrowth);

        // A critical number shakes, less and less as it fades.
        float shake_right = 0.0f;
        float shake_up = 0.0f;
        if (n.critical && !dying) {
            const float amount = kCriticalShake * h * (1.0f - at.t);
            shake_right = amount * std::sin(n.age * 55.0f + n.shake);
            shake_up = amount * std::cos(n.age * 47.0f + n.shake);
        }
        float centre[3];
        for (int i = 0; i < 3; ++i) {
            centre[i] = base[i] - cam.forward[i] * pull + right[i] * shake_right + cam.up[i] * shake_up;
        }
        // Spin and fall: the whole number thrown up a little to its side, spinning, falling.
        const float spin_sign = n.side < 0.0f ? -1.0f : 1.0f;
        float number_turn = tilt;
        if (dying && g.s.kill_effect == KillEffect::Spin) {
            number_turn += spin_sign * kSpin * kt;
            for (int i = 0; i < 3; ++i) {
                centre[i] += right[i] * n.side * 0.6f * kt;
            }
            centre[2] += kThrow * 0.5f * kt - 0.5f * kGravity * kt * kt;
        }

        const Rgb colour = colour_of(n);
        const float boost = g.s.boost ? kBoost : 1.0f;
        const float tint[4] = {colour.r / 255.0f * boost, colour.g / 255.0f * boost,
                               colour.b / 255.0f * boost, alpha};
        const float count = static_cast<float>(n.props.size());
        const float advance = kStyles[static_cast<int>(g.s.style)].advance;
        // The number's line: its glyphs along its turned right.
        const float line_right[3] = {std::cos(number_turn) * right[0] - std::sin(number_turn) * cam.up[0],
                                     std::cos(number_turn) * right[1] - std::sin(number_turn) * cam.up[1],
                                     std::cos(number_turn) * right[2] - std::sin(number_turn) * cam.up[2]};
        for (std::size_t i = 0; i < n.props.size(); ++i) {
            const int id = n.props[i];
            if (id < 0) {
                continue;
            }
            if (!visible) {
                g.mod->prop_hide(id, true);
                continue;
            }
            const float across = (static_cast<float>(i) - (count - 1.0f) * 0.5f) * advance * h;
            float at_glyph[3];
            for (int k = 0; k < 3; ++k) {
                at_glyph[k] = centre[k] + line_right[k] * across;
            }
            float turn = number_turn;
            // Break apart: each glyph thrown its own way, spinning its own way, falling.
            if (dying && g.s.kill_effect == KillEffect::Shatter) {
                const float rx = glyph_random(n, static_cast<int>(i), 0);
                const float ry = glyph_random(n, static_cast<int>(i), 1);
                const float rs = glyph_random(n, static_cast<int>(i), 2);
                const float outwards = (static_cast<float>(i) - (count - 1.0f) * 0.5f) * 0.8f;
                for (int k = 0; k < 3; ++k) {
                    at_glyph[k] += right[k] * (outwards + rx * 0.9f) * kt;
                }
                at_glyph[2] += kThrow * (1.0f + 0.4f * ry) * kt - 0.5f * kGravity * kt * kt;
                turn += (rs < 0.0f ? -1.0f : 1.0f) * kSpin * (0.7f + 0.6f * std::fabs(rs)) * kt;
            }
            // The card's x across (its turned right), its y away from the camera (its face, -y,
            // towards it), its z up (its turned up); then where it is.
            const float sin_t = std::sin(turn);
            const float cos_t = std::cos(turn);
            float m[16];
            for (int k = 0; k < 3; ++k) {
                m[0 + k] = (cos_t * right[k] - sin_t * cam.up[k]) * h;
                m[4 + k] = cam.forward[k] * h;
                m[8 + k] = (sin_t * right[k] + cos_t * cam.up[k]) * h;
                m[12 + k] = at_glyph[k];
            }
            m[3] = m[7] = m[11] = 0.0f;
            m[15] = 1.0f;
            g.mod->prop_hide(id, false);
            if (!g.mod->prop_pose(id, m, tint, OPENRAC_PROP_ALPHA)) {
                n.props[i] = -1;  // gone with its level
            }
        }
    }
}

void frame() {
    const openrac_play_status status = g.mod->play_status();

    // A new level (or the same one loaded again): its slots are other mobys, and its props went.
    if (status.loads != g.loads) {
        g.loads = status.loads;
        clear_numbers(true);
        g.hit_index = -1;
        g.moby_array = 0;
    }
    if (status.phase != OPENRAC_PLAY_PLAYING) {
        // Under a menu the numbers wait (their props hidden, taken again after); a scene or a
        // load ends them.
        if (status.phase == OPENRAC_PLAY_MENU) {
            for (Number& n : g.numbers) {
                give_back_props(n);
            }
            read_hits(false);
        } else {
            clear_numbers(false);
            if (status.phase == OPENRAC_PLAY_SCENE) {
                read_hits(false);
            } else {
                g.hit_index = -1;
            }
        }
        return;
    }

    if (!g.props_tried) {
        try_props();
    }
    g.clock += g.tick_seconds;
    // Where the moby array starts, as the game keeps it (for the slots of mobys deleted since).
    const openrac_gaddr pointer = g.mod->api().relocate_data(g.moby_array_pointer);
    if (pointer != 0) {
        const openrac_gaddr start = read<std::uint32_t>(pointer);
        if (start != 0 && start < 0x02000000u) {
            g.moby_array = start;
        }
    }
    list_mobys();
    read_hits(true);

    // Age the numbers. Each stays where its hit was in the world (an enemy knocked away or flung
    // off as it dies leaves its number behind); the enemy is only watched to see it die.
    for (Number& n : g.numbers) {
        n.age += g.tick_seconds;
        n.pop += g.tick_seconds;
        if (n.kill_age >= 0.0f) {
            n.kill_age += g.tick_seconds;
        }
        if (n.slot < 0) {
            continue;
        }
        const openrac_moby* m = moby_at(n.slot);
        if (m == nullptr) {
            // Gone from the level: deleted after it died, most likely.
            n.killed = true;
            n.slot = -1;
            continue;
        }
        if (!alive(*m)) {
            n.killed = true;
        }
    }
    for (Number& n : g.numbers) {
        if (done(n)) {
            drop_number(n);
        }
    }
    g.numbers.erase(std::remove_if(g.numbers.begin(), g.numbers.end(),
                                   [](const Number& n) { return done(n); }),
                    g.numbers.end());

    openrac_camera cam{};
    if (!g.use_props || !g.mod->camera_view(cam)) {
        for (Number& n : g.numbers) {
            give_back_props(n);
        }
        return;
    }
    draw_props(cam);
}

// ---- settings ---------------------------------------------------------------------------------

// A colour setting, "#RRGGBB" or "RRGGBB"; anything else keeps `fallback` (and says so).
Rgb colour_setting(openrac::Mod& mod, const char* key, Rgb fallback) {
    std::string text = mod.setting<std::string>(key);
    if (!text.empty() && text[0] == '#') {
        text.erase(0, 1);
    }
    const bool hex = text.size() == 6 && std::all_of(text.begin(), text.end(), [](char c) {
                         return std::isxdigit(static_cast<unsigned char>(c)) != 0;
                     });
    if (!hex) {
        if (!text.empty()) {
            mod.warn(std::string(key) + ": not a colour like #FFD700, the default instead");
        }
        return fallback;
    }
    const unsigned long value = std::strtoul(text.c_str(), nullptr, 16);
    return {static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8),
            static_cast<std::uint8_t>(value)};
}

void read_settings(openrac::Mod& mod) {
    Settings& s = g.s;
    s.duration = std::clamp(mod.setting<float>("duration"), 0.3f, 2.0f);
    s.size = std::clamp(mod.setting<float>("size"), 0.5f, 2.0f);
    s.creatures_only = mod.setting<bool>("creatures_only");
    s.log_hits = mod.setting<bool>("log_hits");
    s.glow = mod.setting<bool>("glow");
    const std::string style = mod.setting<std::string>("style");
    s.style = style == "original" ? Style::Original : style == "comic" ? Style::Comic : Style::Pixel;

    const std::string flight = mod.setting<std::string>("flight");
    s.flight = flight == "straight" ? Flight::Straight
               : flight == "arc"    ? Flight::Arc
               : flight == "bounce" ? Flight::Bounce
               : flight == "random" ? Flight::Random
                                    : Flight::Fan;
    s.opacity = std::clamp(mod.setting<float>("opacity"), 0.2f, 1.0f);
    const std::string pop = mod.setting<std::string>("pop");
    s.pop = pop == "none" ? 1.0f : pop == "small" ? 1.25f : pop == "large" ? 2.0f : 1.5f;
    s.boost = mod.setting<bool>("brightness_boost");
    const std::string kill_effect = mod.setting<std::string>("kill_effect");
    s.kill_effect = kill_effect == "shatter" ? KillEffect::Shatter
                    : kill_effect == "none"  ? KillEffect::None
                                             : KillEffect::Spin;
    s.hit = colour_setting(mod, "color_hit", s.hit);
    s.hot = colour_setting(mod, "color_many", s.hot);
    s.heat_hits = std::clamp(mod.setting<int>("heat_hits"), 2, 20);
    s.kill_colour = mod.setting<bool>("kill_color_on");
    s.killed = colour_setting(mod, "color_kill", s.killed);
    s.critical = colour_setting(mod, "color_critical", s.critical);
    s.critical_mark = mod.setting<bool>("critical_mark");
    s.critical_preview = std::clamp(mod.setting<int>("critical_preview"), 0, 100) / 100.0f;
    // The glow's palette follows the colours just read.
    make_palette();
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }
    if (mod.api().api_minor < 12) {
        throw openrac::Error("this mod needs OpenRAC's mod interface 1.12 or later (update OpenRAC)");
    }

    g.mod = &mod;
    read_settings(mod);

    // PAL runs 50 ticks a second, NTSC-U 60.
    const bool pal = std::strcmp(mod.api().version, "pal") == 0;
    g.tick_seconds = pal ? 1.0f / 50.0f : 1.0f / 60.0f;
    g.hit_ring = pal ? 0x00178200u : 0x00178100u;
    g.results = pal ? 0x00173F40u : 0x00173E40u;
    g.moby_array_pointer = pal ? 0x00160018u : 0x0015FF18u;

    // The moment an enemy dies (its death recorded, its bolts dropped), not when it is deleted:
    // an enemy knocked away by the killing blow flies on for a while first.
    mod.hook<openrac::rac1::Moby_RecordDeath>(
        [](auto original, std::uint32_t moby, std::int32_t flags, std::int32_t index) {
            note_death(moby);
            original(moby, flags, index);
        });
    mod.on_frame([] { frame(); });

    mod.info(std::string("loaded (") + kStyles[static_cast<int>(g.s.style)].folder + " style)");
}

}  // namespace

OPENRAC_MOD_CPP("damage-numbers", "0.8.0", load)
