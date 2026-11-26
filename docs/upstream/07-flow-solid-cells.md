Title: Volume flow: solid cells inside the lattice

Split out of #197 (item 11), as asked.

`FlowConfig::walls` closes the six faces of the lattice (`WallProcessor`, `volume_wall.comp`). Nothing inside the lattice can be solid, so smoke in a maze goes straight through the walls.

Could FlowConfig take an optional field that marks solid cells? Where a cell is solid:

- the wall stage sets its velocity to zero, and the normal component in the open cells next to it
- the Jacobi stage reads a solid neighbour's pressure as the centre cell's, the way `volume_index_clamped` already does at the faces
- carried scalars stay at zero

We'd fill the field from the maze grid, or from Expanses. Our original note said "skip it if it isn't small", and that still holds.
