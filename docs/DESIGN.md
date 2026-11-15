# A game on MayaFlux: Nexus as the entry point, Jolt for physics

Read on 15 Nov 2026 against develop `2084dfae`. Every header cited below is byte-identical in the installed nightly (main `38e09827`), so all of it can be used today. Read, not run: nothing was built or tested. Paths are relative to `src/MayaFlux/` unless they say otherwise.

Study notes for this game, written while planning it. Section 1 is the summary. Sections 2 to 8 cover Nexus, the frame order, where Jolt plugs in, the build, input, the UI and assets. Section 9 says what was read in full and what was only skimmed.

## 1. Short answer

It's feasible, and Nexus fits the job well.

Nexus is already a small scene and entity layer. It has:
- entities with a position and an orientation
- a spatial index
- trigger regions
- an audio output and a render output per entity
- a camera entity
- firing on key, mouse, metro or network events
- save and load of the whole scene

What Nexus doesn't have is exactly what Jolt provides: collision shapes, rigid bodies, contacts, joints, a character controller and exact ray casts.

The places to join the two already exist, in Pheme (`Survey`, `Attachment`, `FieldTargets`) and in Kinesis (`AABB3D`, `screen_to_ray`, `NavigationState::eye_constraint`, the mesh generators). The graphics thread's frame order lets one task step physics, commit Nexus and draw the result in the same frame, all on one thread.

**Gaps for a real game, most important first:**
1. **Mouse lock on Linux.** It works only on macOS; on Wayland it would have to be added (section 6).
2. **Gamepads.** They arrive as raw HID bytes, with no standard button and axis mapping (section 6).
3. **Model scene graphs.** `ModelReader::create_mesh_network` gives a MeshNetwork, which can hold a parent tree and a transform per slot. But ModelReader adds every mesh as a root with an identity transform, so the file's placement has to be filled in, by hand or by a small upstream change (section 8).
4. **Skinning.** No bones or skinning are imported. Rigid, jointed and field-deformed parts all work through MeshNetwork's operators (section 8).
5. **Not checked:** lighting, shadows and materials; 3D audio beyond stereo panning; Lila live coding with Jolt loaded.

**What I'd expect each kind of project to take:**
- **Physics sandbox or interactive environment with sound:** possible now. I'd estimate 300–500 lines of project code to join the two.
- **Small complete 3D game** (walking camera, triggers, HUD, sound): possible. It also needs mouse look on Wayland (or a right-drag look until then), gamepad report layouts and a contact queue.
- **Skinned, animated characters:** a large addition (bone import plus a skinning shader). Not a small step.

## 2. What Nexus is (read from the source)

- **The hierarchy.** `Tapestry` owns one or more `Fabric`s and named `Expanse`s (`Tapestry.hpp`). A Fabric holds the entities: `Emitter`, `Sensor` and `Agent`. `Presence` and `Locus` are Agent subclasses, and Agent's `invoke_perception` and `invoke_influence` are virtual (`Principals/Agent.hpp`). So a physics body can be its own Agent subclass.
- **Fabric** is a plain object, not a subsystem (`Fabric.hpp`).
- **Entities fire from their Wiring events:** a metro, a key, a mouse button, scroll, a network source, a window event, a `move_to` chain, or a coroutine of your own. Each calls `Fabric::fire(id)` against the current snapshot (`Wiring.cpp`), so firing needs no `commit()`.
- **`commit()` does the spatial work** (`Fabric.cpp:268-322`):
  1. Writes every entity position into the `Kinesis::SpatialIndex3D` and publishes it. This is the only publish in Nexus (`Fabric.cpp:285`).
  2. Evaluates every Expanse against that snapshot.
  3. Fires the entities that have no wiring.
- **Who reads the published snapshot:** perception (Sensor, Agent and Presence neighbours), `within_radius`, `k_nearest` and `Kinesis::ray_cast`. So a game that uses neighbours or Expanses needs a commit metro. MayaFlux's own tutorial wires its entities with `every`/`on` and still runs `schedule_metro(1.0 / 24.0, [&fabric] { fabric.commit(); }, ...)` for its Expanse and Presences (`docs/Getting_Started.md:391`). A game that only places meshes from Jolt needs no commit.
- **Wiring** decides when an entity fires (`Wiring.hpp`, `Wiring.cpp`):
  - `every(interval, token)`, a metro
  - `on(window, key, held, on_release)`, `on(window, mouse button ...)`, `on_scroll`, `on(NetworkSource)`, `on(WindowEventSource)`
  - `move_to` steps, `position_from`, `times`, `for_duration`
  - `use(...)` with your own coroutine, and `bind(...)`, which calls once, immediately
  - Without any of these, the entity fires on every commit.
  - It warns if an entity that renders or navigates runs on the audio token (`Wiring.cpp:484-503`).
- **Agent** has a position, an optional orientation (a quaternion, local +Z forward) and a query radius. When fired (`Fabric.cpp:389-456`, perception at 429, influence context from 432):
  1. Perception: neighbours within the radius, from the snapshot.
  2. Influence: the influence function, then the audio sinks, render sinks, the attachment and any typed influences.
- **Pheme** joins entities to buffers:
  - **`Survey`** (`Pheme/Survey.hpp`): reads what Jolt needs from any buffer.
    - `read_bounds(buf)` gives the `AABB3D` of a mesh, point cloud, path, volume grid, network or instance buffer.
    - `read_anchor(buf, index)` gives the centroid or one vertex.
    - `read_placement(buf)` gives the render transform the buffer is drawn with.
  - **`Attachment`** (`Pheme/Attachment.*`): moves an existing buffer with the entity.
    - `apply_attachment` builds `pivot_transform(anchor, position - rotation * offset, rotation)` and installs it as a stacked geometry transform on every render processor of the buffer.
    - Nothing is written into the buffer. Release restores the old transform.
    - `OnMove::Carry` or `Offset` decides what an outside move does.
  - **`Sinks`** (`Pheme/Sinks.*`):
    - An audio sink is an `AudioBuffer` on an output channel (`AUDIO_BACKEND`).
    - A render sink is a point, line or triangle buffer with a depth test.
  - **`FieldTargets`** (`Pheme/FieldTargets.hpp`):
    - `bind_force` feeds a `PhysicsOperator`.
    - `bind_position` and `bind_transform` drive one instance or mesh slot.
    - `bind_bounds` moves an Expanse.
    - `bind_params`, `bind_push_constants` and `bind_influence_block` reach shaders.
- **Expanse** (`Expanse.*`) is a region: a box, a predicate, or both.
  - Each commit tests every entity position, and the membership difference fires enter, exit and inside actions.
  - `Hull` takes its box from a buffer's bounds and follows the buffer's render transform exactly, rotation and scale included (`Expanses/Hull.cpp`: the point goes through the inverse placement).
  - `Mantle` is the reverse: the box places the buffer, with fit modes.
- **Locus** is a camera Agent built on `Kinesis::NavigationState` (`Principals/Locus.*`).
  - Perception moves the eye and builds the `ViewTransform`. Influence pushes the view to its render processors.
  - `set_timestep(dt)` makes the path repeatable.
- **State** saves and loads Fabrics and the Tapestry as JSON plus EXR, schema version 7 (`State/Schema.hpp`).
  - Saved: entities, wiring records and Expanse boxes.
  - Not saved: live objects (buffers, attachments, typed influences). They must be made again; named functions come back through Fabric's function registry.
- **Everything is points.** The index holds one point per entity and answers radius and k-nearest queries. `SpatialIndex::publish` rebuilds a hash grid each commit (`Kinesis/Spatial/SpatialIndex.cpp:165-206`). `Kinesis::ray_cast` measures each point's distance from the ray, over the whole snapshot (`Kinesis/Spatial/HitTest.hpp`). There are no shapes, no overlap of extents and no contacts.
- **MayaFlux's own physics** (`Nodes/Network/Operators/PhysicsOperator.hpp`) is particles: velocity, force and mass per point, gravity, drag, a bounds box, repulsion springs, force fields and bonds. It has no rigid bodies, no rotation or inertia, and no contact solving.

The official `docs/Digital_Architecture.md` section "Nexus: Spatial Entity Simulation" agrees with all of this.

## 3. The frame: who runs what (traced)

The graphics thread loop is `Core/Subsystems/GraphicsSubsystem.cpp:377-400`. `process()` (lines 283-314) runs, every frame, in this order:
1. Pre-process hooks.
2. FRAME_ACCURATE coroutines (`tasks.process`).
3. Visual nodes.
4. Graphics buffers. This is where `RenderProcessor::execute_shader` calls `publish_render_transform()`, which calls the geometry and view transform sources (`Buffers/Shaders/RenderProcessor.cpp:606-616, 877`).
5. Render the windows.
6. Window event processing. Forma callbacks run here, per `Portal/Forma/Context.hpp`.
7. Post-process hooks.

So the game splits like this:

1. **The physics step goes in a graphics pre-process hook.** That runs before every FRAME_ACCURATE coroutine in the frame (`GraphicsSubsystem.cpp:289-293`):
   `get_context().get_subsystem_manager()->register_process_hook(Core::SubsystemType::GRAPHICS, "physics", fn, Core::HookPosition::PRE_PROCESS)` (`Core/SubsystemManager.hpp:281`, `Core/Engine.hpp:360`). In `fn`:
   1. Read input: `WindowEventSource::is_key_pressed`, `InputNode::get_target_value()`, or flags that input hooks set.
   2. `PhysicsSystem::Update(dt, ...)` with a fixed dt and an accumulator. Frames can be late (the FrameClock reports lag), so dt shouldn't follow the frame.
   3. Drain the contact queue and `fabric.fire(id)` for the entities a contact concerns.
2. **Each body entity is wired.** `fabric.wire(body).every(1.0 / 60.0, Vruta::ProcessingToken::FRAME_ACCURATE).finalise()` fires it on the graphics clock, after the hook.
   - The body takes its pose from Jolt in its own `invoke_perception` override, as Locus does with its navigation state. `Fabric::fire` builds the influence context from the position and orientation after perception has run (`Fabric.cpp:389-456`, perception at 429, influence context from 432), so the attachment places the mesh at this frame's pose.
3. **A commit metro is needed only for neighbours or Expanses** (section 2).

The same frame's buffer pass then draws the result. Two things leave the graphics thread: Jolt's worker threads inside `Update`, which finish before it returns, and Jolt's contact callbacks, which go through a queue.

Audio stays on the audio callback thread. Hand values across the way the existing projects do: atomics, nodes, `SnapshotCell`. The community guide's rule applies: no mutex on the audio thread. Fabric itself doesn't lock, so keep every Nexus call on that one FRAME_ACCURATE path. The community guide already lists Nexus commits under the graphics thread.

## 4. Where Jolt plugs in

| Need | Jolt | MayaFlux seam |
|---|---|---|
| A body moves its mesh | `BodyInterface::GetPositionAndRotation` | An Agent subclass holding a `JPH::BodyID` sets its own position and orientation from Jolt in `invoke_perception`, and is wired with `every(..., FRAME_ACCURATE)`. `Agent::attach(buf, { .on_move = AttachConfig::OnMove::Carry })` then places the mesh through `apply_attachment`. Carry matters: `follow_attachment` runs before perception and treats Jolt's move as an outside move, and `Offset` would fold that move into the offset (`Pheme/Attachment.cpp:107-127`). Or set the render processor's `set_geometry_transform_source` directly. |
| A multi-part model as bodies | same, one body per part | `ModelReader::create_mesh_network` gives one `MeshSlot` per mesh. Set each root slot's `local_transform` from its body and mark it `dirty`, or feed it with `Nexus::bind_transform(op, slot)` on a `MeshTransformOperator` (section 8). Child slots compose `world = parent_world * local`, so a body's world pose goes in as `inverse(parent_world) * pose`. |
| Many bodies, one draw | same | `InstanceNetwork`: set `get_slot(i).transform` and `dirty` per slot for one instanced draw (`Nodes/Network/InstanceNetwork.hpp:9-30`, `GeometrySlot.hpp`). Or `Nexus::bind_transform` on an `InstanceFieldOperator`. |
| A box shape | `BoxShape`, offset with `RotatedTranslatedShape` | `read_bounds(buf)`: half extent and centre. `Kinesis::generate_box` gives the matching mesh. |
| A convex or static mesh shape | `ConvexHullShapeSettings`, `MeshShapeSettings` | `Kakshya::MeshData` is interleaved vertex bytes, uint32 triangle indices and a `VertexLayout` (`Kakshya/NDData/MeshData.hpp`). Read positions with `Kinesis::position_at(bytes, stride, offset, i)` (`Kinesis/Morphology.hpp`). The sources are `IO::ModelReader::load`, `generate_box`, `generate_grid`, `generate_tube`, `generate_revolution` and `generate_sdf_mesh` (marching cubes). Survey's own position decoder is internal, so this is a short loop of your own. |
| Terrain | `HeightFieldShape` (a square grid) | `Kinesis::generate_explicit_surface(heights, columns, rows, ...)` draws the same height table. Use columns == rows. |
| Triggers | sensor bodies (`SetIsSensor`) | Expanse and Hull test entity points: cheap for tens to hundreds. Use Jolt sensors when a whole body's extent must count. Both can run side by side: Jolt for physical zones, Expanse for musical ones. |
| Contacts make sound or gameplay | `ContactListener`, called on Jolt's job threads | Push to a lock-free queue (`Transitive/Memory`, per the community guide; not re-read this session). Drain it after `Update`, then `Fabric::fire(id)`, or drive an Emitter's audio sink or a node with the impulse. |
| Walking camera | `CharacterVirtual` | `NavigationState::eye_constraint` takes the proposed eye and returns the actual one (`Kinesis/Viewport/NavigationState.hpp`; applied in `advance_navigation`, `NavigationState.cpp`). Turn the step into a velocity, run `ExtendedUpdate`, and return the character position plus eye height. Locus copies the eye into its position and builds the view. `advance_navigation` flies along the pitched forward, so keep only the horizontal part for walking. |
| Picking and aiming | `NarrowPhaseQuery::CastRay` | `Kinesis::screen_to_ray(x, y, w, h, view)` builds the ray (`Kinesis/Spatial/HitTest.hpp`). Store the Nexus entity id as the body's user data to map hits back. |
| Fields push bodies | `AddForce` | Write the field once with `MF_FIELD`. Its `.cpu` half is an ordinary callable: sample it at each body in the physics hook and `AddForce` the result. Its GLSL half drives particles through `GpuFieldOperator`, or meshes through `MeshFieldOperator`. So wind or a vortex moves bodies and particles alike. |
| Debug drawing | `JPH::DebugRenderer` (on in Debug and Release builds by default) | Write its lines into a Nexus render sink with `LINE_LIST`, or into a `LineSegmentsNode`. |
| Save and replay | `PhysicsSystem::SaveState` / `RestoreState` | Nexus `StateEncoder` for the entities. `IO::SpatialCache` (Alembic) records body positions per frame as a points stream with ids and velocities, plus orientation as a vec4 attribute, for replay or export to Houdini and Blender. |

**Conventions:**
- Both are right-handed with Y up. Jolt's default gravity is (0, -9.81, 0). NavigationState's lookAt uses up = (0, 1, 0). Units are metres.
- Quaternions: `glm::quat(w, x, y, z)` against `JPH::Quat(x, y, z, w)`.
- `JPH::Mat44` is column-major, as glm is.
- The anchor: Attachment rotates about the mesh centroid, unless `AttachConfig::index` or `offset` says otherwise. Jolt puts a convex hull's origin at its centre of mass. Match the two with `offset`, or use `GetCenterOfMassPosition`.

## 5. Linking Jolt into a project

**What MayaFlux expects of a project:**
- It's a Weave CMake project: C++23, and `include(cmake/mayaflux)` links `MayaFlux::MayaFluxLib` and the runtime PCH (this repo's `CMakeLists.txt` and `cmake/mayaflux.cmake`).
- The installed config gives C++ `-mavx -mavx2 -mfma` (`/usr/lib/cmake/MayaFlux/MayaFluxConfig.cmake:336`).
- RTTI is on: MayaFlux uses `dynamic_pointer_cast` throughout, Nexus included.
- MayaFlux's `third_party` holds dlpack, magic_enum, tinyexr and tinyvdb. No physics library, so nothing clashes.

**Jolt:** the current release is v5.7.0, out 14 Nov. Breaking in 5.7: `mDeterministicSimulation` is gone (simulation is always deterministic now), and so is `JPH_USE_STD_VECTOR`. Settings for this setup:
- **`CPP_RTTI_ENABLED ON`** (the default is OFF). Deriving `ContactListener` and the layer interfaces in RTTI code needs Jolt's typeinfo. This is my reading of how GCC links; check it on the first build.
- **`INTERPROCEDURAL_OPTIMIZATION OFF`.** Jolt's build notes that otherwise the consumer must use LTO too, or linking can fail.
- **`USE_AVX2` and `USE_FMADD` stay ON** (the defaults), matching MayaFlux's `-mavx2 -mfma`. On aarch64 (the phone) Jolt uses NEON.
- **`JPH_USE_VK OFF`.** Jolt's Vulkan compute path isn't needed here and would look for Vulkan and glslc.
- **The `JPH_*` defines match automatically.** Jolt's public defines (precision, SIMD level, debug renderer, object stream) reach the project through `target_link_libraries`, as long as Jolt builds in the same CMake tree.

```cmake
# after include(cmake/mayaflux)
include(FetchContent)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics
    GIT_TAG v5.7.0
    SOURCE_SUBDIR Build
    SYSTEM)          # keeps -Wpedantic quiet on Jolt headers
FetchContent_MakeAvailable(JoltPhysics)
target_link_libraries(${PROJECT_NAME} PRIVATE Jolt)
```

Keep the glue in the project first. As a community module later, it must follow the module rules (upstream review of community modules): no `API/` includes, take `BufferManager&` and `TaskScheduler&` as arguments, and no mutex on MayaFlux's side. Whether Weave accepts FetchContent inside a module's `.cmake` is not checked. Jolt is built once per build folder; MayaFlux itself is used as installed, never built here.

**Sketch** (types from the headers above; Jolt calls from its API; not compiled):

```cpp
class Body : public Nexus::Agent {           // Agent's invoke_* are virtual
public:
    Body(JPH::BodyInterface& bi, JPH::BodyID id) : Agent(1.0F), m_bi(bi), m_id(id) {}
    void invoke_perception(const Nexus::PerceptionContext& ctx) override
    {
        JPH::RVec3 p; JPH::Quat q;            // Jolt to Nexus, as Locus does with its nav state
        m_bi.GetPositionAndRotation(m_id, p, q);
        set_position({ p.GetX(), p.GetY(), p.GetZ() });
        set_orientation(glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()));
        Agent::invoke_perception(ctx);
    }
private:
    JPH::BodyInterface& m_bi;
    JPH::BodyID m_id;
};

// compose():
get_context().get_subsystem_manager()->register_process_hook(
    Core::SubsystemType::GRAPHICS, "physics", [&](unsigned int) {
        world.step(1.0F / 60.0F);             // PhysicsSystem::Update, fixed dt
        world.contacts.drain([&](const Hit& h) { fabric.fire(h.entity); });
    }, Core::HookPosition::PRE_PROCESS);

body->attach(mesh_buffer, { .on_move = Nexus::AttachConfig::OnMove::Carry });
fabric.wire(body).every(1.0 / 60.0, Vruta::ProcessingToken::FRAME_ACCURATE).finalise();
// only if neighbours or Expanses are used:
schedule_metro(1.0 / 60.0, [&] { fabric.commit(); }, "commit", Vruta::ProcessingToken::FRAME_ACCURATE);
```

`schedule_metro`'s signature is in `API/Chronie.hpp:63`.

## 6. Input for a game (traced)

- **Keyboard and mouse** come from the window backends: Wayland, Win32 and Cocoa.
  - The events are key press, release and repeat; text; mouse motion, buttons, scroll, enter and exit; resize; and focus (`Core/GlobalGraphicsInfo.hpp:480-507`).
  - They reach code through Kriya events (`key_pressed`, `key_held`, `mouse_dragged`), Nexus `Wiring::on(...)` and Forma's `Context`.
  - There are no touch events.
- **Polling.** Each window's `WindowEventSource` keeps key and button state and the mouse position: `is_key_pressed`, `is_mouse_pressed`, `get_mouse_position` (`Vruta/WindowEventSource.hpp:124-135`). `signal()` updates it on the graphics thread, so a FRAME_ACCURATE game step can poll WASD with no event wiring.
- **Every listener gets every event.** `signal()` dispatches each event to all matching awaiters. Nothing marks an event as handled.
- **Mouse lock.** `CursorMode` has `DISABLED` and `CAPTURED` for an FPS camera (`Core/GlobalGraphicsInfo.hpp:446-465`), but only `Cocoa/CocoaWindow.mm` reads it. The Wayland and Win32 backends don't. Mouse look on Linux needs the relative-pointer and pointer-constraints protocols added to `WaylandWindow`, which is an upstream request. Until then, use right-drag look (`NavigationState::rmb_held`).
- **Gamepads and other HID devices** (`HIDNode` and `InputNode`, `.hpp` and `.cpp` read in full).
  - **The path a report takes:**
    1. `HIDBackend` reads with hidapi on its own poll thread (`hid_read_timeout`, 10 ms by default) (`Core/Backends/Input/HIDBackend.hpp:43-74`, `.cpp:66, 396-408`).
    2. Each report becomes an `InputValue` of raw bytes (`HIDBackend.cpp:453-463`) and is queued.
    3. `InputManager` has its own processing thread (`Core/Input/InputManager.cpp:67, 319-381`). It calls `process_input` on every registered node whose `InputBinding` matches: HID, any or one device id, or vendor and product id. Register with `register_input_node(node, bind_hid(vid, pid))` (`API/Input.hpp:30, 98`).
    4. `GlobalInputConfig::with_gamepads()` filters for usage page 0x01, usage 0x05.
  - **One HIDNode per control** (`Nodes/Input/HIDNode.*`). A pad is a set of nodes on one binding, each reading the same report.
    - `HIDConfig::axis(offset, 1 or 2 bytes, signed)`: 16-bit values are little-endian. The value is normalised to [0, 1] through `with_range(min_raw, max_raw)`, so a stick rests at 0.5; use `2v - 1` for movement.
    - `HIDConfig::button(offset, mask, invert)` gives 0 or 1, with smoothing forced off.
    - `HIDConfig::custom(fn)` takes the whole report, for hat switches, packed 12-bit axes, or anything else.
    - Offsets index the bytes hidapi returns, which start with the report id on devices that use numbered reports.
  - **Defaults to change for game axes:**
    - The deadzone (0.05) is applied around 0.5, so it suits a centred stick. On a trigger (rest 0) it puts a dead band in the middle of the travel; use `with_deadzone(0)`.
    - The smoothing is exponential, factor 0.1, stepped once per report (`InputNode.cpp`, `process_input`). That lags a stick, and settles only while reports keep arriving. Use `set_smoothing(SmoothingMode::NONE)` and smooth in the game if wanted.
  - **The events** (`on_tick`/`on_input`, `on_value_change(eps)`, `on_threshold_rising/falling(t)`, `on_range_enter/exit(min, max)`, `while_in_range`, `on_button_press/release`, `on_tick_if(cond)`):
    - They are checked in `notify_tick` against the smoothed value and the previous one. Press and release are crossings of 0.5.
    - They run on the InputManager thread, not the graphics thread (`InputNode.hpp`, `notify_tick` doc). So a hook mustn't call `Fabric::fire` or write a Forma `MappedState`. Have it set an atomic or push to a queue that the physics hook drains.
    - Or skip hooks: poll `get_target_value()` (raw, atomic), `get_current_value()` (smoothed) or `has_new_input()` once per frame, and find the edges in the game.
  - **One thing to test before relying on it:** `RANGE_ENTER` and `RANGE_EXIT` share one `m_was_in_range` flag, and every range entry overwrites it during one `notify_tick`. As written, an enter and an exit hook on the same node look like they'd keep the second one from firing. Read, not run.
  - **No standard controller mapping**, so each pad's report layout has to be written down once. hidraw permissions may need a udev rule (not checked).
  - **Rumble and LEDs:** `HIDBackend::write` (output reports) and `send_feature_report` exist (`HIDBackend.hpp:141-160`). How a project reaches the backend wasn't checked.
- **Other controllers:** MIDI (PipeWire), OSC, serial and tablet (pressure, tilt, rotation). Unusual controllers are a MayaFlux strength.
- **Webcam as a controller.**
  - `FFmpegCameraReader` (v4l2 on Linux) decodes on its own thread into a `CameraContainer` as RGBA, one frame per request (`IO/Camera/CameraSource.hpp`, `FFmpegCameraReader.hpp`). `choose_camera` in `API/Depot.hpp` opens it.
  - `ImageCVProcessor` runs a `Kinesis::Vision` sequence on a GPU image buffer. It downloads the image, stores the result, signals a broadcast source, and can be throttled (`Buffers/Textures/ImageCVProcessor.hpp`).
  - Motion or body tracking as game input is plausible. Vision itself wasn't read in depth.

## 7. Game UI with Forma

Paths in this section are relative to `Portal/Forma/` unless they say otherwise.

### How Forma works

- **Retained, not immediate.** An element is a value (`MappedState<T>`: the value plus a version counter) and a geometry function that writes vertex bytes from the value. `Mapped::sync()` regenerates the geometry only when the version has moved on (`Primitives/Mapped.hpp`).
- **Widgets are plain functions.** Each returns a `Form<T>`: geometry, topology, capacity and the input wiring. Your own count exactly as the shipped ones do; there's no registry (`Primitives/Geometry.hpp`).
- **One batch per surface.** `create(surface, form, initial)` draws each element as a slice of the surface's shared buffer, so a HUD is one batch (`Tend.hpp`, `Forma.hpp`).
- **`Tend{...}.place(surface).bridge().write(...)`** is the chained form.

### What fits a game

- **HUD bars and gauges.**
  - Shapes: `level_meter` (a filled bar with no input), `radial`, `crosshair`, `point` (`Primitives/FormFactory.hpp`).
  - Feeding them: write from the game step with `state->write(hp)`. The step runs on the graphics thread, and `Mapped.hpp` says that's the only thread that may write a MappedState.
  - Or pull with `bridge().bind(state, [&] { return speed; })`, once per frame.
- **Text.** `Element::with_text` and `set_text` draw from a FreeType glyph atlas (`Portal/Text/InkPress.hpp`). `PressParams` gives colour, colour per byte range (`StyleSpan`), a background quad and wrapping inside `render_bounds`. Good for scores, timers and dialogue lines.
- **Buttons.** One element: text plus a background, repainted from `on_press`, `on_release`, `on_enter` and `on_leave`. The installed guide `Portal-Forma-UI-Patterns.md` has the recipe under "ImGui::Button".
- **Settings screens:**
  - horizontal and vertical faders, `toggle`, `Dropdown`, `ColorPicker`
  - `TextField`: caret, click to place the caret, Up/Down keep the column
  - `Collapsible`, and `Scrollable` with an indicator
  - `Entry` rows that show live values as "label: reader()"
- **Icons and images.** `Element::with_texture(VKImage)` takes any image, but its buffer must have a texture slot, so it's made with `create_buffer`.
- **Menus as groups.** `Layer::set_visible`, `set_all_visible`, `bring_to_front` and `send_to_back` all cascade over related elements (`Layer.hpp`). So a pause menu shows or hides in one call, and hidden elements don't take clicks.
- **HUD over the 3D view.** Put a Surface on the game window. Draws follow `draw_priority`, and `FormaBuffer` has no depth test.
- **XML documents.** `instantiate_document` (`Manifest/Instantiate.hpp`) is in the nightly.
  - The library ships only the `polygon` and `vertices` factories.
  - Actions fire on press, enter and release, without values (read from the source on 14 Nov 2026).
  - I found no hot reload.
- **Debugging.** Inspector windows for nodes, buffers, the scheduler and events, and `Forma::plot` live plots, each in its own window (`Forma.hpp`). There's no Nexus or physics inspector.
- **Labels in the world.** Forma elements live in NDC, but `Portal::Text::glyph_vertices(quads, color, spans, GlyphMap, slot)` lays text out in any 2D space, "a world plane" included (`Portal/Text/GlyphMesh.hpp`). It produces MeshVertex quads with glyph-coverage weights. Drawn in a mesh buffer under the 3D view and placed by a geometry transform, such as a Nexus Agent's attachment, a label can follow an entity. It needs the atlas bound at a texture slot and the coverage shader. Plausible from the headers; not tried.

### What the game has to build

- **Keyboard and gamepad navigation.**
  - Focus is set only by a mouse press (`Context.cpp:510`), and there's no `set_focus`.
  - Key callbacks reach only the focused element.
  - So for menus, keep your own selected index, repaint on up/down, and call the action on confirm. Read keys from the window: Kriya key events, or poll `WindowEventSource::is_key_pressed` (section 6).
- **Click-through.**
  - Window events go to every listener (`Vruta/WindowEventSource.cpp`), and Forma can't mark one as handled.
  - So check `ctx.hovered()` (`Context.hpp`), or a "menu open" flag, before a game click acts.
- **Layout and resolution.**
  - Coordinates are NDC, so shapes stretch with the window's aspect ratio.
  - Layout tools: a vertical `LayoutCursor`, plus `Kinesis::place(anchor, side, w, h, gap, align)`. There's no flex or grid.
  - For HUD corners fixed in pixels, use `ctx.to_ndc_rect(x, y, w, h)` and recompute in `on_resize`.
  - Text size is the atlas pixel size (`Text::set_default_font(path, pixel_size)`), so each size needs its own atlas.
  - Wayland has no HiDPI support.
- **Animation and transitions.** None are built in. Drive the values per frame, from the game step or `Bridge::bind` with an easing function; geometry redraws on each version change.
- **Theming.** Colours are set per element at construction. There's no style sheet.
- **Text shaping.** FreeType only, and I found no HarfBuzz, so complex scripts aren't shaped. Fallback fonts work through `Text::add_fallback_font`.
- **Which thread may write.** The guide's ImGui table says "writes from any thread", but `Mapped.hpp` says MappedState isn't atomic and must be written on the graphics thread; from anywhere else, go through a Bridge inbound callable. The header wins.

### Verdict for game UI

- **Good fit:** HUDs, in-game readouts, debug overlays and settings screens. The bindings from UI to nodes, shaders and audio go further than a typical game UI library's.
- **Works with a mouse out of the box:** title and pause menus.
- **Game-side code:** keyboard and gamepad navigation, transitions, and layout that adapts to resolution. A small `MenuList` helper (a selection index plus repainting rows) would cover the menus; I'd guess a couple of hundred lines.
- **Compared with Dear ImGui and RmlUi:** Forma has less built-in widget and layout machinery than ImGui (debug tools) or RmlUi (HTML/CSS-style menus with animation and gamepad navigation). In exchange, any geometry can be a widget, and values wire straight to audio and shaders.

## 8. Assets

- **Assimp** (`IO/Model/ModelReader.*`) reads glTF, OBJ, FBX, PLY, STL, DAE and anything else Assimp supports.
  - It produces one `MeshData` per mesh (68-byte `MeshVertex`).
  - Its import flags are Triangulate, GenSmoothNormals, CalcTangentSpace, FlipUVs, JoinIdenticalVertices and SortByPType (`ModelReader.cpp:256-261`). It loads diffuse textures through a resolver.
  - It doesn't read the scene graph's node transforms or use PreTransformVertices. A model whose parts are placed by its scene graph loads with each part at its own origin (read, not tested).
  - Animations are only counted (`ModelInfo::animation_count`). No bones or skinning are imported.
- **MeshNetwork: the model as a scene** (`Nodes/Network/MeshNetwork.hpp`, `MeshSlot.hpp`). `create_mesh_network(resolver)` makes one named `MeshSlot` per mesh, with its own `MeshWriterNode` and diffuse texture.
  - **Slots can form a parent tree.** Each has a `local_transform`, and `world_transform = parent_world * local` is resolved parents first.
  - **ModelReader fills none of it.** It adds every slot as a root with an identity transform (`ModelReader.cpp`, `create_mesh_network`). So the file's placement goes in by hand (`find_slot(name)`, then set the parent, `local_transform` and `dirty`), or by a small upstream change that walks `aiNode` into the slots.
  - **`MeshTransformOperator`** (the primary operator) drives a slot's local transform from a field of time, `std::function<glm::mat4(float)>`, and propagates world transforms down the tree. That covers jointed parts and procedural animation, though not skinning. `Nexus::bind_transform(op, slot)` feeds it from an Agent's influence (`Nexus/Pheme/FieldTargets.hpp`).
  - **`MeshFieldOperator`** (in the operator chain) deforms each slot's vertices: POSITION, COLOR, NORMAL, TANGENT, UV and scalar fields, from `Kinesis` fields.
    - The CPU path is a per-slot `FieldOperator`. It takes an `MF_FIELD` field's `.cpu` half (`Kinesis/Tendency/DualField.hpp:73-84`).
    - The GPU path is `set_gpu_executor(ShaderExecutionContext)` with `push_constants`.
    - `Nexus::bind_field(op, slot, target)` feeds it from an influence.
  - **For a game:** each part can be a Jolt body (a crate breaking apart, a ragdoll's limbs, a vehicle's wheels), and its slot transform follows the body (section 4). Fields add dents, wobble or colour heat on top.
- **GpuFieldOperator** (`Nodes/Network/Operators/GpuFieldOperator.hpp`) is the GPU field path for point, particle and path networks.
  - It compiles bound `MF_FIELD`, `MF_FIELD_T` (with time) and `MF_FIELD_P` (with time and a vec4 that `set_params` sets) fields into one compute shader. The fields are written once as C++ lambdas, and the text is also emitted as GLSL.
  - Several fields can drive one target; they're summed, and a field can be limited to one cluster.
  - `SpatialFieldConfig` can add GPU stages: a spatial hash, neighbour-density colour, a deterministic claim and absorb step, and population spawning from a reserve.
  - It runs through `NetworkGeometryBuffer` and `VertexFieldProcessor` (`Buffers/Network/NetworkGeometryFieldWiring.cpp`), not through MeshNetwork.
  - `Nexus::bind_params(op)` lets an Agent drive its parameters every frame with no rebuild. For a game: sparks, dust, swarms and smoke that react to bodies and players. Feed it Jolt positions through `set_params` or a field's inputs; the particles themselves stay outside Jolt.
- **Writing models:** `AssimpModelWriter`, and `save_mesh` in `API/Depot.hpp`.
- **Alembic** (`IO/Spatial/SpatialCache.hpp`, `SpatialReader.hpp`) handles points streams (ids, velocities) and curves streams over time, with attributes, in Ogawa or HDF5. It has no polymesh or transform objects. It's good for recording and replaying a simulation, not for importing animated characters.
- **Also available:** OpenVDB volumes, EXR and other images, video and audio files.

## 9. Files read in this pass

**Nexus.** Read in full: every `.hpp` and `.cpp` except `State/Encoder.cpp`, `State/Decoder.cpp` and `State/Schema.describe.hpp`. `State/Schema.hpp` was read.

**Kinesis.**
- Read in full: `Spatial/Bounds.hpp`, `Spatial/HitTest.hpp`, `Spatial/HitRegion.hpp`, `Spatial/SpatialIndex.hpp`, `Viewport/NavigationState.hpp` and `.cpp`, and the `GeometryPrimitives.hpp` header.
- Read in part: `SpatialIndex.cpp` (publish and the grid query).
- Function lists only: `Morphology.hpp`, `Spatial/Projection.hpp`, `Viewport/ViewTransform.hpp`, `MatrixTransforms.hpp`.
- Not read: Discrete, Vision, Stochastic, the Tendency internals, `ProximityGraphs`, `Lattice`, `OrbitState`. Acoustics was not needed here.

**Forma.**
- Read in full: `Forma.hpp`, `Context.hpp`, `Primitives/FormFactory.hpp`, `Primitives/Geometry.hpp`, `Primitives/Mapped.hpp`.
- Read in part: `Element.hpp`, `Surface.hpp`, `Bridge.hpp`, `Tend.hpp`, `Layer.hpp`, `Primitives/LayoutCursor.hpp`, `Target.cpp`.
- Class docs and setters only: `Entry`, `TextField`, `Dropdown`, `Picker`, `ColorPicker`, `Collapsible`, `Scrollable`.
- Grep only: `Context.cpp` (focus), `FormaBuffer.cpp` (depth).
- `Manifest/Instantiate.hpp`: entry points only.
- `Portal/Text`: `InkPress.hpp` (PressParams), `GlyphMesh.hpp` (start), `Text.hpp` (font calls).
- `Vruta/WindowEventSource.cpp` (`signal`) and `.hpp` (queries).
- The installed guides `Portal-Forma-UI-Patterns.md` and `Portal-Forma.md`: headings, the button, z-order and ImGui sections.
- Not read: `Plot`, `Inspect`, `Layer.cpp`, `Atelier`, the widget `.cpp` files.

**Engine.**
- `GraphicsSubsystem.cpp` (process and loop), `RenderProcessor.cpp` (the transform publish).
- The API of `PhysicsOperator.hpp`, `InstanceNetwork.hpp` and `GeometrySlot.hpp`.
- The function lists of `API/Core.hpp`, `API/Input.hpp` and `API/Depot.hpp`.

**IO and input.**
- Read in full: `ModelReader.hpp`, `SpatialReader.hpp`, `CameraSource.hpp`, `FFmpegCameraReader.hpp`, `Nodes/Input/HIDNode.hpp` and `.cpp`, `Nodes/Input/InputNode.hpp` and `.cpp`.
- Read in part: `ModelReader.cpp` (extraction and network building), `SpatialCache.hpp`, `HIDBackend.hpp` (reports), `HIDBackend.cpp` (`parse_hid_report`).
- Grep only: `InputBackend.hpp`, `InputManager.cpp` (thread and dispatch), `InputBinding.hpp`, the cursor-mode use in the window backends.

**Mesh and field operators.**
- Read in full: `MeshSlot.hpp`.
- Read in part: `MeshNetwork.hpp`, `MeshTransformOperator.hpp`, `MeshFieldOperator.hpp`, `GpuFieldOperator.hpp` (`SpatialFieldConfig` and the bind API), `Kinesis/Tendency/DualField.hpp` (`DualField`, `MF_FIELD`).
- Not read: the `.cpp` files of these operators, `NetworkGeometryFieldWiring.cpp`, `VertexFieldProcessor`.

**Jolt.** The GitHub releases page, `Build/CMakeLists.txt` and `Jolt/Jolt.cmake` on master (15 Nov 2026).
