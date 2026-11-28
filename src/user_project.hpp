// NOLINTBEGIN
#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/Kriya/InputEvents.hpp"
#include "MayaFlux/Nexus/Pheme/InfluenceList.hpp"
#include "MayaFlux/Vruta/EventManager.hpp"

#include "Creature.hpp"
#include "Live.hpp"
#include "MazeView.hpp"
#include "scene/Scene.hpp"

/**
 * @brief Engine configuration, run once before Init().
 *
 * The launcher loads mayaflux.json from the project folder first, so values
 * set here win. Anything left unset keeps its default (48 kHz, stereo,
 * 512 frames, 60 FPS).
 *
 * A build with Lila starts its live coding session here (Live.hpp), first and
 * on its own thread, so Clang starts up while the engine and the game do.
 */
void settings()
{
    game::live::start(std::filesystem::path(MayaFlux::Config::project_root()) / "src");
}

/**
 * @brief Builds the game. Runs once after Start().
 *
 * Shows data/maze.txt in 3D, reloading when the editor saves it, with a
 * procedural creature swaying on the floor cell nearest the middle, all lit by
 * the scene's sun and sky. Closing the window ends the game. The next steps are
 * listed in README.md.
 */
void compose()
{
    constexpr auto frame = MayaFlux::Vruta::ProcessingToken::FRAME_ACCURATE;
    const auto path = std::filesystem::path(MayaFlux::Config::project_root()) / "data" / "maze.txt";

    MayaFlux::schedule_sequence({ { 0.1, [path] {
        auto lights = std::make_shared<MayaFlux::Nexus::InfluenceList>();
        auto* view = game::MazeView::open(path, lights);
        if (view) {
            game::Scene::open(view->window(), view->view(), lights, MAYAFLUX_PROJECT_SHADER_DIR);
            game::Creature::open(view->window(), view->view(), view->floor_near_center(), lights);
            MayaFlux::get_event_manager()->add_event(
                std::make_shared<MayaFlux::Vruta::Event>(MayaFlux::Kriya::window_closed(view->window(), [] { MayaFlux::Unblock(); })),
                "game_view_closed");
            MayaFlux::schedule_metro(0.25, [view] { view->reload_if_changed(); }, "maze_view_reload", frame);
        }
    } } }, "maze_view_open", frame);
}

// NOLINTEND
