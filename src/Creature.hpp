#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/API/Rigs.hpp"
#include "MayaFlux/Kakshya/NDData/MeshSkin.hpp"
#include "MayaFlux/Kakshya/Semantic/MeshAssemblyContainer.hpp"
#include "MayaFlux/Kakshya/Semantic/MeshAssemblyMotionFrames.hpp"
#include "MayaFlux/Kinesis/GeometryPrimitives.hpp"
#include "MayaFlux/Kriya/MotionReflow.hpp"
#include "MayaFlux/Kriya/Realization/MeshAssemblyRealization.hpp"

#include <glm/gtc/quaternion.hpp>

#include <cstring>
#include <numbers>
#include <optional>
#include <vector>

namespace game {

/**
 * @brief A tentacle made in code: a tapered tube skinned to a chain of joints, with two motions.
 *
 * Nothing comes from a file. Kinesis::generate_tube makes the body, the joints and their
 * motions are keyed here, and each vertex follows the two joints nearest its height. It plays
 * through the same path as a loaded glTF: a MeshAssembly, realized and skinned on the GPU,
 * moved by a MotionReflow.
 */
class Creature {
public:
    static constexpr float height = 2.4F;
    static constexpr size_t joint_count = 6;

    /** @brief The motions, in the order they are added to the assembly. */
    enum Motion : size_t {
        Sway,
        Curl
    };

    /**
     * @brief Build the creature standing at @p position in @p window, seen through @p view.
     *        Call from a FRAME_ACCURATE task.
     * @return The creature, which lives until the process exits (like MazeView), or null if a step failed.
     */
    static Creature* open(
        const std::shared_ptr<MayaFlux::Core::Window>& window,
        const std::function<MayaFlux::Kinesis::ViewTransform()>& view,
        glm::vec3 position)
    {
        auto* creature = new Creature();
        if (!creature->build(window, view, position)) {
            MF_PRINT("creature: could not build it");
            return nullptr;
        }
        MF_PRINT("creature: swaying at ({:.1f}, {:.1f})", position.x, position.z);
        return creature;
    }

private:
    static constexpr float segment = height / static_cast<float>(joint_count - 1);
    static constexpr double sway_period = 4.0;
    static constexpr double curl_period = 3.0;
    static constexpr size_t keys_per_period = 48;

    std::shared_ptr<MayaFlux::Kakshya::MeshAssemblyContainer> m_assembly;
    std::shared_ptr<MayaFlux::Kriya::MeshAssemblyRealization> m_realization;
    std::shared_ptr<MayaFlux::Kriya::MotionReflow> m_reflow;

    bool build(const std::shared_ptr<MayaFlux::Core::Window>& window,
        const std::function<MayaFlux::Kinesis::ViewTransform()>& view,
        glm::vec3 position)
    {
        namespace Kakshya = MayaFlux::Kakshya;
        namespace Kriya = MayaFlux::Kriya;

        m_assembly = std::make_shared<Kakshya::MeshAssemblyContainer>();
        const auto root = m_assembly->add_placement({ .name = "creature", .local = glm::translate(glm::mat4(1.0F), position) });
        if (!root) {
            return false;
        }

        std::vector<uint64_t> joints;
        for (size_t j = 0; j < joint_count; ++j) {
            const auto row = m_assembly->add_placement({
                .name = "joint_" + std::to_string(j),
                .parent = j == 0 ? *root : joints.back(),
                .local = j == 0 ? glm::mat4(1.0F) : glm::translate(glm::mat4(1.0F), { 0.0F, segment, 0.0F }),
            });
            if (!row) {
                return false;
            }
            joints.push_back(*row);
        }

        auto body = make_body(joints);
        if (!body) {
            MF_PRINT("creature: the body mesh or its skin is not valid");
            return false;
        }
        const auto source = m_assembly->add_source(std::move(*body));
        if (!m_assembly->add_placement({ .name = "body", .parent = *root, .source = source })) {
            return false;
        }

        const auto sway_motion = make_motion("sway", joints, sway_period, [](size_t j, double phase) {
            const auto reach = static_cast<float>(j) / static_cast<float>(joint_count - 1);
            const float side = (0.10F + 0.22F * reach) * std::sin(static_cast<float>(phase) - 0.7F * static_cast<float>(j));
            const float lean = (0.05F + 0.12F * reach) * std::sin(static_cast<float>(phase) - 0.5F * static_cast<float>(j) + 1.3F);
            return glm::angleAxis(side, glm::vec3(0.0F, 0.0F, 1.0F)) * glm::angleAxis(lean, glm::vec3(1.0F, 0.0F, 0.0F));
        });
        const auto curl_motion = make_motion("curl", joints, curl_period, [](size_t j, double phase) {
            const auto amount = static_cast<float>(0.5 - 0.5 * std::cos(phase));
            const float bend = j == 0 ? 0.0F : 0.55F * amount;
            return glm::angleAxis(bend, glm::vec3(0.0F, 0.0F, 1.0F));
        });
        if (!sway_motion || !curl_motion || !m_assembly->add_motion(*sway_motion) || !m_assembly->add_motion(*curl_motion)) {
            MF_PRINT("creature: a motion's keys are not valid");
            return false;
        }

        m_realization = Kriya::MeshAssemblyRealization::create(m_assembly);
        if (!m_realization) {
            return false;
        }
        MayaFlux::present_realization(m_realization, [window](const Kriya::MeshAssemblyRealizedPart&, size_t) {
            return MayaFlux::Portal::Graphics::RenderConfig { .target_window = window };
        });
        for (const auto& part : m_realization->parts()) {
            if (part.network_buffer) {
                part.network_buffer->get_render_processor()->set_view_transform_source(
                    view, true, MayaFlux::Portal::Graphics::CullMode::NONE);
            }
        }

        const auto frames = Kakshya::MeshAssemblyMotionFrames::create(m_assembly, Sway);
        if (!frames) {
            return false;
        }
        m_reflow = Kriya::MotionReflowBuilder::sampling(frames, m_realization->placement_sink(), 0.0, MayaFlux::get_scheduler())
                       .course({ .in = 0.0, .out = sway_period, .edge = Kriya::CourseEdge::Loop })
                       .start();
        return m_reflow != nullptr;
    }

    /**
     * @brief The tube, coloured from a deep base to a pale tip with darker rings, and its skin.
     *
     * The mesh shader has no lighting, so a fixed light is baked into the colours.
     */
    static std::optional<MayaFlux::Kakshya::MeshAssemblySource> make_body(const std::vector<uint64_t>& joints)
    {
        namespace Kakshya = MayaFlux::Kakshya;

        std::vector<glm::vec3> spine;
        for (int i = 0; i <= 32; ++i) {
            spine.emplace_back(0.0F, height * static_cast<float>(i) / 32.0F, 0.0F);
        }
        const auto tube = MayaFlux::Kinesis::generate_tube(spine, [](float t) { return 0.04F + 0.26F * std::pow(1.0F - t, 0.8F); }, 16, true);

        const auto* bytes = std::get_if<std::vector<uint8_t>>(&tube.vertex_variant);
        const auto* indices = std::get_if<std::vector<uint32_t>>(&tube.index_variant);
        if (!bytes || !indices || bytes->size() % sizeof(Kakshya::MeshVertex) != 0) {
            return std::nullopt;
        }
        std::vector<Kakshya::MeshVertex> vertices(bytes->size() / sizeof(Kakshya::MeshVertex));
        std::memcpy(vertices.data(), bytes->data(), bytes->size());

        Kakshya::MeshSkin skin;
        skin.joints = joints;
        for (size_t j = 0; j < joint_count; ++j) {
            skin.inverse_bind.push_back(glm::translate(glm::mat4(1.0F), { 0.0F, -segment * static_cast<float>(j), 0.0F }));
        }

        const glm::vec3 light = glm::normalize(glm::vec3(0.4F, 0.8F, 0.45F));
        const glm::vec3 base_color(0.22F, 0.10F, 0.34F);
        const glm::vec3 tip_color(0.62F, 0.90F, 0.86F);
        for (auto& vertex : vertices) {
            const float t = std::clamp(vertex.position.y / height, 0.0F, 1.0F);
            const float ring = 0.82F + 0.18F * std::cos(t * 14.0F * std::numbers::pi_v<float>);
            const float shade = 0.5F + 0.5F * std::max(glm::dot(vertex.normal, light), 0.0F);
            vertex.color = glm::vec4(glm::mix(base_color, tip_color, t) * ring * shade, 1.0F);

            const float s = t * static_cast<float>(joint_count - 1);
            const auto lower = std::min(static_cast<uint32_t>(s), static_cast<uint32_t>(joint_count - 2));
            const float upper_weight = std::clamp(s - static_cast<float>(lower), 0.0F, 1.0F);
            skin.influence_joints.emplace_back(lower, lower + 1, 0, 0);
            skin.influence_weights.emplace_back(1.0F - upper_weight, upper_weight, 0.0F, 0.0F);
        }

        auto mesh = Kakshya::MeshData::from_vertices(vertices, *indices);
        if (!mesh || !skin.valid(vertices.size())) {
            return std::nullopt;
        }

        Kakshya::MeshAssemblySource source { .geometry = std::move(*mesh), .name = "body" };
        source.facets.set(std::shared_ptr<const Kakshya::MeshSkin>(std::make_shared<Kakshya::MeshSkin>(std::move(skin))));
        return source;
    }

    /**
     * @brief One rotation channel per joint, keyed over one period so it loops without a seam.
     * @param rotation The joint's rotation at a phase from 0 to 2π.
     */
    template <typename Rotation>
    static std::optional<MayaFlux::Kakshya::MeshAssemblyMotion> make_motion(
        std::string name, const std::vector<uint64_t>& joints, double period, Rotation rotation)
    {
        namespace Kinesis = MayaFlux::Kinesis;

        const auto times = Kinesis::TimeSampling::uniform(period / static_cast<double>(keys_per_period));
        if (!times) {
            return std::nullopt;
        }

        MayaFlux::Kakshya::MeshAssemblyMotion motion { .name = std::move(name), .channels = {} };
        for (size_t j = 0; j < joints.size(); ++j) {
            std::vector<glm::vec4> values;
            for (size_t k = 0; k <= keys_per_period; ++k) {
                const double phase = 2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(keys_per_period);
                const glm::quat q = rotation(j, phase);
                values.emplace_back(q.x, q.y, q.z, q.w);
            }
            auto keys = Kinesis::TimedValues<glm::vec4>::create({
                .times = *times,
                .values = std::move(values),
                .kind = Kinesis::ValueKind::Rotation,
            });
            if (!keys) {
                return std::nullopt;
            }
            motion.channels.push_back({ .row = joints[j], .rotation = std::move(*keys) });
        }
        return motion;
    }
};

}
