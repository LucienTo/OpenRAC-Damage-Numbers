# Damage numbers

Damage numbers for Ratchet & Clank on [OpenRAC](https://openrac.dev)'s native PC port
(PAL and NTSC-U). Every hit, from the wrench and weapons, shows how much damage
it did: a number pops up where the hit landed, tilts and flies off in its own direction,
and fades out. Yellow for a hit, orange when hits add up, red for the one that kills.

Hits close together on the same enemy add up into one number. The numbers show the
game's damage times 10: a wrench swing does 1, shown as 10.

## Install

Paste this repository's address in the OpenRAC launcher's Mods tab (*Add from a link*),
then switch the mod on. Or download the `.zip` from the latest release and add it there.

- Needs OpenRAC 1.0.2 or later (mod interface 1.12).
- Ratchet & Clank, PAL or NTSC-U.
- Windows, Linux and macOS, with the Vulkan or the OpenGL renderer. Not on Android, where
  OpenRAC loads no native mods.
- Works alongside other mods.

## Settings

| Setting | Default | |
|---|---|---|
| Duration | 0.8 s | How long a number flies and fades before it is gone |
| Size | 1.0 | How big the numbers are |
| Style | in the world | In the world: outlined digits drawn in the level, facing the camera, with room for every hit. HUD text: the game's own font, at most 16 lines shared by all mods* |
| HUD text outline | on | HUD text only: a dark outline round each number (3 HUD lines a number) |
| HUD text numbers at once | 5 | HUD text only: the most shown at once (at most 5 with the outline) |
| Enemies only | on | Only enemies and people, not crates or other objects |

*16 lines shared by all mods will probably be increased in future updates, so then I can increase numbers shown on screen in the HUD style

## Known limits

- Numbers move once a game tick (50 or 60 a second), not on the extra frames the port
  draws between ticks.
- Not tested yet: the PAL version.

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

The digit props are `assets/digit_0.glb` to `digit_9.glb` and their texture
`assets/digits.png`, made by `python tools/make_digits.py` (standard library only).

### Releasing

`.github/workflows/build.yml` (the SDK template's) builds the mod on every push for
Windows, Linux, macOS and Android, against OpenRAC's `sdk/` at a pinned commit
(`OPENRAC_SDK_COMMIT`, OpenRAC 1.0.2). Push a tag named after the manifest's version,
for example `v0.7.0`, and it publishes the archives and `SHA256SUMS` on that release,
which is what the launcher installs from.

### How it works

The game posts each hit as a record in a ring of 64, which the mod reads each game
tick (the records written since the last one). It does not use `on_moby_hit`: the
port tells mods only the hits posted through `PostHitMessage` and `record_moby_hit`,
while the wrench's swing and the sphere tests (bombs) are written by the collision
code itself. Each number stays where its hit landed in the world; the enemy
(`moby_list`) is only watched to see it die.

In the world (the default), each digit is a prop (a small card with the digit's
texture) that the renderer draws in the level, turned each game tick to face the
game's camera (`camera_view`) and tilted along the number's flight. The mod uses up
to a maximum of 200 of the 256 props all mods share, kept and reused for the whole level; past
that, a new number's digits are not drawn until older numbers have gone(might be changed in future updates).

As HUD text, the number is projected with the game's camera and field of view
(horizontal tangent 0.63) onto the HUD's 4:3 screen and drawn with the game's font
through `hud_text_set`.

The hit ring's addresses come from OpenRAC's `port/game/rac1/host/collision.c` and
`world.c`: PAL ring L00 `0x00178200`, results L00 `0x00173F40`; NTSC-U `0x00178100`
and `0x00173E40`.
