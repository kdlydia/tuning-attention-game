#pragma once

// The C++ half of the scene bridge. This header names no MayaFlux type, so cargo can
// compile the generated bridge without MayaFlux's flags. The scene itself is the game's
// (src/scene/Scene.hpp); the host attaches it before the editor starts.

#include "rust/cxx.h"

#include <cstddef>
#include <cstdint>

namespace game {
class Scene;
}

namespace mf_scene {

// Defined by the generated bridge header.
struct MotionInfo;
struct PlayerState;

// For the C++ host: the scene the editor works on.
void attach(game::Scene* scene);

std::int32_t load(rust::Str path);
bool remove(std::size_t asset);
std::size_t asset_count();
bool present(std::size_t asset);
rust::String asset_name(std::size_t asset);
rust::String asset_file(std::size_t asset);
rust::Vec<MotionInfo> motions(std::size_t asset);
bool play(std::size_t asset, std::size_t motion);
bool pause(std::size_t asset);
bool resume(std::size_t asset);
void stop(std::size_t asset);
bool seek(std::size_t asset, double seconds);
void set_speed(std::size_t asset, double value);
void set_loop(std::size_t asset, bool looping);
PlayerState state(std::size_t asset);

}
