// NOLINTBEGIN
#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include "MazeView.hpp"

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
 * Shows data/maze.txt in 3D, reloading when the editor saves it. The next
 * steps are listed in README.md.
 */
void compose()
{
    constexpr auto frame = MayaFlux::Vruta::ProcessingToken::FRAME_ACCURATE;
    static game::MazeView* view = nullptr;
    const auto path = std::filesystem::path(MayaFlux::Config::project_root()) / "data" / "maze.txt";

    MayaFlux::schedule_sequence({ { 0.1, [path] { view = game::MazeView::open(path); } } }, "maze_view_open", frame);
    MayaFlux::schedule_metro(0.25, [] { if (view) view->reload_if_changed(); }, "maze_view_reload", frame);
}

// NOLINTEND
