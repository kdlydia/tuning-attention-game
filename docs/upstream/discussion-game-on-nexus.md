I'm starting a game on MayaFlux, and I'd like to check the design with you before building much of it.

**The idea:**
- Nexus is the high-level entry point: entities, trigger regions and the camera.
- Jolt Physics does the rigid bodies.
- Forma draws the HUD and the menus.
- Sound comes from nodes and the entities' audio sinks.

I read the Nexus, Kinesis (spatial, viewport, geometry), Forma, input and model IO sources on develop `2084dfae` first. Everything below is the plan, not running code yet.

## How I plan to join Jolt and Nexus

- **Stepping.** Jolt steps in a graphics `PRE_PROCESS` hook, registered through `SubsystemManager::register_process_hook`. That runs before the frame's coroutines and buffers, on the graphics thread (`GraphicsSubsystem::process`).
- **Contacts.** Jolt calls its contact callbacks on its own worker threads. They go into a queue, which the hook drains after the step, calling `Fabric::fire(id)` for the entities involved.
- **Bodies.**
  - A body is an `Agent` subclass that takes its pose from Jolt in `invoke_perception`, the way `Locus` takes its own from its `NavigationState`.
  - It's wired with `every(1.0 / 60.0, FRAME_ACCURATE)` and attached to its mesh with `OnMove::Carry`, so `apply_attachment` places the mesh in the same frame.
  - Commits run on a metro only where neighbours or Expanses are used, as in the Getting Started example.
- **Walking.** A `Locus` with `NavigationState::eye_constraint` running a Jolt `CharacterVirtual`.
- **Aiming.** `Kinesis::screen_to_ray` gives the ray, and Jolt's `CastRay` finds the exact hit.
- **Models.** `ModelReader::create_mesh_network`, with one Jolt body per slot driving that slot's transform. `MeshFieldOperator` adds deformation.
- **Fields.** One `MF_FIELD` field pushes bodies through its CPU half (`AddForce`) and moves particles through `GpuFieldOperator`.

## Questions

1. Is a graphics pre-process hook the right place for a fixed-step simulation? Or would you rather it ran as a `FRAME_ACCURATE` routine, and if so, is there an ordering guarantee among `FRAME_ACCURATE` routines?
2. Is subclassing `Agent` and overriding `invoke_perception`, as `Locus` does, the extension point you intend for entities driven by something external?
3. Forma and game input both receive every window event. Is there a preferred way for the UI to take a click so the game doesn't also act on it? For now I check `Context::hovered()`.
4. Would a seam for external rigid-body poses belong in Nexus, for example a Body principal or a Pheme target? Or should it stay in projects and community modules?
5. Are bone and skin import, and a skinning path, something MayaFlux plans to have?

## Feature requests filed for this

- (Wayland cursor modes: link)
- (ModelReader node tree into MeshNetwork: link)
- (Forma set_focus: link)
- (HID binding by usage and a gamepad profile: link)
