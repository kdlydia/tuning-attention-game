#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/API/Rigs.hpp"
#include "MayaFlux/API/Stagecraft.hpp"
#include "MayaFlux/Buffers/Network/RecordedGeometryBuffer.hpp"
#include "MayaFlux/Kinesis/Radiometry/Prefilter.hpp"
#include "MayaFlux/Kinesis/Spatial/BoxHierarchy.hpp"
#include "MayaFlux/Kinesis/Spatial/HitTest.hpp"
#include "MayaFlux/Nexus/Fabric.hpp"
#include "MayaFlux/Nexus/Pheme/Influence.hpp"
#include "MayaFlux/Nexus/Pheme/InfluenceList.hpp"
#include "MayaFlux/Nexus/Principals/Emitter.hpp"
#include "MayaFlux/Nexus/State/Bindings.hpp"
#include "MayaFlux/Nexus/State/Orchestrator.hpp"
#include "MayaFlux/Nexus/Tapestry.hpp"

#include "Player.hpp"

#include <bit>
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
 *
 * Light is one InfluenceList, shared with the maze: the sky as its surroundings, then Nexus
 * Emitters writing into it, the sun first and then any lights a loaded file has. Mesh parts draw
 * with the game's scene_lit shaders (data/shaders), which are MayaFlux's influence_shade with
 * back faces lit as fronts and alpha masks cut out. Each part takes its material's roughness,
 * metalness, alpha mode and sidedness from the file.
 */
class Scene {
public:
    /**
     * @brief An asset's rest pose as triangles, where its rows rest but before the asset's
     *        own placement, with a BoxHierarchy for finding what a ray meets.
     */
    struct Shape {
        std::vector<glm::vec3> points;
        std::vector<uint32_t> triangles;
        std::shared_ptr<const MayaFlux::Kinesis::BoxHierarchy> boxes;
        glm::vec3 lo { 0.0F };
        glm::vec3 hi { 0.0F };
    };

    struct Asset {
        std::string name;
        std::filesystem::path file;
        std::shared_ptr<MayaFlux::Kakshya::MeshAssemblyContainer> assembly;
        std::shared_ptr<MayaFlux::Kriya::MeshAssemblyRealization> realization;
        MayaFlux::Nexus::StateOrchestrator::Rows entities;
        std::shared_ptr<Player> player;
        std::shared_ptr<const Shape> shape; ///< Null when the file has no mesh at rest.
        glm::vec3 size {}; ///< Of the rest pose as placed, in metres.
    };

    /** @brief What a ray meets: an asset and how far along the ray. */
    struct Hit {
        size_t asset;
        float distance;
    };

    using View = std::shared_ptr<MayaFlux::Memory::OwnedCell<MayaFlux::Kinesis::Viewpoint>>;

    /**
     * @brief A scene drawn into @p window from @p view. Call from a FRAME_ACCURATE task.
     * @param lights  The list everything in the window is lit by. The scene puts the sky and
     *                the sun in it, and the lights of the files it loads.
     * @param shaders The folder holding the compiled scene_lit shaders.
     * @return The scene, kept in MayaFlux's persistent store until End() as "scene", like MazeView.
     */
    static Scene* open(const std::shared_ptr<MayaFlux::Core::Window>& window, const View& view,
        std::shared_ptr<MayaFlux::Nexus::InfluenceList> lights, std::filesystem::path shaders)
    {
        return MayaFlux::store("scene", std::shared_ptr<Scene>(new Scene(window, view, std::move(lights), std::move(shaders)))).get();
    }

    /**
     * @brief Add a light: an Emitter at @p position whose @p intensity falls by the inverse square
     *        of the distance, to nothing at @p radius.
     */
    std::shared_ptr<MayaFlux::Nexus::Emitter> add_light(glm::vec3 position, glm::vec3 color, float intensity, float radius)
    {
        auto light = std::make_shared<MayaFlux::Nexus::Emitter>();
        light->set(MayaFlux::Nexus::PlacementControls { .position = position });
        light->set(MayaFlux::Nexus::EmitterControls { .intensity = intensity, .radius = radius, .color = color });
        return keep_light(light);
    }

    /**
     * @brief Add sunlight: light arriving along @p direction everywhere, @p irradiance being what
     *        a surface square to it receives.
     */
    std::shared_ptr<MayaFlux::Nexus::Emitter> add_sun(glm::vec3 direction, glm::vec3 color, float irradiance)
    {
        namespace Nexus = MayaFlux::Nexus;

        const glm::vec3 along = glm::normalize(direction);
        const glm::vec3 up = std::abs(along.y) > 0.99F ? glm::vec3(0.0F, 0.0F, 1.0F) : glm::vec3(0.0F, 1.0F, 0.0F);
        auto sun = std::make_shared<Nexus::Emitter>();
        sun->set(Nexus::PlacementControls { .position = -along * 100.0F, .orientation = glm::quatLookAt(along, up) });
        sun->set(Nexus::EmitterControls { .intensity = irradiance, .color = color, .shape = Nexus::SourceShape::Directional });
        return keep_light(sun);
    }

    [[nodiscard]] const std::vector<std::shared_ptr<MayaFlux::Nexus::Emitter>>& lights() const { return m_lights; }

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

        const size_t shown = MayaFlux::present_realization(realization, [this](const MayaFlux::Kriya::MeshAssemblyRealizedPart& part, size_t) {
            MayaFlux::Portal::Graphics::RenderConfig config { .target_window = m_window };
            if (part.network_buffer) {
                config.fragment_shader = lit_shader(part);
            }
            return config;
        });
        MF_PRINT("scene: {} shows {} buffers in {} parts", file.filename().string(), shown, realization->parts().size());
        for (const auto& part : realization->parts()) {
            const auto surface = surface_of(*assembly, part);
            view_from(part.network_buffer, surface.double_sided);
            view_from(part.instance_buffer, surface.double_sided);
            for (const auto& buffer : part.spatial_buffers) {
                view_from(buffer, true);
            }
            if (part.network_buffer && !lit_shader(part).empty()) {
                const auto& renderer = part.network_buffer->get_render_processor();
                const float cutoff = surface.alpha == MayaFlux::Kakshya::MeshAssemblyAlpha::Opaque ? 0.0F : surface.alpha_cutoff;
                renderer->set_specialization_constant(9, std::bit_cast<uint32_t>(surface.roughness));
                renderer->set_specialization_constant(10, std::bit_cast<uint32_t>(surface.metallic));
                renderer->set_specialization_constant(16, std::bit_cast<uint32_t>(cutoff));
                m_light_list->bind(renderer);
            }
        }

        // The file's lights join the list. They are lit once, where they rest.
        for (const auto& [row, light] : MayaFlux::Nexus::StateOrchestrator::rows_as<MayaFlux::Nexus::Emitter>(entities)) {
            light->set_influence<MayaFlux::Nexus::InfluenceBlock>(MayaFlux::Nexus::InfluenceBlock::from, m_light_list->target());
            if (const auto& fabric = m_orchestrator->fabric()) {
                fabric->fire(light->id());
            }
            MF_PRINT("scene: {} brings a light at row {}", file.filename().string(), row);
        }

        auto shape = rest_shape(assembly);
        const auto [placement, size] = fit(shape.get(), at.value_or(looked_at()), height);
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
            .shape = std::move(shape),
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

    /** @brief Where the eye is and which way it faces (-Z forward), as a placement. */
    [[nodiscard]] glm::mat4 eye() const { return m_view->read().first; }

    /** @brief The view as the window shows it now. */
    [[nodiscard]] MayaFlux::Kinesis::ViewTransform view_transform() const
    {
        const auto [placement, frustum] = m_view->read();
        const auto& state = m_window->get_state();
        const float aspect = state.current_height > 0 ? static_cast<float>(state.current_width) / static_cast<float>(state.current_height) : 1.0F;
        return MayaFlux::Kinesis::view_from_frustum(placement, frustum, aspect);
    }

    /** @brief The ray from the eye through a point of the window, in NDC with +y up. */
    [[nodiscard]] MayaFlux::Kinesis::Ray ray_through(glm::vec2 ndc) const
    {
        return MayaFlux::Kinesis::ray_through(ndc, view_transform());
    }

    /**
     * @brief The asset @p ray meets first, nearer than @p limit, by its rest pose where its
     *        player places it now. A motion that moves far from rest is found where it rests.
     */
    [[nodiscard]] std::optional<Hit> pick(const MayaFlux::Kinesis::Ray& ray, float limit = std::numeric_limits<float>::max()) const
    {
        std::optional<Hit> best;
        for (size_t i = 0; i < m_assets.size(); ++i) {
            const auto& asset = m_assets[i];
            if (!asset || !asset->shape || !asset->shape->boxes || !asset->player) {
                continue;
            }
            const auto& shape = *asset->shape;
            const auto local = MayaFlux::Kinesis::transform_ray(glm::inverse(asset->player->placement()), ray);
            const auto hit = MayaFlux::Kinesis::ray_triangles_parameter(local, *shape.boxes, shape.points, shape.triangles);
            if (hit && hit->second.x < limit && (!best || hit->second.x < best->distance)) {
                best = Hit { .asset = i, .distance = hit->second.x };
            }
        }
        return best;
    }

    /** @brief The middle of the top of an asset's rest pose where its player places it now, or nullopt. */
    [[nodiscard]] std::optional<glm::vec3> top_of(size_t index) const
    {
        const Asset* asset = index < m_assets.size() && m_assets[index] ? &*m_assets[index] : nullptr;
        if (!asset || !asset->shape || !asset->player) {
            return std::nullopt;
        }
        const auto& shape = *asset->shape;
        const glm::vec3 top((shape.lo.x + shape.hi.x) / 2, shape.hi.y, (shape.lo.z + shape.hi.z) / 2);
        return glm::vec3(asset->player->placement() * glm::vec4(top, 1.0F));
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
    Scene(const std::shared_ptr<MayaFlux::Core::Window>& window, const View& view,
        std::shared_ptr<MayaFlux::Nexus::InfluenceList> lights, std::filesystem::path shaders)
        : m_window(window)
        , m_view(view)
        , m_shaders(std::move(shaders))
        , m_tapestry(MayaFlux::create_tapestry())
        , m_orchestrator(MayaFlux::create_orchestrator(m_tapestry))
        , m_bindings(std::make_shared<MayaFlux::Nexus::State::Bindings>())
        , m_light_list(std::move(lights))
        , m_light_fabric(m_tapestry->create_fabric("lights"))
    {
        m_bindings->windows["game"] = m_window;
        m_orchestrator->set_bindings(m_bindings);

        m_light_list->set_surround(sky());
        add_sun({ 0.45F, -0.75F, -0.5F }, { 1.0F, 0.94F, 0.84F }, 2.5F);
    }

    /**
     * @brief Radiance from all around: a pale horizon rising to a blue zenith, and a dim
     *        brown ground below, prefiltered for every roughness. CPU work, done once.
     */
    static MayaFlux::Kinesis::SurroundRadiance sky()
    {
        namespace Kinesis = MayaFlux::Kinesis;

        constexpr uint32_t width = 64;
        constexpr uint32_t height = 32;
        const glm::vec3 zenith(0.16F, 0.26F, 0.52F);
        const glm::vec3 horizon(0.52F, 0.56F, 0.62F);
        const glm::vec3 ground(0.10F, 0.085F, 0.07F);
        std::vector<float> pixels;
        pixels.reserve(static_cast<size_t>(width) * height * 3);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const glm::vec3 d = Kinesis::grid_direction({ (static_cast<float>(x) + 0.5F) / width, (static_cast<float>(y) + 0.5F) / height });
                const glm::vec3 c = d.y >= 0.0F
                    ? glm::mix(horizon, zenith, std::sqrt(d.y))
                    : glm::mix(horizon * 0.5F, ground, std::min(-d.y * 4.0F, 1.0F));
                pixels.insert(pixels.end(), { c.x, c.y, c.z });
            }
        }
        return Kinesis::prefilter_surround({ .pixels = pixels, .width = width, .height = height, .channels = 3 }, 128);
    }

    /** @brief Write @p light into the list, wire it so it follows its controls, and keep it. */
    std::shared_ptr<MayaFlux::Nexus::Emitter> keep_light(const std::shared_ptr<MayaFlux::Nexus::Emitter>& light)
    {
        light->set_influence<MayaFlux::Nexus::InfluenceBlock>(MayaFlux::Nexus::InfluenceBlock::from, m_light_list->target());
        m_light_fabric->wire(light).every(0.25, MayaFlux::Vruta::ProcessingToken::FRAME_ACCURATE).finalise();
        m_light_fabric->fire(light->id());
        m_lights.push_back(light);
        return light;
    }

    /**
     * @brief The material of the first of @p part's rows that names one, or a rough, matte,
     *        opaque, double-sided surface when none does.
     */
    static MayaFlux::Kakshya::MeshAssemblySurface surface_of(
        const MayaFlux::Kakshya::MeshAssemblyContainer& assembly, const MayaFlux::Kriya::MeshAssemblyRealizedPart& part)
    {
        const auto& materials = assembly.get_data().materials;
        for (const uint64_t row : part.rows) {
            const auto placement = assembly.placement(row);
            if (placement && placement->material && *placement->material < materials.size()) {
                if (const auto* surface = materials[*placement->material].facets.get<MayaFlux::Kakshya::MeshAssemblySurface>()) {
                    return *surface;
                }
            }
        }
        return { .roughness = 0.8F, .double_sided = true };
    }

    std::shared_ptr<MayaFlux::Core::Window> m_window;
    View m_view;
    std::filesystem::path m_shaders;
    std::shared_ptr<MayaFlux::Nexus::Tapestry> m_tapestry;
    std::shared_ptr<MayaFlux::Nexus::StateOrchestrator> m_orchestrator;
    std::shared_ptr<MayaFlux::Nexus::State::Bindings> m_bindings;
    std::shared_ptr<MayaFlux::Nexus::InfluenceList> m_light_list;
    std::shared_ptr<MayaFlux::Nexus::Fabric> m_light_fabric;
    std::vector<std::shared_ptr<MayaFlux::Nexus::Emitter>> m_lights;
    std::vector<std::optional<Asset>> m_assets;

    /**
     * @brief The scene shader for a part drawn as a mesh network: scene_lit for one with a
     *        texture, scene_lit_color for one without. Empty, keeping the library's unlit
     *        shader, when the compiled shader isn't there.
     */
    [[nodiscard]] std::string lit_shader(const MayaFlux::Kriya::MeshAssemblyRealizedPart& part) const
    {
        const bool textured = part.network
            && std::ranges::any_of(part.network->slots(), [](const auto& slot) { return slot.diffuse_texture != nullptr; });
        const auto path = m_shaders / (textured ? "scene_lit.frag.spv" : "scene_lit_color.frag.spv");
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            MF_PRINT("scene: no {}, drawing unlit", path.string());
            return {};
        }
        return path.string();
    }

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
     * @brief Draw @p buffer from the scene's view, culling back faces unless its material is
     *        double-sided: such faces may wind either way.
     */
    template <typename Buffer>
    void view_from(const std::shared_ptr<Buffer>& buffer, bool double_sided) const
    {
        namespace Graphics = MayaFlux::Portal::Graphics;
        if (buffer && buffer->get_render_processor()) {
            buffer->get_render_processor()->set_view_cell(m_view, true, double_sided ? Graphics::CullMode::NONE : Graphics::CullMode::BACK);
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
     * @brief Every mesh row's triangles placed where the row rests, and their bounds, or null
     *        when there are none. A mesh without indices is read as a list of triangles.
     */
    static std::shared_ptr<const Shape> rest_shape(const std::shared_ptr<const MayaFlux::Kakshya::MeshAssemblyContainer>& assembly)
    {
        namespace Kakshya = MayaFlux::Kakshya;

        const auto rest = Kakshya::MeshAssemblyRestFrames::create(assembly);
        const auto frame = rest ? rest->sample(0.0) : nullptr;
        if (!frame) {
            return nullptr;
        }

        auto shape = std::make_shared<Shape>();
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
            const auto base = static_cast<uint32_t>(shape->points.size());
            for (size_t offset = 0; offset + sizeof(glm::vec3) <= bytes->size(); offset += stride) {
                glm::vec3 position;
                std::memcpy(&position, bytes->data() + offset, sizeof(position));
                shape->points.emplace_back(world * glm::vec4(position, 1.0F));
            }
            const auto count = static_cast<uint32_t>(shape->points.size()) - base;
            if (const auto* indices = std::get_if<std::vector<uint32_t>>(&mesh->index_variant); indices && !indices->empty()) {
                for (const uint32_t index : *indices) {
                    shape->triangles.push_back(base + index);
                }
            } else {
                for (uint32_t i = 0; i + 2 < count; i += 3) {
                    shape->triangles.insert(shape->triangles.end(), { base + i, base + i + 1, base + i + 2 });
                }
            }
        }
        if (shape->points.empty()) {
            return nullptr;
        }

        shape->lo = glm::vec3(std::numeric_limits<float>::max());
        shape->hi = glm::vec3(std::numeric_limits<float>::lowest());
        for (const auto& point : shape->points) {
            shape->lo = glm::min(shape->lo, point);
            shape->hi = glm::max(shape->hi, point);
        }
        const auto boxes = MayaFlux::Kinesis::triangle_boxes(shape->points, shape->triangles);
        shape->boxes = std::make_shared<const MayaFlux::Kinesis::BoxHierarchy>(boxes);
        return shape;
    }

    /**
     * @brief The placement that stands the rest pose on @p at, centred over it, and the size it stands at.
     *
     * glTF is in metres, as the maze is, so a rest pose between 1 cm and 10 m tall keeps its
     * size. A file too tall for metres is read as centimetres, then millimetres (the Fox
     * sample is in centimetres), and one that is neither is scaled to @p height. The bounds
     * are those of @p shape.
     */
    static Fit fit(const Shape* shape, glm::vec3 at, float height)
    {
        const Fit unfitted { .placement = glm::translate(glm::mat4(1.0F), at), .size = glm::vec3(0.0F) };
        if (!shape || !(shape->hi.y > shape->lo.y)) {
            return unfitted;
        }
        const glm::vec3 lo = shape->lo;
        const glm::vec3 hi = shape->hi;
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
