// Starts MayaFlux and hands the graphics thread to the Rust editor: editor_start() once,
// then editor_frame() every frame. Also opens the game's own 3D view of the maze, which
// reloads when the editor saves. With --test it runs the binding's press test instead
// and exits 0 on pass, 1 on fail, 2 if the check never ran.

#include "MayaFlux/MayaFlux.hpp"

#include "MazeView.hpp"
#include "forma/src/lib.rs.h"

#include <atomic>
#include <string_view>

extern "C" void editor_start();
extern "C" void editor_frame();

int main(int argc, char* argv[])
{
    const bool test = argc > 1 && std::string_view(argv[1]) == "--test";

#ifdef MAYAFLUX_PROJECT_ROOT
    MayaFlux::Config::set_project_root(MAYAFLUX_PROJECT_ROOT);
#endif

    MayaFlux::Init();
    MayaFlux::Start();

    static std::atomic<int> result { test ? 2 : 0 };
    constexpr auto frame = MayaFlux::Vruta::ProcessingToken::FRAME_ACCURATE;

    std::vector<std::pair<double, std::function<void()>>> steps;
    if (test) {
        steps.emplace_back(0.1, [] { mf_rs::selftest_start(); });
        steps.emplace_back(1.0, [] { mf_rs::selftest_poke(); });
        steps.emplace_back(0.5, [] {
            result = mf_rs::selftest_check() ? 0 : 1;
            MayaFlux::Unblock();
        });
    } else {
        static game::MazeView* view = nullptr;
        steps.emplace_back(0.1, [] {
            editor_start();
            view = game::MazeView::open(GAME_MAZE_FILE);
        });
        // One frame at the default 60 fps. Becomes Kriya::each_cycle once a nightly has it.
        MayaFlux::schedule_metro(1.0 / 60, [] { editor_frame(); }, "editor_frame", frame);
        MayaFlux::schedule_metro(0.25, [] { if (view) view->reload_if_changed(); }, "game_view_reload", frame);
    }
    MayaFlux::schedule_sequence(std::move(steps), "editor", frame);

    MayaFlux::Await();
    MayaFlux::End();
    return result.load();
}
