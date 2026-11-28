// Starts MayaFlux and hands the graphics thread to the Rust editor: editor_start() once,
// then editor_frame() every frame. First it opens the game's own 3D view of the maze, which
// reloads when the editor saves, and the scene drawn in it, which the editor loads assets
// into, lit by the scene's sun and sky. A left click in the game view chooses an asset for
// the player window. Model or Alembic files given as arguments are loaded into the scene first.
// With --test it runs the binding's press test instead, and with --check-player FILE it loads
// FILE into the scene, drives its player through play, pause, a crossfade, seek and stop, then
// removes it.
// Both exit 0 on pass, 1 on fail, 2 if the check never ran.

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/Kriya/Tasks.hpp"
#include "MayaFlux/Nexus/Pheme/InfluenceList.hpp"
#include "MayaFlux/Portal/Text/Text.hpp"

#include "MazeView.hpp"
#include "forma/src/lib.rs.h"
#include "scene/Scene.hpp"
#include "scene/Selection.hpp"
#include "scene_shim.h"

#include <atomic>
#include <string_view>

extern "C" void editor_start();
extern "C" void editor_frame();

namespace {

bool g_checks_passed = true;

void expect(bool pass, std::string_view what)
{
    MF_PRINT("check: {} {}", pass ? "ok  " : "FAIL", what);
    g_checks_passed = g_checks_passed && pass;
}

}

int main(int argc, char* argv[])
{
    const bool test = argc > 1 && std::string_view(argv[1]) == "--test";
    const bool check = argc > 2 && std::string_view(argv[1]) == "--check-player";

#ifdef MAYAFLUX_PROJECT_ROOT
    MayaFlux::Config::set_project_root(MAYAFLUX_PROJECT_ROOT);
#endif

    MayaFlux::Init();
    MayaFlux::Start();

    // Read after End(), which empties MayaFlux's persistent store, so it stays out of it.
    std::atomic<int> result { test || check ? 2 : 0 };
    constexpr auto frame = MayaFlux::Vruta::ProcessingToken::FRAME_ACCURATE;

    std::vector<std::pair<double, std::function<void()>>> steps;
    if (test) {
        steps.emplace_back(0.1, [] { mf_rs::selftest_start(); });
        steps.emplace_back(1.0, [] { mf_rs::selftest_poke(); });
        steps.emplace_back(0.5, [&result] {
            result = mf_rs::selftest_check() ? 0 : 1;
            MayaFlux::Unblock();
        });
    } else if (check) {
        struct Check {
            std::string file;
            game::Scene* scene {};
            size_t index {};
            game::Player* player {};
            double held {};
        };
        auto& run = MayaFlux::make_persistent<Check>();
        run.file = argv[2];
        const auto finish = [&result] {
            result = g_checks_passed ? 0 : 1;
            MayaFlux::Unblock();
        };
        steps.emplace_back(0.1, [&run, finish] {
            auto lights = std::make_shared<MayaFlux::Nexus::InfluenceList>();
            auto* view = game::MazeView::open(GAME_MAZE_FILE, lights);
            run.scene = view ? game::Scene::open(view->window(), view->view(), lights, GAME_SHADER_DIR) : nullptr;
            const auto loaded = run.scene ? run.scene->load(run.file) : std::nullopt;
            run.index = loaded.value_or(0);
            run.player = loaded ? run.scene->asset(run.index)->player.get() : nullptr;
            expect(run.player && !run.player->motions().empty(), "the file loads with a motion");
            if (!run.player || run.player->motions().empty()) {
                run.player = nullptr;
                finish();
                return;
            }
            run.player->play(0);
        });
        steps.emplace_back(1.0, [&run] {
            if (!run.player) {
                return;
            }
            expect(run.player->current() == 0 && !run.player->paused() && run.player->time() > 0.0, "motion 0 plays");
            run.player->pause();
        });
        steps.emplace_back(0.3, [&run] { run.held = run.player ? run.player->time() : 0.0; });
        steps.emplace_back(0.5, [&run] {
            if (!run.player) {
                return;
            }
            expect(run.player->paused() && run.player->time() == run.held, "pause holds the time");
            run.player->play(run.player->motions().size() > 1 ? 1 : 0);
        });
        steps.emplace_back(0.6, [&run] {
            if (!run.player) {
                return;
            }
            const size_t wanted = run.player->motions().size() > 1 ? 1 : 0;
            expect(run.player->current() == wanted && !run.player->paused(), "playing another motion switches to it");
            run.player->seek(0.25);
        });
        steps.emplace_back(0.1, [&run] {
            if (!run.player) {
                return;
            }
            expect(run.player->time() >= 0.25 && run.player->time() < 0.6, "seek moves the time");
            run.player->stop();
        });
        steps.emplace_back(0.5, [&run] {
            if (!run.player) {
                return;
            }
            expect(!run.player->current(), "stop returns to rest");
            run.player = nullptr;
            expect(run.scene->remove(run.index) && !run.scene->asset(run.index) && run.scene->slots() == run.index + 1,
                "remove takes it out and keeps the slot");
        });
        steps.emplace_back(0.5, [&run, finish] {
            if (!run.scene || !g_checks_passed) {
                return;
            }
            expect(!run.scene->remove(run.index), "it can't be removed twice");
            finish();
        });
    } else {
        steps.emplace_back(0.1, [first_assets = std::vector<std::string>(argv + 1, argv + argc)] {
            // Inter, rasterized near the size the tool rows draw it, so glyphs are not rescaled.
            // MayaFlux's own default (sans-serif at 24 px) stays if Inter isn't installed.
            if (!MayaFlux::Portal::Text::set_default_font("Inter", "Regular", 16, 1024)) {
                MF_PRINT("editor: Inter not found, keeping the default font");
            }
            auto lights = std::make_shared<MayaFlux::Nexus::InfluenceList>();
            auto* view = game::MazeView::open(GAME_MAZE_FILE, lights);
            if (view) {
                MayaFlux::schedule_metro(0.25, [view] { view->reload_if_changed(); }, "game_view_reload", frame);
                auto* scene = game::Scene::open(view->window(), view->view(), lights, GAME_SHADER_DIR);
                mf_scene::attach(scene, game::Selection::open(*scene, *view));
                // In two rows across where the view looks: pieces up to 2.5 m wide in front, and
                // wider ones 4 m behind them, each row centred and spaced by the pieces' widths.
                const glm::vec3 middle = scene->looked_at();
                std::vector<size_t> front;
                std::vector<size_t> back;
                for (const auto& file : first_assets) {
                    if (const auto index = scene->load(file, middle)) {
                        const auto& size = scene->asset(*index)->size;
                        (std::max(size.x, size.z) > 2.5F ? back : front).push_back(*index);
                    }
                }
                const auto lay_out = [scene, middle](const std::vector<size_t>& row, float behind) {
                    constexpr float gap = 0.8F;
                    float width = 0.0F;
                    for (const size_t i : row) {
                        width += std::max(scene->asset(i)->size.x, 1.0F) + gap;
                    }
                    float x = -width / 2;
                    for (const size_t i : row) {
                        const float w = std::max(scene->asset(i)->size.x, 1.0F) + gap;
                        const auto& player = scene->asset(i)->player;
                        player->set_placement(glm::translate(glm::mat4(1.0F), glm::vec3(x + w / 2, 0.0F, -behind)) * player->placement());
                        x += w;
                    }
                };
                lay_out(front, 0.0F);
                lay_out(back, 4.0F);
            }
            editor_start();
        });
        MayaFlux::get_scheduler()->add_task(MayaFlux::Kriya::each_cycle([] { editor_frame(); }, frame), "editor_frame");
    }
    MayaFlux::schedule_sequence(std::move(steps), "editor", frame);

    MayaFlux::Await();
    MayaFlux::End();
    return result.load();
}
