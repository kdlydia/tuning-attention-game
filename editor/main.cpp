// Starts MayaFlux and hands the graphics thread to the Rust editor: editor_start() once,
// then editor_frame() every frame. First it opens the game's own 3D view of the maze, which
// reloads when the editor saves, and the scene drawn in it, which the editor loads assets
// into. Model or Alembic files given as arguments are loaded into the scene first.
// With --test it runs the binding's press test instead, and with --check-player FILE it loads
// FILE into the scene, drives its player through play, pause, a crossfade, seek and stop, then
// removes it.
// Both exit 0 on pass, 1 on fail, 2 if the check never ran.

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/Portal/Text/Text.hpp"

#include "MazeView.hpp"
#include "forma/src/lib.rs.h"
#include "scene/Scene.hpp"
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

    static std::atomic<int> result { test || check ? 2 : 0 };
    constexpr auto frame = MayaFlux::Vruta::ProcessingToken::FRAME_ACCURATE;

    std::vector<std::pair<double, std::function<void()>>> steps;
    if (test) {
        steps.emplace_back(0.1, [] { mf_rs::selftest_start(); });
        steps.emplace_back(1.0, [] { mf_rs::selftest_poke(); });
        steps.emplace_back(0.5, [] {
            result = mf_rs::selftest_check() ? 0 : 1;
            MayaFlux::Unblock();
        });
    } else if (check) {
        static const std::string file = argv[2];
        static game::Scene* scene = nullptr;
        static size_t index = 0;
        static game::Player* player = nullptr;
        static double held = 0.0;
        const auto finish = [] {
            result = g_checks_passed ? 0 : 1;
            MayaFlux::Unblock();
        };
        steps.emplace_back(0.1, [finish] {
            auto* view = game::MazeView::open(GAME_MAZE_FILE);
            scene = view ? game::Scene::open(view->window(), view->view()) : nullptr;
            const auto loaded = scene ? scene->load(file) : std::nullopt;
            index = loaded.value_or(0);
            player = loaded ? scene->asset(index)->player.get() : nullptr;
            expect(player && !player->motions().empty(), "the file loads with a motion");
            if (!player || player->motions().empty()) {
                player = nullptr;
                finish();
                return;
            }
            player->play(0);
        });
        steps.emplace_back(1.0, [] {
            if (!player) {
                return;
            }
            expect(player->current() == 0 && !player->paused() && player->time() > 0.0, "motion 0 plays");
            player->pause();
        });
        steps.emplace_back(0.3, [] { held = player ? player->time() : 0.0; });
        steps.emplace_back(0.5, [] {
            if (!player) {
                return;
            }
            expect(player->paused() && player->time() == held, "pause holds the time");
            player->play(player->motions().size() > 1 ? 1 : 0);
        });
        steps.emplace_back(0.6, [] {
            if (!player) {
                return;
            }
            const size_t wanted = player->motions().size() > 1 ? 1 : 0;
            expect(player->current() == wanted && !player->paused(), "playing another motion switches to it");
            player->seek(0.25);
        });
        steps.emplace_back(0.1, [] {
            if (!player) {
                return;
            }
            expect(player->time() >= 0.25 && player->time() < 0.6, "seek moves the time");
            player->stop();
        });
        steps.emplace_back(0.5, [] {
            if (!player) {
                return;
            }
            expect(!player->current(), "stop returns to rest");
            player = nullptr;
            expect(scene->remove(index) && !scene->asset(index) && scene->slots() == index + 1, "remove takes it out and keeps the slot");
        });
        steps.emplace_back(0.5, [finish] {
            if (!scene || !g_checks_passed) {
                return;
            }
            expect(!scene->remove(index), "it can't be removed twice");
            finish();
        });
    } else {
        static game::MazeView* view = nullptr;
        static const std::vector<std::string> first_assets(argv + 1, argv + argc);
        steps.emplace_back(0.1, [] {
            // Inter, rasterized near the size the tool rows draw it, so glyphs are not rescaled.
            // MayaFlux's own default (sans-serif at 24 px) stays if Inter isn't installed.
            if (!MayaFlux::Portal::Text::set_default_font("Inter", "Regular", 16, 1024)) {
                MF_PRINT("editor: Inter not found, keeping the default font");
            }
            view = game::MazeView::open(GAME_MAZE_FILE);
            if (view) {
                auto* scene = game::Scene::open(view->window(), view->view());
                mf_scene::attach(scene);
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
        // One frame at the default 60 fps. Becomes Kriya::each_cycle once a nightly has it.
        MayaFlux::schedule_metro(1.0 / 60, [] { editor_frame(); }, "editor_frame", frame);
        MayaFlux::schedule_metro(0.25, [] { if (view) view->reload_if_changed(); }, "game_view_reload", frame);
    }
    MayaFlux::schedule_sequence(std::move(steps), "editor", frame);

    MayaFlux::Await();
    MayaFlux::End();
    return result.load();
}
