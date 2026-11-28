#include "scene_shim.h"

#include "scene/src/lib.rs.h"

#include "scene/Scene.hpp"
#include "scene/Selection.hpp"

namespace mf_scene {

namespace {

    game::Scene* g_scene = nullptr;
    game::Selection* g_selection = nullptr;

    game::Player* player_of(std::size_t asset)
    {
        auto* found = g_scene ? g_scene->asset(asset) : nullptr;
        return found ? found->player.get() : nullptr;
    }

}

void attach(game::Scene* scene, game::Selection* selection)
{
    g_scene = scene;
    g_selection = selection;
}

std::int64_t selected()
{
    const auto chosen = g_selection ? g_selection->selected() : std::nullopt;
    return chosen ? static_cast<std::int64_t>(*chosen) : -1;
}

void select(std::int64_t asset)
{
    if (g_selection) {
        g_selection->select(asset >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(asset)) : std::nullopt);
    }
}

std::int32_t load(rust::Str path)
{
    if (!g_scene) {
        MF_PRINT("scene: no scene is attached");
        return -1;
    }
    const auto index = g_scene->load(std::filesystem::path(std::string(path)));
    return index ? static_cast<std::int32_t>(*index) : -1;
}

bool remove(std::size_t asset)
{
    return g_scene && g_scene->remove(asset);
}

std::size_t asset_count()
{
    return g_scene ? g_scene->slots() : 0;
}

bool present(std::size_t asset)
{
    return g_scene && g_scene->asset(asset);
}

rust::String asset_name(std::size_t asset)
{
    const auto* found = g_scene ? g_scene->asset(asset) : nullptr;
    return found ? rust::String(found->name) : rust::String();
}

rust::String asset_file(std::size_t asset)
{
    const auto* found = g_scene ? g_scene->asset(asset) : nullptr;
    return found ? rust::String(found->file.string()) : rust::String();
}

rust::Vec<MotionInfo> motions(std::size_t asset)
{
    rust::Vec<MotionInfo> result;
    if (const auto* player = player_of(asset)) {
        for (const auto& motion : player->motions()) {
            result.push_back(MotionInfo { .name = rust::String(motion.name), .length = motion.length });
        }
    }
    return result;
}

bool play(std::size_t asset, std::size_t motion)
{
    auto* player = player_of(asset);
    return player && player->play(motion);
}

bool pause(std::size_t asset)
{
    auto* player = player_of(asset);
    return player && player->pause();
}

bool resume(std::size_t asset)
{
    auto* player = player_of(asset);
    return player && player->resume();
}

void stop(std::size_t asset)
{
    if (auto* player = player_of(asset)) {
        player->stop();
    }
}

bool seek(std::size_t asset, double seconds)
{
    auto* player = player_of(asset);
    return player && player->seek(seconds);
}

void set_speed(std::size_t asset, double value)
{
    if (auto* player = player_of(asset)) {
        player->speed(value);
    }
}

void set_loop(std::size_t asset, bool looping)
{
    if (auto* player = player_of(asset)) {
        player->loop(looping);
    }
}

PlayerState state(std::size_t asset)
{
    const auto* player = player_of(asset);
    if (!player) {
        return PlayerState { .motion = -1, .time = 0.0, .length = 0.0, .paused = true, .speed = 1.0, .looping = true };
    }
    const auto current = player->current();
    return PlayerState {
        .motion = current ? static_cast<std::int32_t>(*current) : -1,
        .time = player->time(),
        .length = current ? player->motions()[*current].length : 0.0,
        .paused = player->paused(),
        .speed = player->speed(),
        .looping = player->looping(),
    };
}

}
