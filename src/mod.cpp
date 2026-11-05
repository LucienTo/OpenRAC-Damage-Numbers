// Damage numbers: a number (the game's damage times 10) pops up where each hit on an enemy lands,
// tilts and flies off in its own direction, and fades out.
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
// - Each number stays where its hit landed in the world; its enemy (moby_list) is only watched to
//   see it die (the number turns red). Each game tick (on_frame) the numbers age and are drawn one
//   of two ways (the "style" setting):
//   - props (1.12): each digit a card (assets/digit_N.glb, its texture assets/digits.png, made by
//     tools/make_digits.py) the renderer draws in the level, turned to face the game's camera
//     (camera_view) and tilted along the number's flight. The mods share 256 props, so many
//     numbers show at once.
//   - HUD text (hud_text_set, 1.12): drawn with the game's font on the HUD's 4:3 screen, with an
//     outline made of two dark copies; mods share 16 lines, so few numbers show at once.
//   If the digit props cannot be made (no file, no room), the HUD text takes over.
// - The game's projection (docs/engine/systems/CAMERA_FOG.md): horizontal tangent 0.63, vertical
//   0.63 x 0.775 (NTSC) or 0.756 (PAL). Camera x is -left . v (right), y is -up . v (down), depth
//   forward . v. Cutscene cameras change the field of view, so numbers only show in play.
#include "openrac/mod.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kTanH = 0.63f;
// Hits on the same enemy within this many seconds add up into one number.
constexpr float kMergeSeconds = 0.35f;
// How long a number takes to pop from big to its size, in seconds.
constexpr float kPopSeconds = 0.15f;
// How far a number flies over its life, in world units (Ratchet is about one unit tall), and the
// widest angle from straight up it may fly at, either side (radians; 1.2 is about 70 degrees).
// Each number picks its own angle, so numbers close together fan out.
constexpr float kTravel = 1.4f;
constexpr float kSpread = 1.2f;
// A hit point further than this from its victim is not where the victim is (some hits carry the
// attacker's position or none): the number goes over the victim instead.
constexpr float kFarHit = 6.0f;
// How far over a victim's origin a number starts when the hit point is not used.
constexpr float kOverHead = 1.2f;

// Props: the most this mod makes (of the 256 all mods share), a digit's height in world units at
// size 1 and close by (it grows with distance, up to kFarGrowth times, so far numbers stay
// readable), the step from one digit to the next as a share of that height (the card is 7/9 of
// it wide, its outline overlapping the next one's), and how far a number is drawn towards the
// camera from where it is, so the enemy's own body does not hide it.
constexpr int kMaxProps = 200;
constexpr float kDigitHeight = 0.32f;
constexpr float kNearDepth = 7.0f;
constexpr float kFarGrowth = 2.5f;
constexpr float kDigitAdvance = 0.62f;
constexpr float kTowardCamera = 0.7f;

// HUD text: numbers this close to the screen's edge are not drawn (hud_text_set refuses places
// outside it); the HUD's screen in the game's pixels (hud.c draws mod text on 512 across and the
// screen's lines down, 416 for the game's draw buffer), for the outline's one-pixel steps; the
// lines one number takes with the outline (two dark copies under it, one a pixel left, one a pixel
// up; with the shadow flag each copy also darkens the pixel below or right of it, and the number's
// own shadow the corner).
constexpr float kEdge = 0.03f;
constexpr float kHudWidth = 512.0f;
constexpr float kHudHeight = 416.0f;
constexpr int kOutlinedLines = 3;

// The hit ring.
constexpr int kHitRecords = 64;
constexpr openrac_gaddr kHitSize = 0x40;
constexpr openrac_gaddr kHitPoint = 0x00;
constexpr openrac_gaddr kHitDamage = 0x2C;
constexpr openrac_gaddr kHitVictim = 0x34;
constexpr openrac_gaddr kResultsIndex = 0x14;

// Colours, 0 to 255 a channel. For the HUD's font 128 is its own shade and 255 about twice it, so
// a strong colour needs channels near 255 and near 0; props are tinted by them (255 is 1).
constexpr std::uint8_t kHitColour[3] = {255, 215, 0};   // bright yellow
constexpr std::uint8_t kBigColour[3] = {255, 120, 0};   // orange: a number that added up
constexpr std::uint8_t kKillColour[3] = {255, 40, 40};  // red: the hit that killed
// A number that added up to this much (the game's own damage) turns orange.
constexpr float kBigDamage = 4.0f;
// The numbers show the game's damage times this (the game's are small: 1 for a wrench swing).
constexpr float kShownScale = 10.0f;
// How opaque a number is when it appears; it fades from there to nothing over its flight.
constexpr float kOpacity = 0.8f;

struct Number {
    int slot = -1;           // the victim's slot, -1 once it is gone
    float offset[3] = {};    // the hit point from the victim's origin
    float pos[3] = {};       // where the number is in the world (before it flies off)
    float dir[2] = {0.0f, 1.0f};  // where it flies on the screen: right and up, a unit vector
    float damage = 0.0f;
    float age = 0.0f;        // since it appeared
    float pop = 0.0f;        // since it last popped big (it appeared, or a hit added to it)
    float since_hit = 0.0f;  // since its last hit
    bool killed = false;
    // Props: the text its digit props show, and the props (-1 for a digit without one).
    std::string shown;
    std::vector<int> props;
};

struct State {
    openrac::Mod* mod = nullptr;
    float duration = 0.8f;
    float size = 1.0f;
    int max_numbers = 5;
    bool creatures_only = true;
    bool outline = true;
    bool props_wanted = true;
    // HUD lines a number takes (kOutlinedLines with the outline, else 1).
    int lines_per_number = kOutlinedLines;
    float tick_seconds = 1.0f / 60.0f;
    float tan_v = 0.63f * 0.775f;
    // The hit ring and the collision results record (the write index), in the L00 program.
    openrac_gaddr hit_ring = 0;
    openrac_gaddr results = 0;
    // The ring's write index as the last tick left it; -1 to take it as it is.
    int hit_index = -1;

    std::uint32_t loads = ~0u;
    std::uint32_t random = 0x9E3779B9u;
    std::vector<Number> numbers;
    // This tick's mobys, and where each slot is in it (-1 for none).
    std::vector<openrac_moby> mobys;
    std::vector<int> by_slot;

    // Props of this level load: each digit's spare ones (hidden), how many the mod made, whether
    // they were tried (the first tick in the level) and whether they work (else the HUD text).
    std::vector<int> spare[10];
    int props_made = 0;
    bool props_tried = false;
    bool use_props = false;
    bool hud_shown = false;
    bool warned = false;
};

State g;

const openrac_moby* moby_at(int slot) {
    if (slot < 0 || slot >= static_cast<int>(g.by_slot.size()) || g.by_slot[slot] < 0) {
        return nullptr;
    }
    return &g.mobys[static_cast<std::size_t>(g.by_slot[slot])];
}

bool alive(const openrac_moby& m) {
    return (m.flags & OPENRAC_MOBY_DEAD) == 0 && m.state < 0x80;
}

void list_mobys() {
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

template <class T>
T read(openrac_gaddr address) {
    T value;
    std::memcpy(&value, g.mod->api().memory + address, sizeof value);
    return value;
}

// ---- props ------------------------------------------------------------------------------------

// A prop showing `digit`: a spare one shown again, else a new one; -1 when none can be had.
int take_prop(int digit) {
    std::vector<int>& spare = g.spare[digit];
    if (!spare.empty()) {
        const int id = spare.back();
        spare.pop_back();
        g.mod->prop_hide(id, false);
        return id;
    }
    if (g.props_made >= kMaxProps) {
        return -1;
    }
    char path[32];
    std::snprintf(path, sizeof path, "assets/digit_%d.glb", digit);
    const int id = g.mod->prop_create(path);
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
            g.spare[n.shown[i] - '0'].push_back(id);
        }
    }
    n.props.clear();
    n.shown.clear();
}

// The first tick in a level: one prop of each digit, made now (the port reads a new mesh with
// the level's scenery, a hitch, so it comes at the start rather than at the first hit) and kept
// spare. None at all: the props do not work here, and the HUD text takes over.
void try_props() {
    g.props_tried = true;
    g.use_props = false;
    if (!g.props_wanted) {
        return;
    }
    int made = 0;
    for (int d = 0; d < 10; ++d) {
        const int id = take_prop(d);
        if (id >= 0) {
            g.mod->prop_hide(id, true);
            g.spare[d].push_back(id);
            ++made;
        }
    }
    g.use_props = made == 10;
    if (!g.use_props && !g.warned) {
        g.warned = true;
        g.mod->warn("the digit props could not be made (assets/digit_N.glb): HUD text instead");
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
}

// One hit: `damage` to the moby in `slot`, at `point`.
void add_hit(int slot, float damage, const float point[3]) {
    if (!(damage > 0.0f) || !std::isfinite(damage) || slot < 0) {
        return;
    }
    // Hits on Ratchet are not shown.
    if (slot == g.mod->figure_slot(-1)) {
        return;
    }
    const openrac_moby* victim = moby_at(slot);
    if (victim == nullptr) {
        return;
    }
    if (g.creatures_only && (victim->flags & OPENRAC_MOBY_CREATURE) == 0) {
        return;
    }

    // Hits close together on the same enemy add up, while the number is in the first half of its
    // flight; it pops again but keeps flying where it was going (its age is left alone).
    for (Number& n : g.numbers) {
        if (n.slot == slot && !n.killed && n.since_hit < kMergeSeconds
            && n.age < g.duration * 0.5f) {
            n.damage += damage;
            n.since_hit = 0.0f;
            n.pop = 0.0f;
            return;
        }
    }

    Number n;
    n.slot = slot;
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = point[i] - victim->position[i];
        d2 += d * d;
    }
    // The sphere tests leave the point at 0: over the victim's head then.
    const bool near = std::isfinite(d2) && d2 <= kFarHit * kFarHit;
    for (int i = 0; i < 3; ++i) {
        n.offset[i] = near ? point[i] - victim->position[i] : 0.0f;
    }
    if (!near) {
        n.offset[2] = kOverHead;
    }
    for (int i = 0; i < 3; ++i) {
        n.pos[i] = victim->position[i] + n.offset[i];
    }
    const float angle = random_signed() * kSpread;
    n.dir[0] = std::sin(angle);
    n.dir[1] = std::cos(angle);
    n.damage = damage;
    g.numbers.push_back(n);
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
        float point[3];
        for (int k = 0; k < 3; ++k) {
            point[k] = read<float>(record + kHitPoint + 4u * static_cast<openrac_gaddr>(k));
        }
        add_hit(g.mod->api().moby_slot_of(victim), read<float>(record + kHitDamage), point);
    }
    g.hit_index = now;
}

// The HUD text's form: a whole number, or one decimal (of the damage as shown, times 10).
void format_damage(float damage, char* out, std::size_t size) {
    damage *= kShownScale;
    const float rounded = std::round(damage);
    if (std::fabs(damage - rounded) < 0.05f) {
        std::snprintf(out, size, "%d", static_cast<int>(rounded));
    } else {
        std::snprintf(out, size, "%.1f", damage);
    }
}

// ---- drawing ------------------------------------------------------------------------------------

// Where a number is now (it flies fast at first and slows down, across the view: along the
// camera's right, -left, and up), how far through its life (0 to 1), and its opacity (kOpacity
// as it appears, then less and less as it flies, nothing at the end).
struct Placed {
    float p[3];
    float t;
    float alpha;
    float pop_scale;
};

Placed place(const Number& n, const openrac_camera& cam) {
    Placed out{};
    out.t = std::clamp(n.age / g.duration, 0.0f, 1.0f);
    const float travel = kTravel * (1.0f - (1.0f - out.t) * (1.0f - out.t));
    const float right_step = travel * n.dir[0];
    const float up_step = travel * n.dir[1];
    for (int i = 0; i < 3; ++i) {
        out.p[i] = n.pos[i] - cam.left[i] * right_step + cam.up[i] * up_step;
    }
    out.alpha = kOpacity * (1.0f - out.t);
    out.pop_scale = n.pop < kPopSeconds ? 1.5f - 0.5f * (n.pop / kPopSeconds) : 1.0f;
    return out;
}

const std::uint8_t* colour_of(const Number& n) {
    return n.killed ? kKillColour : n.damage >= kBigDamage ? kBigColour : kHitColour;
}

float dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void hide_hud() {
    if (g.hud_shown) {
        g.mod->hud_text_set({});
        g.hud_shown = false;
    }
}

// Each number's digits as props facing the camera.
void draw_props(const openrac_camera& cam) {
    const float right[3] = {-cam.left[0], -cam.left[1], -cam.left[2]};
    for (Number& n : g.numbers) {
        // Whole numbers only (the font has no point); at least 1.
        char text[16];
        std::snprintf(text, sizeof text, "%ld", std::max(1L, std::lround(n.damage * kShownScale)));
        if (n.shown != text) {
            give_back_props(n);
            n.shown = text;
            for (const char* c = text; *c != '\0'; ++c) {
                n.props.push_back(take_prop(*c - '0'));
            }
        }

        const Placed at = place(n, cam);
        float v[3];
        for (int i = 0; i < 3; ++i) {
            v[i] = at.p[i] - cam.eye[i];
        }
        float depth = dot(v, cam.forward);
        const bool visible = depth > 0.6f;
        // Towards the camera, out of the enemy's body.
        const float pull = visible ? std::min(kTowardCamera, depth - 0.5f) : 0.0f;
        depth -= pull;
        float centre[3];
        for (int i = 0; i < 3; ++i) {
            centre[i] = at.p[i] - cam.forward[i] * pull;
        }
        const float h = kDigitHeight * g.size * at.pop_scale
                        * std::clamp(depth / kNearDepth, 1.0f, kFarGrowth);

        // Tilted towards where it flies: its up along its direction on the screen, its digits
        // across at right angles to that (the camera's right turned the same way).
        float card_up[3];
        float card_right[3];
        for (int k = 0; k < 3; ++k) {
            card_up[k] = n.dir[0] * right[k] + n.dir[1] * cam.up[k];
            card_right[k] = n.dir[1] * right[k] - n.dir[0] * cam.up[k];
        }

        const std::uint8_t* colour = colour_of(n);
        const float tint[4] = {colour[0] / 255.0f, colour[1] / 255.0f, colour[2] / 255.0f, at.alpha};
        const float count = static_cast<float>(n.props.size());
        for (std::size_t i = 0; i < n.props.size(); ++i) {
            const int id = n.props[i];
            if (id < 0) {
                continue;
            }
            if (!visible) {
                g.mod->prop_hide(id, true);
                continue;
            }
            const float across = (static_cast<float>(i) - (count - 1.0f) * 0.5f) * kDigitAdvance * h;
            // The card's x across (its tilted right), its y away from the camera (its face, -y,
            // towards it), its z up (its tilted up); then where it is.
            float m[16];
            for (int k = 0; k < 3; ++k) {
                m[0 + k] = card_right[k] * h;
                m[4 + k] = cam.forward[k] * h;
                m[8 + k] = card_up[k] * h;
                m[12 + k] = centre[k] + card_right[k] * across;
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

// The numbers as lines of HUD text (the game's font), as many as fit in the shared 16.
void draw_hud(const openrac_camera& cam) {
    std::vector<openrac_hud_text> lines;
    // The newest numbers, as many as may show; oldest first, so the newest draw on top.
    const std::size_t from = g.numbers.size() > static_cast<std::size_t>(g.max_numbers)
                                 ? g.numbers.size() - static_cast<std::size_t>(g.max_numbers)
                                 : 0;
    for (std::size_t k = from; k < g.numbers.size(); ++k) {
        const Number& n = g.numbers[k];
        const Placed at = place(n, cam);
        float v[3];
        for (int i = 0; i < 3; ++i) {
            v[i] = at.p[i] - cam.eye[i];
        }
        const float depth = dot(v, cam.forward);
        if (depth < 0.5f) {
            continue;
        }
        const float x = 0.5f + 0.5f * -dot(v, cam.left) / (depth * kTanH);
        const float y = 0.5f + 0.5f * -dot(v, cam.up) / (depth * g.tan_v);
        if (!(x >= kEdge && x <= 1.0f - kEdge && y >= kEdge && y <= 1.0f - kEdge)) {
            continue;
        }
        // Smaller further away; big for a moment when it appears.
        const float scale =
            std::clamp(g.size * std::clamp(8.0f / depth, 0.6f, 1.4f) * at.pop_scale, 0.25f, 4.0f);

        openrac_hud_text line{};
        format_damage(n.damage, line.text, sizeof line.text);
        line.x = x;
        line.y = y;
        line.scale = scale;
        std::memcpy(line.rgba, colour_of(n), 3);
        line.rgba[3] = static_cast<std::uint8_t>(at.alpha * 255.0f + 0.5f);
        line.flags = OPENRAC_HUD_SHADOW;
        if (g.outline) {
            // A pixel at the game's size, more when the number is drawn bigger.
            const float px = static_cast<float>(std::max(1L, std::lround(scale)));
            openrac_hud_text dark = line;
            dark.rgba[0] = dark.rgba[1] = dark.rgba[2] = 0;
            dark.x = x - px / kHudWidth;
            lines.push_back(dark);
            dark.x = x;
            dark.y = y - px / kHudHeight;
            lines.push_back(dark);
        }
        lines.push_back(line);
    }
    // Other mods' lines count against the same 16: with too many, the oldest numbers give way,
    // a whole number (its outline too) at a time.
    while (!lines.empty() && !g.mod->hud_text_set(lines)) {
        const std::size_t drop = std::min(lines.size(), static_cast<std::size_t>(g.lines_per_number));
        lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    g.hud_shown = !lines.empty();
    if (lines.empty()) {
        g.mod->hud_text_set({});
    }
}

void frame() {
    const openrac_play_status status = g.mod->play_status();

    // A new level (or the same one loaded again): its slots are other mobys, and its props went.
    if (status.loads != g.loads) {
        g.loads = status.loads;
        clear_numbers(true);
        g.hit_index = -1;
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
        hide_hud();
        return;
    }

    if (!g.props_tried) {
        try_props();
    }
    list_mobys();
    read_hits(true);

    // Age the numbers. Each stays where its hit was in the world (an enemy knocked away or flung
    // off as it dies leaves its number behind); the enemy is only watched to see it die.
    for (Number& n : g.numbers) {
        n.age += g.tick_seconds;
        n.pop += g.tick_seconds;
        n.since_hit += g.tick_seconds;
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
        if (n.age >= g.duration) {
            drop_number(n);
        }
    }
    g.numbers.erase(std::remove_if(g.numbers.begin(), g.numbers.end(),
                                   [](const Number& n) { return n.age >= g.duration; }),
                    g.numbers.end());

    openrac_camera cam{};
    if (!g.mod->camera_view(cam)) {
        for (Number& n : g.numbers) {
            give_back_props(n);
        }
        hide_hud();
        return;
    }
    if (g.use_props) {
        hide_hud();
        draw_props(cam);
    } else {
        draw_hud(cam);
    }
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }
    if (mod.api().api_minor < 12) {
        throw openrac::Error("this mod needs OpenRAC's mod interface 1.12 or later (update OpenRAC)");
    }

    g.mod = &mod;
    g.duration = std::clamp(mod.setting<float>("duration"), 0.3f, 2.0f);
    g.size = std::clamp(mod.setting<float>("size"), 0.5f, 2.0f);
    g.creatures_only = mod.setting<bool>("creatures_only");
    g.props_wanted = mod.setting<std::string>("style") != "hud";
    g.outline = mod.setting<bool>("outline");
    g.lines_per_number = g.outline ? kOutlinedLines : 1;
    // HUD text: mods share 16 lines, so no more numbers than fit in them.
    g.max_numbers = std::clamp(mod.setting<int>("max_numbers"), 1,
                               OPENRAC_HUD_TEXTS_MAX / g.lines_per_number);

    // PAL runs 50 ticks a second and has a slightly flatter picture than NTSC.
    const bool pal = std::strcmp(mod.api().version, "pal") == 0;
    g.tick_seconds = pal ? 1.0f / 50.0f : 1.0f / 60.0f;
    g.tan_v = kTanH * (pal ? 0.756f : 0.775f);
    g.hit_ring = pal ? 0x00178200u : 0x00178100u;
    g.results = pal ? 0x00173F40u : 0x00173E40u;

    mod.on_frame([] { frame(); });

    mod.info(g.props_wanted ? "loaded (numbers as props)" : "loaded (numbers as HUD text)");
}

}  // namespace

OPENRAC_MOD_CPP("damage-numbers", "0.7.1", load)
