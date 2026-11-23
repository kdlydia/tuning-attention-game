# tuning-attention-game

A maze game built on [MayaFlux](https://github.com/MayaFlux/MayaFlux) and navigated with a drawing tablet. Moving through the maze, and the sound, change the world: the music, the light, which parts open. The world reacts back.

- **MayaFlux Nexus** holds the world: the camera, rooms as trigger regions, and entities that react.
- **The tablet** moves the player through MayaFlux's TabletBackend: pen tilt, pressure, proximity and position.
- **External sensors** arrive as OSC.
- **MayaFlux Forma** draws the HUD.
- **Sound** comes from MayaFlux nodes and the entities' audio outputs.
- **[Jolt Physics](https://github.com/jrouwe/JoltPhysics)** only if there turn out to be objects to push. A grid maze doesn't need it.

Status: an empty MayaFlux project. Nothing game-specific is built yet.

## Build

You need the MayaFlux nightly installed. On Arch that's the AUR package `mayaflux-dev-bin`; elsewhere, set `MAYAFLUX_ROOT`.

```sh
cmake --preset release
cmake --build --preset release --parallel
./build/TuningAttentionGame
```

`./validate.sh` checks the setup. `./run-report.sh` runs the game and saves its console output to `mayaflux_run.txt`.

## Tablet access

The game reads the tablet directly through HID, so your user needs read access to `/dev/hidraw*`. On Linux, current MayaFlux nightlies install a udev rule for common tablets (Wacom, XP-Pen, Huion, UC-Logic, VEIKK, Genius, Hanvon) at `/usr/lib/udev/rules.d/70-mayaflux-hid.rules`. Load it and replug the tablet:

```sh
sudo udevadm control --reload && sudo udevadm trigger
```

If that file is missing, or your tablet isn't in it, add a rule with the tablet's vendor id from `lsusb`. When MayaFlux can't open a device, its log prints the exact line to use.

```sh
echo 'KERNEL=="hidraw*", ATTRS{idVendor}=="xxxx", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/70-mayaflux-local.rules
sudo udevadm control --reload && sudo udevadm trigger
```

On macOS the app needs the Input Monitoring permission instead. The backend is turned on in `settings()` with `GlobalInputConfig::with_tablet()`.

## Layout

| Path | What |
|---|---|
| `src/user_project.hpp` | `settings()` and `compose()`, where the game is built |
| `src/main.cpp` | Weave launcher, unchanged |
| `cmake/` | MayaFlux discovery, shaders, community modules (Weave) |
| `docs/DESIGN.md` | How MayaFlux fits this game, read from the MayaFlux source |
| `docs/upstream/` | Feature requests (MayaFlux #188–#191) and a discussion (#192) filed for this game |

## First steps

One step at a time, each ending in something that runs:

1. **A grid maze.** The walls are one box mesh drawn through an `InstanceNetwork`. A Nexus `Locus` is the camera, and its motion is checked against the grid.
2. **Tablet navigation.** One `TabletNode` per axis, read each frame. Try three mappings and keep what feels right:
   - tilt as a joystick, with pressure as speed
   - the tablet as the map
   - drag from where the pen touched down
3. **Rooms as Nexus Expanses.** Entering or leaving one changes the music and the light.
4. **Sensor input over OSC.**
5. **A HUD** in Forma.

## Working rules

- Keep `data/shaders/` empty unless a shader is ours. Copied library shaders override the installed ones and break Forma text.
- Anything sent to MayaFlux itself follows its `CONTRIBUTING.md`: a person writes, reviews and submits it.
