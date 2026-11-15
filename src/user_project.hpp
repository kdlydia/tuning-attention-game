// NOLINTBEGIN
#pragma once

#include "MayaFlux/MayaFlux.hpp"

/**
 * @brief Engine configuration, run once before Init().
 *
 * The launcher loads mayaflux.json from the project folder first, so values
 * set here win. Anything left unset keeps its default (48 kHz, stereo,
 * 512 frames, 60 FPS).
 */
void settings()
{
}

/**
 * @brief Builds the game. Runs once after Start().
 *
 * Empty for now. The first steps are listed in README.md.
 */
void compose()
{
}

// NOLINTEND
