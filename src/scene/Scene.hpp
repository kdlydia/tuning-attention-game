#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/API/Rigs.hpp"
#include "MayaFlux/API/Stagecraft.hpp"
#include "MayaFlux/Buffers/Network/RecordedGeometryBuffer.hpp"
#include "MayaFlux/Nexus/State/Bindings.hpp"
#include "MayaFlux/Nexus/State/Orchestrator.hpp"
#include "MayaFlux/Nexus/Tapestry.hpp"

#include "Player.hpp"

#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace game {

/**
 * @brief The things placed in the game world, shared by the game and the editor.
 *
 * One Nexus Tapestry holds the entities. A StateOrchestrator turns a model or Alembic file into
 * drawn parts and the entities that follow them (cameras become Loci, lights Emitters), and names
 * what it makes in the Bindings, so a saved state can refer back to the file. Everything is drawn
 * into one window from one viewpoint, and each asset gets a Player for its motions.
 */
class Scene {
public:
    struct Asset {
        std::string name;
        std::filesystem::path file;
        std::shared_ptr<MayaFlux::Kakshya::MeshAssemblyContainer> assembly;
        std::shared_ptr<MayaFlux::Kriya::MeshAssemblyRealization> realization;
        MayaFlux::Nexus::StateOrchestrator::Rows entities;
        std::shared_ptr<Player> player;
        glm::vec3 size {}; ///< Of the rest pose as placed, in metres.
    };

    using View = std::shared_ptr<MayaFlux::Memory::OwnedCell<MayaFlux::Kinesis::Viewpoint>>;

    /**
     * @brief A scene drawn into @p window from @p view. Call from a FRAME_ACCURATE task.
     * @return The scene, which lives until the process exits, like MazeView.
     */
    static Scene* open(const std::shared_ptr<MayaFlux::Core::Window>& window, const View& view)
    {
        return new Scene(window, view);
    }

    /**
     * @brief Load a model or Alembic file and stand it at @p at. Call from a FRAME_ACCURATE task.
     * @param at Where it stands. Without one, where the view is looking (looked_at()).
     * @param height How tall to make a file whose size can't be metres (see fit()).
     * @return The asset's index, or nullopt if the file gave nothing to draw.
     */
    std::optional<size_t> load(const std::filesystem::path& file, std::optional<glm::vec3> at = std::nullopt, float height = 1.6F)
    {
        auto [assembly, realization, entities] = m_orchestrator->orchestrate(file.string());
        if (!assembly || !realization) {
            MF_PRINT("scene: {} gave nothing to draw", file.string());
            return std::nullopt;
        }

        const size_t shown = MayaFlux::present_realization(realization, [this](const MayaFlux::Kriya::MeshAssemblyRealizedPart&, size_t) {
            return MayaFlux::Portal::Graphics::RenderConfig { .target_window = m_window };
        });
        MF_PRINT("scene: {} shows {} buffers in {} parts", file.filename().string(), shown, realization->parts().size());
        for (const auto& part : realization->parts()) {
            view_from(part.network_buffer);
            view_from(part.instance_buffer);
            for (const auto& buffer : part.spatial_buffers) {
                view_from(buffer);
            }
        }

        const auto [placement, size] = fit(assembly, at.value_or(looked_at()), height);
        auto player = Player::create(assembly, realization, placement);
        if (!player) {
            MF_PRINT("scene: {} loaded, but its placements can't be played", file.string());
            return std::nullopt;
        }

        Asset asset {
            .name = unique_name(file.stem().string()),
            .file = file,
            .assembly = std::move(assembly),
            .realization = std::move(realization),
            .entities = std::move(entities),
            .player = std::move(player),
            .size = size,
        };
        MF_PRINT("scene: loaded {} from {}: {} entities, {} motions",
            asset.name, file.string(), asset.entities.size(), asset.player->motions().size());
        m_assets.emplace_back(std::move(asset));
        return m_assets.size() - 1;
    }

    /**
     * @brief Take an asset out of the scene. Call from a FRAME_ACCURATE task.
     *
     * Its motions stop, its buffers and node networks stop drawing, its entities leave the
     * Fabric and the names the orchestrator gave its buffers leave the Bindings. Every other
     * asset keeps its index.
     * @return False if there is no asset at @p index.
     */
    bool remove(size_t index)
    {
        auto* found = asset(index);
        if (!found) {
            return false;
        }

        found->player.reset();
        for (const auto& part : found->realization->parts()) {
            std::vector<std::shared_ptr<MayaFlux::Buffers::VKBuffer>> buffers(part.spatial_buffers.begin(), part.spatial_buffers.end());
            buffers.emplace_back(part.network_buffer);
            buffers.emplace_back(part.instance_buffer);
            for (const auto& buffer : buffers) {
                if (!buffer) {
                    continue;
                }
                forget_names_of(buffer);
                MayaFlux::unregister_graphics_buffer(buffer);
            }
            if (part.network) {
                MayaFlux::unregister_node_network(part.network, MayaFlux::Nodes::ProcessingToken::VISUAL_RATE);
            }
            if (part.instances) {
                MayaFlux::unregister_node_network(part.instances, MayaFlux::Nodes::ProcessingToken::VISUAL_RATE);
            }
        }
        if (const auto& fabric = m_orchestrator->fabric()) {
            for (const auto& [row, member] : found->entities) {
                fabric->remove(std::visit([](const auto& principal) { return principal->id(); }, member));
            }
        }

        MF_PRINT("scene: removed {}", found->name);
        m_assets[index].reset();
        return true;
    }

    /**
     * @brief Where the view's line of sight meets the floor (y = 0), or a few units ahead
     *        of the eye, on the floor, when it looks level or up.
     */
    [[nodiscard]] glm::vec3 looked_at() const
    {
        const glm::mat4 placement = m_view->read().first;
        const glm::vec3 eye(placement[3]);
        const glm::vec3 forward = -glm::normalize(glm::vec3(placement[2]));
        if (forward.y < -0.05F) {
            return eye + forward * (-eye.y / forward.y);
        }
        constexpr float ahead = 5.0F;
        return { eye.x + forward.x * ahead, 0.0F, eye.z + forward.z * ahead };
    }

    /** @brief One more than the highest asset index given so far, removed ones included. */
    [[nodiscard]] size_t slots() const { return m_assets.size(); }

    /** @brief The asset at @p index, or null if there never was one or it was removed. */
    [[nodiscard]] Asset* asset(size_t index)
    {
        return index < m_assets.size() && m_assets[index] ? &*m_assets[index] : nullptr;
    }

    [[nodiscard]] const std::shared_ptr<MayaFlux::Nexus::Tapestry>& tapestry() const { return m_tapestry; }
    [[nodiscard]] const std::shared_ptr<MayaFlux::Nexus::State::Bindings>& bindings() const { return m_bindings; }

private:
    Scene(const std::shared_ptr<MayaFlux::Core::Window>& window, const View& view)
        : m_window(window)
        , m_view(view)
        , m_tapestry(MayaFlux::create_tapestry())
        , m_orchestrator(MayaFlux::create_orchestrator(m_tapestry))
        , m_bindings(std::make_shared<MayaFlux::Nexus::State::Bindings>())
    {
        m_bindings->windows["game"] = m_window;
        m_orchestrator->set_bindings(m_bindings);
    }

    std::shared_ptr<MayaFlux::Core::Window> m_window;
    View m_view;
    std::shared_ptr<MayaFlux::Nexus::Tapestry> m_tapestry;
    std::shared_ptr<MayaFlux::Nexus::StateOrchestrator> m_orchestrator;
    std::shared_ptr<MayaFlux::Nexus::State::Bindings> m_bindings;
    std::vector<std::optional<Asset>> m_assets;

    /** @brief Drop every name the Bindings give @p buffer, with the file each was read from. */
    void forget_names_of(const std::shared_ptr<MayaFlux::Buffers::VKBuffer>& buffer)
    {
        auto& names = m_bindings->buffers;
        for (auto it = names.begin(); it != names.end();) {
            if (it->second == buffer) {
                m_bindings->assets.erase(it->first);
                it = names.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * @brief Draw @p buffer from the scene's view, with no face culling: files often mark
     *        materials double-sided, and their faces then wind either way.
     */
    template <typename Buffer>
    void view_from(const std::shared_ptr<Buffer>& buffer) const
    {
        if (buffer && buffer->get_render_processor()) {
            buffer->get_render_processor()->set_view_cell(m_view, true, MayaFlux::Portal::Graphics::CullMode::NONE);
        }
    }

    [[nodiscard]] std::string unique_name(const std::string& stem) const
    {
        std::string name = stem.empty() ? "asset" : stem;
        for (int n = 2; std::ranges::any_of(m_assets, [&name](const std::optional<Asset>& a) { return a && a->name == name; }); ++n) {
            name = stem + " " + std::to_string(n);
        }
        return name;
    }

    struct Fit {
        glm::mat4 placement;
        glm::vec3 size;
    };

    /**
     * @brief The placement that stands the rest pose on @p at, centred over it, and the size it stands at.
     *
     * glTF is in metres, as the maze is, so a rest pose between 1 cm and 10 m tall keeps its
     * size. A file too tall for metres is read as centimetres, then millimetres (the Fox
     * sample is in centimetres), and one that is neither is scaled to @p height. The bounds
     * come from every mesh row's vertices placed at rest.
     */
    static Fit fit(const std::shared_ptr<const MayaFlux::Kakshya::MeshAssemblyContainer>& assembly, glm::vec3 at, float height)
    {
        namespace Kakshya = MayaFlux::Kakshya;

        const Fit unfitted { .placement = glm::translate(glm::mat4(1.0F), at), .size = glm::vec3(0.0F) };
        const auto rest = Kakshya::MeshAssemblyRestFrames::create(assembly);
        const auto frame = rest ? rest->sample(0.0) : nullptr;
        if (!frame) {
            return unfitted;
        }

        glm::vec3 lo(std::numeric_limits<float>::max());
        glm::vec3 hi(std::numeric_limits<float>::lowest());
        const auto& sources = assembly->get_data().sources;
        for (size_t row = 0; row < assembly->get_total_elements(); ++row) {
            const auto placement = assembly->placement(row);
            if (!placement || !placement->source || *placement->source >= sources.size()) {
                continue;
            }
            const auto* mesh = std::get_if<Kakshya::MeshData>(&sources[*placement->source].geometry);
            const auto* bytes = mesh ? std::get_if<std::vector<uint8_t>>(&mesh->vertex_variant) : nullptr;
            if (!bytes) {
                continue;
            }
            const size_t stride = mesh->layout.stride_bytes != 0 ? mesh->layout.stride_bytes : sizeof(Kakshya::MeshVertex);
            const glm::mat4 world = row < frame->world.size() ? frame->world[row] : glm::mat4(1.0F);
            for (size_t offset = 0; offset + sizeof(glm::vec3) <= bytes->size(); offset += stride) {
                glm::vec3 position;
                std::memcpy(&position, bytes->data() + offset, sizeof(position));
                const glm::vec3 placed(world * glm::vec4(position, 1.0F));
                lo = glm::min(lo, placed);
                hi = glm::max(hi, placed);
            }
        }

        if (!(hi.y > lo.y)) {
            return unfitted;
        }
        constexpr float smallest = 0.01F;
        constexpr float tallest = 10.0F;
        const float tall = hi.y - lo.y;
        float scale = height / tall;
        std::string read_as = std::format("no unit fits, scaled to {:.2f} m tall", height);
        for (const auto& [unit, name] : { std::pair { 1.0F, "metres" }, std::pair { 0.01F, "centimetres" }, std::pair { 0.001F, "millimetres" } }) {
            if (tall * unit >= smallest && tall * unit <= tallest) {
                scale = unit;
                read_as = std::string("read as ") + name;
                break;
            }
        }
        const glm::vec3 size = (hi - lo) * scale;
        MF_PRINT("scene: rest pose is {:.2f} x {:.2f} x {:.2f} m, {}", size.x, size.y, size.z, read_as);
        const glm::vec3 foot((lo.x + hi.x) / 2, lo.y, (lo.z + hi.z) / 2);
        return {
            .placement = glm::translate(glm::mat4(1.0F), at) * glm::scale(glm::mat4(1.0F), glm::vec3(scale)) * glm::translate(glm::mat4(1.0F), -foot),
            .size = size,
        };
    }
};

}
