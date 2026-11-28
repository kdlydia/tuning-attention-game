#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

// A textured part of a scene asset: its diffuse texture, lit by scene_light. Matches the
// outputs of mesh_network.vert and the bindings MeshNetworkBuffer gives a textured part.

#include "scene_light.glsl"

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
layout(location = 3) flat in uint in_slot;
layout(location = 4) in vec3 in_world_pos;
layout(location = 8) flat in vec4 in_eye;

layout(set = 0, binding = 3) uniform sampler2D diffuseTex[];

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 tex = texture(diffuseTex[in_slot], in_uv);
    if (tex.a < SCENE_ALPHA_CUTOFF) {
        discard;
    }
    vec3 normal = scene_normal(in_normal, gl_FrontFacing);
    out_color = vec4(influence_shade(surface_texture_color(tex.rgb), in_world_pos, normal, in_eye), 1.0);
}
