// NOLINTBEGIN
#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/Kriya/InputEvents.hpp"
#include "MayaFlux/Vruta/EventManager.hpp"

#include "Creature.hpp"
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
 * Shows data/maze.txt in 3D, reloading when the editor saves it, with a
 * procedural creature swaying on the floor cell nearest the middle. Closing the
 * window ends the game. The next steps are listed in README.md.
 */
void compose()
{
    constexpr auto frame = MayaFlux::Vruta::ProcessingToken::FRAME_ACCURATE;
    static game::MazeView* view = nullptr;
    const auto path = std::filesystem::path(MayaFlux::Config::project_root()) / "data" / "maze.txt";

    MayaFlux::schedule_sequence({ { 0.1, [path] {
        view = game::MazeView::open(path);
        if (view) {
            game::Creature::open(view->window(), view->view(), view->floor_near_center());
            MayaFlux::get_event_manager()->add_event(
                std::make_shared<MayaFlux::Vruta::Event>(MayaFlux::Kriya::window_closed(view->window(), [] { MayaFlux::Unblock(); })),
                "game_view_closed");
        }
    } } }, "maze_view_open", frame);
    MayaFlux::schedule_metro(0.25, [] { if (view) view->reload_if_changed(); }, "maze_view_reload", frame);
}

// NOLINTEND
