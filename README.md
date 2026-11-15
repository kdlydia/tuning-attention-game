# tuning-attention-game

A game built on [MayaFlux](https://github.com/MayaFlux/MayaFlux):

- **MayaFlux Nexus** holds the world: entities, trigger regions and the camera.
- **[Jolt Physics](https://github.com/jrouwe/JoltPhysics)** will do the rigid bodies.
- **MayaFlux Forma** draws the HUD and the menus.
- **Sound** comes from MayaFlux nodes and the entities' audio outputs.

Status: an empty MayaFlux project. Nothing game-specific is built yet.

## Build

You need the MayaFlux nightly installed. On Arch that's the AUR package `mayaflux-dev-bin`; elsewhere, set `MAYAFLUX_ROOT`.

```sh
cmake --preset release
cmake --build --preset release --parallel
./build/TuningAttentionGame
```

`./validate.sh` checks the setup. `./run-report.sh` runs the game and saves its console output to `mayaflux_run.txt`.

## Layout

| Path | What |
|---|---|
| `src/user_project.hpp` | `settings()` and `compose()`, where the game is built |
| `src/main.cpp` | Weave launcher, unchanged |
| `cmake/` | MayaFlux discovery, shaders, community modules (Weave) |
| `docs/DESIGN.md` | How MayaFlux and Jolt fit together for this game, read from the MayaFlux source |
| `docs/upstream/` | Feature requests and a discussion drafted for MayaFlux. Not filed yet. |

## First steps

One step at a time, each ending in something that runs:

1. **Add Jolt** v5.7.0 through FetchContent, with the CMake settings in `docs/DESIGN.md` section 5.
2. **A floor and a few boxes.** Each box is a Nexus Agent whose pose comes from its Jolt body. Its mesh is a `Kinesis::generate_box`, attached with `OnMove::Carry`. Jolt steps in a graphics pre-process hook.
3. **A walking camera.** A Nexus `Locus` whose `NavigationState::eye_constraint` runs a Jolt `CharacterVirtual`.
4. **Sound from contacts.** Contacts are queued from Jolt's threads and fire entities on the graphics thread.
5. **A HUD** in Forma.

## Working rules

- Keep `data/shaders/` empty unless a shader is ours. Copied library shaders override the installed ones and break Forma text.
- Anything sent to MayaFlux itself follows its `CONTRIBUTING.md`: a person writes, reviews and submits it.
