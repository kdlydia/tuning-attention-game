## Summary

`MeshNetwork` slots already have parents, a `local_transform` and a resolved `world_transform`. `ModelReader::create_mesh_network`, however, adds every `aiMesh` as a root slot with an identity transform, and the Assimp node tree isn't read. So a model whose parts are placed by its scene graph, as most glTF and FBX files are, loads with every part at its own origin.

## Current behaviour

Read on develop `2084dfae`; the header is the same in the nightly from main `38e09827`.

- `create_mesh_network` loops over `scene->mMeshes` and calls `net->add_slot(mesh_name, node)` with no parent and no transform (`IO/Model/ModelReader.cpp:150-200`, the call at 196).
- `extract_meshes` and `create_mesh_buffers` likewise go mesh by mesh. `mRootNode` is checked only for null after import, and no `mTransformation` is read anywhere in `ModelReader.cpp`. The import flags don't include `aiProcess_PreTransformVertices` (`ModelReader.cpp:256-261`).
- `MeshNetwork` and `MeshSlot` already support the result this needs: `add_slot(name, node, parent)`, `world = parent_world * local`, and `find_slot(name)` (`Nodes/Network/MeshNetwork.hpp`, `MeshSlot.hpp`).

## Proposal

1. **Walk the node tree in `create_mesh_network`, from `mRootNode`.**
   - Each mesh a node references becomes a slot. Its name is the node name, or the mesh name when the node has none.
   - `local_transform` is the node's `mTransformation`. Assimp's matrix is row-major and glm's is column-major, so it's transposed on the way in.
   - The parent is the slot made for the nearest ancestor. For an ancestor with no mesh, either fold its transform into its children's `local_transform`, or add an empty slot if MeshNetwork can hold a slot without geometry. That choice is yours.
   - A mesh referenced by several nodes appears once per node, sharing the `MeshWriterNode` if that's possible.
2. **Static scenes:** optionally, an import option that applies `aiProcess_PreTransformVertices`, for scenes where a flat set of meshes is enough.

## Acceptance

- A glTF with a parent node rotated +90° about Y and a child mesh at (1, 0, 0): after `create_mesh_network`, the child's `world_transform` puts its origin at (0, 0, -1).
- A node named `wheel_fl` is found with `find_slot("wheel_fl")`, and its parent is its parent node's slot.
- A mesh used by two nodes appears in two slots with different world transforms.
- A file with a single node (most OBJ, STL and PLY) loads exactly as today.

## Not requested

Bones, skinning and animation import. I'll raise those separately if they're wanted.

## Context

In a game I'm planning, each part of a model becomes a physics body, and its slot's transform follows the body. `MeshTransformOperator` and `MeshFieldOperator` cover the rest. Only the file's own placement is missing.
