*Updated 15 Nov: the game is navigated with a drawing tablet through TabletBackend, not a gamepad or mouse look, and a grid maze doesn't need Jolt. #191 is no longer needed for it.*

I'm starting a game on MayaFlux, and I'd like to check the design with you before building much of it.

**The idea:** a maze navigated with a drawing tablet. Moving through it, and the sound, change the world: the music, the light, which parts open. The world reacts back.
- Nexus is the high-level entry point: the camera, rooms as Expanses, and entities that react.
- Forma draws the HUD.
- Sound comes from nodes and the entities' audio sinks.
- External sensors arrive as OSC.
- Jolt Physics comes in only if there turn out to be objects to push.

I read these sources on develop `2084dfae` first: Nexus, Kinesis, Forma, input (TabletBackend, TabletNode, InputNode) and model IO. Everything below is the plan, not running code yet.

## How I plan to build it

- **Navigation:**
  - One `TabletNode` per axis (pressure, tilt, proximity, and position if the tablet serves as the map), bound with `InputBinding::tablet`.
  - A graphics `PRE_PROCESS` hook reads their `get_target_value()` each frame and moves a `Locus` by writing its `NavigationState` eye, yaw and pitch, with collision against the maze grid.
  - Proximity and contact edges mark when the pen arrives and leaves.
- **Rooms** are Expanses. Entering or leaving one changes the music and the light. A commit metro evaluates them, as in the Getting Started example.
- **Walls** are one box mesh drawn through an `InstanceNetwork`.
- **Entities driven from outside** (by sensors, or by Jolt bodies if they come) are `Agent` subclasses that take their pose in `invoke_perception`, the way `Locus` does, wired with `every(1.0 / 60.0, FRAME_ACCURATE)`.
- **Fields:** one `MF_FIELD` field drives particles through `GpuFieldOperator`, and, through its CPU half, anything else that needs the same force.

## Questions

1. Is a graphics pre-process hook the right place for the per-frame game step? Or would you rather it ran as a `FRAME_ACCURATE` routine, and if so, is there an ordering guarantee among those?
2. Is subclassing `Agent` and overriding `invoke_perception`, as `Locus` does, the extension point you intend for entities driven from outside?
3. TabletBackend reads the pen through hidraw, but the Wayland backend has no tablet protocol. So I expect the compositor to emulate a pointer from the same pen, and the game window to get mouse events while the pen moves. Is there a preferred way to tell the two apart, or would you rather the Wayland backend took `zwp_tablet_v2` events? For now I plan to ignore the mouse while the pen is in proximity.
4. Forma and game input both receive every window event. Is there a preferred way for the UI to take a click, so the game doesn't also act on it? For now I check `Context::hovered()`.

## Feature requests filed for this

- Wayland cursor modes (pointer lock and relative motion), for a keyboard-and-mouse fallback: #188
- ModelReader node tree into MeshNetwork slots: #189
- Forma `set_focus`, for keyboard-driven menus: #190
- HID binding by usage, with a standard gamepad profile: #191 (no longer needed for this game)
