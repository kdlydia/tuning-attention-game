Title: RaymarchBuffer: stop the march at scene depth

Split out of #197 (item 9), as asked.

`RaymarchBuffer::setup_rendering` turns the depth test off, and `volume_raymarch.frag` marches from where the ray enters the box to where it leaves it. So a volume draws over anything inside it or in front of it. In our maze, fog in a corridor shows through the walls.

An image target can already keep its depth (`ImageTarget::Config::sample_depth`). Could the raymarch take an optional depth texture and stop the march there? The shader would turn the scene depth back into a distance along the ray (so it needs the inverse projection, or near and far) and clamp `exit` to it. With no depth bound it would work as it does now.

The camera is usually inside the fog for us, so `enter` is 0 and only `exit` matters.
