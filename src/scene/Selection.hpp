#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/Kriya/InputEvents.hpp"
#include "MayaFlux/Vruta/EventManager.hpp"

#include "MazeView.hpp"
#include "Scene.hpp"

#include <optional>

namespace game {

/**
 * @brief Which asset is chosen in the game view, and its name floating above it.
 *
 * A left click in the window chooses the asset under the pointer: the first one the ray from
 * the eye meets before any wall. A click on nothing leaves the choice as it was. The name is
 * a Forma Surface placed in the scene, turned to face the eye, and takes no input, so the
 * camera keeps every press.
 */
class Selection {
public:
    /**
     * @brief Start choosing in the window @p scene draws into. Call from a FRAME_ACCURATE task.
     * @return The selection, kept in MayaFlux's persistent store until End() as "selection", like the scene.
     */
    static Selection* open(Scene& scene, const MazeView& maze)
    {
        return MayaFlux::store("selection", std::shared_ptr<Selection>(new Selection(scene, maze))).get();
    }

    [[nodiscard]] std::optional<size_t> selected() const { return m_selected; }

    /** @brief Choose @p asset, or nothing. A removed asset counts as nothing. */
    void select(std::optional<size_t> asset)
    {
        const auto* found = asset ? m_scene.asset(*asset) : nullptr;
        m_selected = found ? asset : std::nullopt;
        if (found) {
            m_label->set_text(found->name, label_params());
        }
        m_surface.layer().set_visible(m_label_id, found != nullptr);
    }

private:
    Selection(Scene& scene, const MazeView& maze)
        : m_scene(scene)
        , m_maze(maze)
        , m_surface(MayaFlux::Portal::Forma::create_surface({ .window = maze.window(), .name = "selection" }))
    {
        namespace Forma = MayaFlux::Portal::Forma;

        auto target = Forma::slice(m_surface, MayaFlux::Portal::Graphics::PrimitiveTopology::TRIANGLE_LIST);
        m_label = std::make_shared<Forma::Element>();
        m_label->with_name("selected_name").with_bounds(label_bounds).with_buffer(target).with_text("", label_params(), label_bounds);
        m_label->interactive = false;
        m_label_id = m_surface.add(*m_label);
        m_surface.layer().set_visible(m_label_id, false);
        m_surface.set_placement([this] { return m_scene.view_transform(); }, [this] { return label_placement(); });

        const auto& window = maze.window();
        MayaFlux::get_event_manager()->add_event(
            std::make_shared<MayaFlux::Vruta::Event>(MayaFlux::Kriya::mouse_pressed(window, MayaFlux::IO::MouseButtons::Left,
                [this](double x, double y) { press(x, y); })),
            "game_view_select");
    }

    /** The label in its plane, in NDC units: 5 to 1, as its 480 by 96 pixels are. */
    static constexpr MayaFlux::Kinesis::AABB2D label_bounds { .min = { -1.0F, -0.2F }, .max = { 1.0F, 0.2F } };
    /** Metres per NDC unit of the label, so it is 1.2 m wide in the scene. */
    static constexpr float label_scale = 0.6F;

    Scene& m_scene;
    const MazeView& m_maze;
    MayaFlux::Portal::Forma::Surface m_surface;
    std::shared_ptr<MayaFlux::Portal::Forma::Element> m_label;
    uint32_t m_label_id {};
    std::optional<size_t> m_selected;

    static MayaFlux::Portal::Text::PressParams label_params()
    {
        return {
            .color = { 0.807F, 0.807F, 0.776F, 1.0F },
            .background = { 0.010F, 0.011F, 0.014F, 0.75F },
            .render_bounds = { 480, 96 },
            .budget_h = 96,
        };
    }

    /** Half a metre over the chosen asset's rest pose, turned the way the eye is. */
    [[nodiscard]] glm::mat4 label_placement() const
    {
        const auto top = m_selected ? m_scene.top_of(*m_selected) : std::nullopt;
        if (!top) {
            return glm::scale(glm::mat4(1.0F), glm::vec3(0.0F));
        }
        const glm::mat4 eye = m_scene.eye();
        glm::mat4 turn(1.0F);
        for (int axis = 0; axis < 3; ++axis) {
            turn[axis] = glm::vec4(glm::normalize(glm::vec3(eye[axis])), 0.0F);
        }
        return glm::translate(glm::mat4(1.0F), *top + glm::vec3(0.0F, 0.5F, 0.0F)) * turn * glm::scale(glm::mat4(1.0F), glm::vec3(label_scale));
    }

    void press(double x, double y)
    {
        const auto& window = m_maze.window();
        if (window->get_event_source().pointer_claimed(0)) {
            return;
        }
        const auto& state = window->get_state();
        const auto ray = MayaFlux::Kinesis::screen_to_ray(x, y, state.current_width, state.current_height, m_scene.view_transform());
        const auto hit = m_scene.pick(ray, m_maze.nearest_wall(ray).value_or(std::numeric_limits<float>::max()));
        if (hit) {
            select(hit->asset);
            MF_PRINT("selection: {}", m_scene.asset(hit->asset)->name);
        }
    }
};

}
