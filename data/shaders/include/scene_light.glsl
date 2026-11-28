#ifndef SCENE_LIGHT_GLSL
#define SCENE_LIGHT_GLSL

// Light for the scene's shaders: MayaFlux's influence_shade (the sky the scene's InfluenceList
// holds, then every influence in it, through the renderer's surface constants and a tone
// curve), with two things the library's lit shaders leave out for files:
// - the scene draws without culling, so a face seen from behind is lit as its front;
// - a material's alpha mask cuts out texels below SCENE_ALPHA_CUTOFF (zero keeps them all).
// Include it ahead of the shader's own declarations, as lighting_influence.glsl asks.

#include "include/lighting_influence.glsl"

layout(constant_id = 16) const float SCENE_ALPHA_CUTOFF = 0.0;

/** The normal of the side being drawn. A zero normal stays zero. */
vec3 scene_normal(vec3 normal, bool front_facing)
{
    return front_facing ? normal : -normal;
}

#endif
