Title: Part 2 of #196: requests from a maze game (a Locus that plays file cameras, timed weight fades, named markers)

We're building a maze game on MayaFlux ([tuning-attention-game](https://github.com/kdlydia/tuning-attention-game)): the world is in Nexus and the player moves with a drawing tablet. The nightly with #196 gives us most of an animation player. These are the pieces we would otherwise write ourselves, in case they fit part 2.

**1. A Locus that plays a file camera.**
The player's camera is a Locus moved by the tablet. For camera paths we want the same Locus to follow an Alembic camera, then hand control back to the tablet. Right now the two don't meet:

- A Locus builds its view from its `NavigationState`: yaw and pitch (pitch clamped to ±89°), no roll, and a symmetric `fov_radians`.
- A file camera gives a `Kinesis::Lens` and a row placement. `view_from_lens` makes a `ViewTransform` from them, but nothing passes that to a Locus.

So a camera path and the tablet end up as two writers to `set_view_transform`. Part 2's "an entity can follow a named part" would cover the position. Could a Locus that follows a camera row also take its roll and its lens (focal length, film back, clip planes), then let go so navigation carries on from where the path ended?

**2. Timed weight fades on a Course.**
A crossfade is two lanes mixed by `placement_blend`, with one course's weight going down while the other's goes up. `Course::weight(double)` sets the weight at once, so the ramp is our own per-frame code. Something like `fade_weight(target, seconds)`, or a weight that can follow a TimeMap the way a course's time can, would make a crossfade one call.

**3. Markers that tell the entity which one fired.**
`Wiring::on(course->subscribe(CourseEventKind::Marked))` fires the entity on every marker, but the value isn't passed on, so the entity can't tell a footstep from a door cue. Part 2's "named markers become events that fire entities" would cover this. We'd use it for footstep sounds and for a cue that unlocks a room.

**4. Rooms and corridors driving playback** (already on the part 2 list).
+1 for an Expanse's enter and exit starting and stopping a motion or a segment, and for position as the playhead. Our rooms are Expanses on the maze, and a corridor that scrubs a recording as you walk it is one of the things we want to try.

**5. Lower priority: skinned characters drawn as instances.**
A skinned mesh in an instanced part shows its rest pose. If several copies of one character roam the maze, each instance would need its own pose. We haven't decided whether the game has characters yet.

---

Follow-up comment, posted on 25 Nov: https://github.com/MayaFlux/MayaFlux/issues/197#issuecomment-7167379763

A few smaller things we found while planning the editor. They're all in classes part 2 touches, and from the code they look small. Numbering carries on from above.

**6. Lit shaders that use the normal.**
`mesh_network_lit.frag`, `mesh_network_textured_lit.frag` and `instance_textured_lit.frag` take `in_normal` but don't use it, so an influence lights a surface the same whether it faces the influence or not. A facing term would fix it. There's also no lit shader for untextured instances that matches `instance.vert`, which is what our maze walls use.

**7. A Hull around one slot or instance.**
`read_bounds` honours `collection` only for a NetworkGeometryBuffer. A MeshNetworkBuffer is bounded as its whole world mesh, and an InstanceNetworkBuffer as the union of its slots. Our walls are one InstanceNetworkBuffer, so a Hull over them is one box around the whole maze. If the index could pick a slot, and the Hull mapped through that slot's transform, each wall or each part of a realized model could have its own Hull. `instance_bounds` already walks the slots with their palettes.

**8. A ray test on an Expanse.**
We want to point at things through their Hulls, but an Expanse only tests points, and `Kinesis::ray_cast` takes a SpatialIndex3D. A virtual `intersect(const Kinesis::Ray&)` returning the distance, answered by the box, Hull and Mantle (nullopt for a predicate alone), and a Fabric query for the nearest hit would cover it. Hull's `contains` already inverts `read_placement`, so the ray would go through the same inverse. A public `reach()` would also help, for a quick pre-test and for framing a camera on a region.

**9. The raymarch stopping at scene depth.**
`volume_raymarch.frag` marches the whole box, so smoke draws over walls that stand inside the volume. Now that an image target can keep its depth (`sample_depth`), an optional depth binding where the march stops would let a volume sit inside a scene. We'd use it for fog in the corridors.

**10. Hull and Mantle in saved state.**
Nothing is encoded for a Hull, and a decoded Mantle is a plain box. If a buffer could be registered by name, the way `register_contains` names a function, the decoder could rebuild both, and a saved world would come back with its regions still wrapping their objects. This may belong with the part 2 tidying of extension structs.

**11. Solid cells in the flow stages** (maybe not small).
`FlowConfig::walls` closes the six faces of the box. For smoke moving through a maze, a field marking solid cells, with velocity zeroed there and nothing carried in, would do, and an Expanse could fill it. Skip it if it isn't small.

Reply, posted on 26 Nov after #198 closed this issue: https://github.com/MayaFlux/MayaFlux/issues/197#issuecomment-7198560799

Thanks! Opened #199 for 9 and #200 for 11. Everything else we asked for is in #198.
