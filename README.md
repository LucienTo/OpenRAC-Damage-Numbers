# Damage numbers

Damage numbers for Ratchet & Clank on [OpenRAC](https://openrac.dev)'s native PC port
(PAL and NTSC-U). Every hit, from the wrench and weapons, shows how much damage
it did: a number pops up where the hit landed, flies off and fades out. Choose the digits'
style (pixel, original or comic), how numbers fly (fan out, straight up, arc, bounce or a
random mix) and their colours.

Hits on the same enemy add up into one number while it is still showing. Each hit starts it
again like a new number, at that hit, with the added-up value, and a step hotter in colour:
yellow for one hit, through orange, to red after six hits, by default. When the enemy dies,
its number turns red, holds still for a moment, then spins as it falls to the ground, or breaks
apart. The numbers show
the game's damage times 10: a wrench swing does 1, shown as 10.

## Install

In the OpenRAC launcher's Mods tab, find **Damage numbers** and install it, then switch
it on. It is listed on OpenRAC's mod page:
[openrac.dev/mods/damage-numbers](https://openrac.dev/mods/damage-numbers).

Or paste this repository's address in the Mods tab (*Add from a link*), or download the
`.zip` from the [latest release](https://github.com/LucienTo/OpenRAC-Damage-Numbers/releases/latest)
and add it there.

- Needs OpenRAC 1.0.2 or later (mod interface 1.12).
- Ratchet & Clank, PAL or NTSC-U.
- Windows, Linux and macOS, with the Vulkan or the OpenGL renderer. Not on Android, where
  OpenRAC loads no native mods.
- Works alongside other mods.

## Settings

| Setting | Default | |
|---|---|---|
| Style | original | How the digits look. Pixel: a chunky pixel font. Original: wide square digits with rounded corners, shaded bright to dark, like a game menu. Comic: square blocky digits with their outline set off down and to the right |
| Size | 1.0 | How big the numbers are |
| Opacity at start | 0.8 | How solid a number is when it appears; it fades from there to nothing |
| Pop size | normal | How much bigger a number is for a moment when it appears: none, small, normal or large |
| Flight style | random | Fan out (up and to either side), Straight up, Arc (thrown up and to the side, falling back down), Bounce (drops and bounces), or Random (each number picks one) |
| Duration | 0.8 s | How long a number flies and fades before it is gone |
| Hit and many-hits colours | `#FFD700`, `#FF1E00` | A number starts in the hit colour and turns a step towards the many-hits colour with each hit added to it. Each as `#RRGGBB` |
| Hits to full colour | 6 | How many hits turn a number fully the many-hits colour |
| Kill colour on | off | Show the hit that kills in the kill colour. Off, it keeps the colour its hits gave it (many enemies die in one hit) |
| Kill colour | `#FF2828` | The number of the hit that kills, with Kill colour on, as `#RRGGBB` |
| Kill effect | break apart | What the number does when its enemy dies: Spin and fall (jumps a little, spins and drops to the ground), Break apart (its digits fly apart, each spinning and falling), or None (it flies on as any number, and fades out over the whole duration from the kill). It fades out as it goes |
| Enemies only | on | Only enemies and people, not crates or other objects |
| Brightness boost | off | Brighter numbers, so dark places do not dim them |
| Glow in the dark (experimental) | off | Numbers light themselves, so they show in dark places. Only with OpenRAC's enhanced rendering on (Vulkan only): at the start of each level the mod checks, and without it uses the regular numbers. They glow in the colours set above; the hit colours in up to 6 steps |
| Critical colour (experimental) | `#3CE6FF` | The number of a critical hit, as `#RRGGBB` |
| Critical mark (experimental) | off | An exclamation mark after a critical hit's number |
| Critical preview (experimental) | 0% | The game has no critical hits yet: this share of hits is shown as critical (bigger, shaking, in the critical colour) to try the look |
| Log hits | off | For bug reports: a line in the game's log for every hit the mod reads (what was hit, the kind and damage of the hit, who hit it, and whether a number showed or why not) |

## Known limits

- Numbers are drawn in the level, so a wall or anything else between a number and the
  camera hides it, and they are lit as the level is: dark places dim them, as in Blarg
  Station's dark hallway (Glow in the dark is the experiment against that).
- Numbers move once a game tick (50 or 60 a second), not on the extra frames the port
  draws between ticks.
- Not tested yet: the PAL version.

## Reporting bugs

Open an issue: [github.com/LucienTo/OpenRAC-Damage-Numbers/issues](https://github.com/LucienTo/OpenRAC-Damage-Numbers/issues).
Say which version of the mod and of OpenRAC you use, PAL or NTSC-U, and what you saw.

## Changes

| Version | |
|---|---|
| 0.8.1 | New defaults: the original style, random flight, break apart as the kill effect, brightness boost and the critical mark off. Settings you changed yourself stay as you set them |
| 0.8.0 | Three digit styles (pixel, original, comic), flight styles (fan out, straight up, arc, bounce, random), opacity, pop size, brightness boost, custom colours, and critical hits ready for later (with a preview setting). The HUD text style is gone: numbers are always drawn in the world. Each hit on an enemy restarts its number at that hit with the added-up value, a step hotter in colour (yellow to red) per hit; a kill effect (spin and fall, or break apart) after a short pause; the kill colour as a setting, off by default; Glow in the dark (experimental, enhanced rendering only) for dark places; a Log hits setting for bug reports. Fixed: a frog's bite showed numbers on other frogs; frogs killed outright early in a level showed no number; a wrench swing hitting several enemies showed 10, 20, 30 instead of 10 each; an enemy killed outright by a weapon (Logging Site's exploding one) showed no number; a kill counts at the killing blow, not when an enemy that flies off is deleted |
| 0.7.1 | The release archives build the same on every platform (no change in the game) |
| 0.7.0 | A number stays where its hit landed instead of following the enemy |
| 0.6.0 | Damage shown times 10, smaller digits, numbers tilt towards where they fly and fade from 80% |
| 0.5.0 | Numbers drawn in the world as outlined digit props, room for every hit; HUD text as a setting |
| 0.4.0 | Duration 0.8 s; a dark outline for HUD text |
| 0.3.0 | Each number flies off in its own direction, shorter, fading as it goes |
| 0.2.0 | The wrench's and bombs' hits are shown too; brighter colours; works with Multiplayer |
| 0.1.0 | First version |

---

## For developers

### Building

Windows, MSVC only: `build.bat` builds `build\package\damage-numbers\`. Copy that
folder to `%USERPROFILE%\OpenRAC\active\rac1\data\plugins\` and switch the mod on in
the launcher's Mods tab.

Anywhere else, with CMake: `cmake -S . -B build -G Ninja -DOPENRAC_SDK=<OpenRAC>/sdk`
and `cmake --build build`. The mod needs mod interface 1.12, which no SDK release
carries yet (the newest is 1.11), so `tools/fetch_sdk.py` and `sdk.lock` only work
once `sdk-v1.12.0` is released; until then point `OPENRAC_SDK` at an OpenRAC
checkout's `sdk/` folder.

The glyph props are one set per style in `assets/pixel/`, `assets/original/` and
`assets/comic/`: `digit_0.glb` to `digit_9.glb` and `bang.glb` (the critical mark), and
their texture `glyphs.png`, all made by `python tools/make_digits.py` (standard library
only; all three styles are this project's own drawing, none taken from the game). The
order of the glyphs there is the order of `kGlyphFiles` in `src/mod.cpp`, and each
style's spacing is in `kStyles`.

### Releasing

`.github/workflows/build.yml` (the SDK template's) builds the mod on every push for
Windows, Linux, macOS and Android, against OpenRAC's `sdk/` at a pinned commit
(`OPENRAC_SDK_COMMIT`, OpenRAC 1.0.2). Push a tag named after the manifest's version,
for example `v0.7.1`, and it publishes the archives and `SHA256SUMS` on that release,
which is what the launcher installs from.

- Bump the version in both `openrac-mod.json` and `OPENRAC_MOD_CPP` in `src/mod.cpp`
  (the port checks they agree), and add a line to **Changes** above.
- `.gitattributes` keeps text files LF on every machine: the workflow joins each
  platform's archive into one and refuses files (LICENSE, README) that differ between
  them, which a Windows CRLF checkout would make them.
- The mod is listed on OpenRAC from
  [OpenRAC/openrac-mods#87](https://github.com/OpenRAC/openrac-mods/issues/87). A new
  release of a native mod from another repository is offered to players after
  OpenRAC's review (24 hours and a maintainer's approval).

### How it works

The game posts each hit as a record in a ring of 64, which the mod reads each game
tick (the records written since the last one). It does not use `on_moby_hit`: the
port tells mods only the hits posted through `PostHitMessage` and `record_moby_hit`,
while the wrench's swing and the sphere tests (bombs) are written by the collision
code itself. Each number stays where its hit landed in the world; the enemy
(`moby_list`) is only watched to see it die.

The game posts the same hit again while it lasts: a wrench swing tests its sweep each
frame it is active and posts to every enemy still in it, though the enemy takes the hit
once. A post from the same attacker to the same enemy within 0.15 s of the last is
counted as that same hit.

The enemy a hit was on is looked up in the mobys as they were the tick before (an enemy one
shot kills is deleted by the time its hit is read). For a deleted moby the port's
`moby_slot_of` answers -1, so the mod works its slot out as the port does, `(address - start of
the moby array) / 0x100`, the array's start read each tick from the game's pointer to it (PAL
`D_00160018`, NTSC-U `0x0015FF18`, as the port's `world.c` reads it). A bomb kills a Blarg Station frog outright, and this is what
gives it a number.

A number turns killed when the game records its enemy's death: the mod hooks
`rac1.moby.record_death` (`Moby_RecordDeath`, which drops the enemy's bolts). Enemies call it at
the killing blow, before a death flight: the peckbot starts its knock-back (state `0x63`) and
records its death in the same moment, and is deleted only when it lands (`peckbot.c`). Without
it, a number would only turn killed when the enemy is deleted or marked dead.

Only hits of the kinds enemies take damage from show: the record's mask has one of the bits of
`0x330000` or `0x800000` (most enemies read their hits with `0x330000`; the wrench's is
`0x10000`). A sphere attack posts to every moby in it, and an enemy's own attack carries none of
these bits: a Blarg Station frog's bite (mask `1`) reached the frogs around it and showed
numbers on them, though they took no damage.

Blarg Station's frogs (class 827) do get numbers (the "Log hits" setting showed every wrench
hit on one shown), but they live in a dark hallway, and props are lit as the level's mobys
are, so the numbers there are too dark to see. "Glow in the dark" uses glyph meshes whose
material has the glyph texture as its emission too: OpenRAC's enhanced rendering adds a
material's emission after the lighting (`port/scene/level_renderer.cpp`, `material_emission`),
so they light themselves. The prop's tint does not colour the emission, so the colour is in
the mesh: at the game's start the mod writes a set of glyph meshes for each colour a number can
have (`write_glow_meshes`: up to 6 heat steps from the hit colour to the many-hits colour, the
kill colour, the critical colour; emission the colour times 0.8) into its own folder,
`assets/<style>/tinted/<RRGGBB>/` (the folder found from where its library was loaded; the port
takes a prop's mesh only from the mod's folder), and makes one prop of each at the start of a
level: a mesh the level was read without makes the port read the level again, a hitch, so it
comes with the loading. A number swaps to its colour's props when a hit heats it or it dies.
The port adds emission only with enhanced rendering, which it turns on only with Vulkan
(`renderer/gl.h`, `enhanced_rendering_available`), so at the start of each level the mod uses
the glow meshes only when the game draws with Vulkan (`graphics().backend`) and the port's
settings file (`settings/<game>-<disc>.ini`, beside `mod-data/` in OpenRAC's data folder) has
`enhanced_rendering = 1`; else the regular glyphs, and the game's log says why.

Critical hits: the game has none. `is_critical()` in `src/mod.cpp` is where a rule goes
(per weapon, from the hit's attacker and mask); for now it only answers the "Critical
preview" setting.

Each glyph is a prop (a small card with the glyph's texture) that the renderer draws in
the level, turned each game tick to face the game's camera (`camera_view`) and tilted
along the number's flight. The mod uses up to a maximum of 200 of the 256 props all mods
share, kept and reused for the whole level; past that, a new number's digits are not drawn
until older numbers have gone (might be changed in future updates).

Up to 0.7, numbers could also be HUD text (`hud_text_set`, the game's font). That style is
gone: mods share only 16 HUD lines (`OPENRAC_HUD_TEXTS_MAX`). That limit likely protects
the game's display list: the port draws each glyph natively but also writes a 48-byte
marker into the game's display list (`openrac_ui_send`, at `g_render_packet_cursor`), with
no check of the room left, and the game drops a frame whose display list overflows
(`VU1_sendChain`).

The hit ring's addresses come from OpenRAC's `port/game/rac1/host/collision.c` and
`world.c`: PAL ring L00 `0x00178200`, results L00 `0x00173F40`; NTSC-U `0x00178100`
and `0x00173E40`. The moby array's pointer: PAL `0x00160018`, NTSC-U `0x0015FF18`
(`rac1-ntsc/host/rac1_version.h`).
