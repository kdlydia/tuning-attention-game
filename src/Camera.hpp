#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/API/Stagecraft.hpp"
#include "MayaFlux/Nexus/Fabric.hpp"
#include "MayaFlux/Nexus/Principals/Emitter.hpp"
#include "MayaFlux/Nexus/Principals/Locus.hpp"
#include "MayaFlux/Nexus/Tapestry.hpp"

#include <atomic>
#include <optional>

namespace game {

/**
 * @brief The game view's eye: a Nexus Locus, moved by keys and the mouse through Fabric wirings.
 *
 * WASD and QE move, right-drag looks, scroll dollies. The eye stays inside a box, and above a
 * floor height in it until B lets it under. B again closes the floor; an eye already under it
 * can then only rise.
 */
class Camera {
public:
    /**
     * @param bounds Where the eye may go, its bottom under the floor.
     * @param floor  The lowest the eye goes while the floor is closed.
     */
    Camera(const std::shared_ptr<MayaFlux::Core::Window>& window, const MayaFlux::Kinesis::NavigationConfig& config,
        const MayaFlux::Kinesis::AABB3D& bounds, float floor)
        : m_tapestry(MayaFlux::create_tapestry())
        , m_fabric(m_tapestry->create_fabric("camera"))
        , m_locus(std::make_shared<MayaFlux::Nexus::Locus>(config, 1.0F,
              [](const MayaFlux::Nexus::PerceptionContext&) {}, [](const MayaFlux::Nexus::InfluenceContext&) {}))
    {
        namespace Kinesis = MayaFlux::Kinesis;
        namespace Nexus = MayaFlux::Nexus;
        using MayaFlux::IO::Keys;
        using Nav = Kinesis::NavigationState;

        auto* nav = &m_locus->nav();
        auto under = std::make_shared<std::atomic<bool>>(false);
        nav->eye_constraint = [nav, under, bounds, floor](glm::vec3 eye) {
            const float lowest = under->load() ? bounds.min.y : std::max(bounds.min.y, std::min(floor, nav->eye.y));
            return glm::clamp(eye, glm::vec3(bounds.min.x, lowest, bounds.min.z), bounds.max);
        };
        m_fabric->wire(m_locus).every(MayaFlux::Vruta::ProcessingToken::FRAME_ACCURATE).finalise();

        const auto hold = [&](Keys key, bool Nav::*held) {
            m_fabric->wire(std::make_shared<Nexus::Emitter>([nav, held](const Nexus::InfluenceContext&) { nav->*held = true; }))
                .on(window, key, true, [nav, held] { nav->*held = false; })
                .finalise();
        };
        hold(Keys::W, &Nav::forward_held);
        hold(Keys::S, &Nav::back_held);
        hold(Keys::A, &Nav::left_held);
        hold(Keys::D, &Nav::right_held);
        hold(Keys::Q, &Nav::down_held);
        hold(Keys::E, &Nav::up_held);

        auto last = std::make_shared<std::optional<glm::vec2>>();
        m_fabric->wire(std::make_shared<Nexus::Emitter>([nav, last](const Nexus::InfluenceContext& ctx) {
                    if (ctx.cursor_pos && *last) {
                        const glm::vec2 moved = *ctx.cursor_pos - **last;
                        Kinesis::apply_mouse_delta(*nav, moved.x, moved.y);
                    }
                    *last = ctx.cursor_pos;
                }))
            .on(window, MayaFlux::IO::MouseButtons::Right, true, [last](double, double) { last->reset(); })
            .finalise();

        m_fabric->wire(std::make_shared<Nexus::Emitter>())
            .on_scroll(window, [nav](double, double dy) { Kinesis::apply_scroll(*nav, static_cast<float>(dy)); })
            .finalise();

        m_fabric->wire(std::make_shared<Nexus::Emitter>([under](const Nexus::InfluenceContext&) {
                    const bool open = !under->load();
                    under->store(open);
                    MF_PRINT("camera: {}", open ? "free to go under the floor" : "kept above the floor");
                }))
            .on(window, Keys::B)
            .finalise();
    }

    /** @brief The viewpoint the Locus drives. */
    [[nodiscard]] const std::shared_ptr<MayaFlux::Memory::OwnedCell<MayaFlux::Kinesis::Viewpoint>>& view() const
    {
        return m_locus->view();
    }

    /** @brief Draw @p renderer from the eye. */
    void add_view_target(std::shared_ptr<MayaFlux::Buffers::RenderProcessor> renderer)
    {
        m_locus->add_view_target(std::move(renderer));
    }

private:
    std::shared_ptr<MayaFlux::Nexus::Tapestry> m_tapestry;
    std::shared_ptr<MayaFlux::Nexus::Fabric> m_fabric;
    std::shared_ptr<MayaFlux::Nexus::Locus> m_locus;
};

}
