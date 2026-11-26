#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

/**
 * The maze in 3D, built from the editor's data/maze.txt. The game and the editor's
 * game view both use it.
 */
namespace game {

/**
 * @brief The maze grid as the editor saves it: one line per row, north first,
 *        '#' for a wall and '.' for floor.
 */
struct Maze {
    size_t rows {};
    size_t cols {};
    std::vector<bool> walls;

    [[nodiscard]] bool is_wall(size_t row, size_t col) const { return walls[row * cols + col]; }

    /** @brief Read @p path. On failure returns nullopt and says why in @p error. */
    static std::optional<Maze> load(const std::filesystem::path& path, std::string& error)
    {
        std::ifstream file(path);
        if (!file) {
            error = "can't open " + path.string();
            return std::nullopt;
        }

        Maze maze;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            if (maze.cols == 0) {
                maze.cols = line.size();
            }
            if (line.size() != maze.cols) {
                error = "row " + std::to_string(maze.rows + 1) + " has " + std::to_string(line.size())
                    + " cells, expected " + std::to_string(maze.cols);
                return std::nullopt;
            }
            for (const char cell : line) {
                if (cell != '#' && cell != '.') {
                    error = "row " + std::to_string(maze.rows + 1) + " has '" + cell + "', expected '#' or '.'";
                    return std::nullopt;
                }
                maze.walls.push_back(cell == '#');
            }
            ++maze.rows;
        }

        if (maze.rows == 0) {
            error = path.string() + " is empty";
            return std::nullopt;
        }
        return maze;
    }
};

/**
 * @brief The maze drawn in its own window, with MayaFlux's fly camera:
 *        WASD and QE to move, right-drag to look, scroll to dolly.
 *
 * Every cell has a floor tile and a wall box, each one instance of a shared box, so the
 * floor and the walls are one draw each. A floor cell's wall is scaled to nothing.
 * Column runs along +X and row along +Z, so the camera starts south of the maze
 * looking north and the view matches the editor's grid.
 */
class MazeView {
public:
    static constexpr float cell = 2.0F;
    static constexpr float wall_height = 3.0F;

    /**
     * @brief Open the view in a new window. Call from a FRAME_ACCURATE task.
     * @return The view, which lives until the process exits (like keep_alive()),
     *         or null if the maze can't be read.
     */
    static MazeView* open(std::filesystem::path path)
    {
        std::string error;
        auto maze = Maze::load(path, error);
        if (!maze) {
            MF_PRINT("maze view: {}", error);
            return nullptr;
        }

        auto* view = new MazeView(std::move(path));
        if (!view->build(*maze)) {
            return nullptr;
        }
        return view;
    }

    /** @brief Rebuild from the file if it changed since the last look. Call on the graphics thread. */
    void reload_if_changed()
    {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(m_path, ec);
        if (ec || time == m_seen) {
            return;
        }
        m_seen = time;

        std::string error;
        if (auto maze = Maze::load(m_path, error)) {
            place(*maze);
            m_maze = std::move(*maze);
            MF_PRINT("maze view: reloaded {}x{}", m_maze.cols, m_maze.rows);
        } else {
            MF_PRINT("maze view: keeping the last maze, {}", error);
        }
    }

    [[nodiscard]] const std::shared_ptr<MayaFlux::Core::Window>& window() const { return m_window; }

    /** @brief The fly camera's viewpoint, for anything else drawn in this window. */
    [[nodiscard]] const std::shared_ptr<MayaFlux::Memory::OwnedCell<MayaFlux::Kinesis::Viewpoint>>& view() const
    {
        return m_view.first;
    }

    /** @brief The middle of the floor cell nearest the maze's centre, at floor height, or the centre if every cell is a wall. */
    [[nodiscard]] glm::vec3 floor_near_center() const
    {
        std::optional<glm::vec3> best;
        for (size_t row = 0; row < m_maze.rows; ++row) {
            for (size_t col = 0; col < m_maze.cols; ++col) {
                if (m_maze.is_wall(row, col)) {
                    continue;
                }
                const glm::vec3 position = cell_position(m_maze, row, col);
                if (!best || glm::length(position) < glm::length(*best)) {
                    best = position;
                }
            }
        }
        return best.value_or(glm::vec3(0.0F));
    }

private:
    explicit MazeView(std::filesystem::path path)
        : m_path(std::move(path))
    {
    }

    using Layer = std::shared_ptr<MayaFlux::Buffers::InstanceNetworkBuffer>;

    std::filesystem::path m_path;
    std::filesystem::file_time_type m_seen {};
    Maze m_maze;
    std::shared_ptr<MayaFlux::Core::Window> m_window;
    Layer m_floor;
    Layer m_walls;
    MayaFlux::ViewportBinding m_view;

    bool build(const Maze& maze)
    {
        MayaFlux::Core::WindowCreateInfo window {};
        window.title = "game view";
        window.width = 960;
        window.height = 720;
        m_window = MayaFlux::create_window(window);
        if (!m_window) {
            MF_PRINT("maze view: could not create the window");
            return false;
        }
        m_window->show();

        const size_t count = maze.rows * maze.cols;
        m_floor = make_layer(shaded_box({ 0.0F, -0.05F, 0.0F }, { cell * 0.48F, 0.05F, cell * 0.48F }, { 0.24F, 0.25F, 0.28F }), count);
        m_walls = make_layer(shaded_box({ 0.0F, wall_height / 2, 0.0F }, { cell / 2, wall_height / 2, cell / 2 }, { 0.80F, 0.76F, 0.68F }), count);
        if (!m_floor || !m_walls) {
            MF_PRINT("maze view: could not build the maze");
            return false;
        }
        place(maze);
        m_maze = maze;

        const float span = static_cast<float>(std::max(maze.rows, maze.cols)) * cell;
        const MayaFlux::Kinesis::NavigationConfig camera {
            .initial_eye = { 0.0F, span * 0.7F, span * 0.75F },
            .initial_target = { 0.0F, 0.0F, 0.0F },
            .near_plane = 0.1F,
            .move_speed = 10.0F,
        };
        m_view = MayaFlux::bind_fly_preset(m_window, m_walls->get_render_processor(), camera, {}, "game_view");
        m_floor->get_render_processor()->set_view_cell(m_view.first);

        std::error_code ec;
        m_seen = std::filesystem::last_write_time(m_path, ec);
        MF_PRINT("maze view: showing {}x{} from {}", maze.cols, maze.rows, m_path.string());
        return true;
    }

    /** @brief A box whose faces are shaded by direction, since the instanced shader has no lighting. */
    static std::shared_ptr<MayaFlux::Nodes::GpuSync::MeshWriterNode> shaded_box(
        glm::vec3 center, glm::vec3 half, glm::vec3 color)
    {
        auto box = std::make_shared<MayaFlux::Nodes::GpuSync::MeshWriterNode>(24);
        box->set_mesh(MayaFlux::Kinesis::generate_box(center, half));
        auto vertices = box->get_mesh_vertices();
        for (auto& vertex : vertices) {
            float light = 0.62F;
            if (vertex.normal.y > 0.5F) {
                light = 1.0F;
            } else if (vertex.normal.y < -0.5F) {
                light = 0.35F;
            } else if (std::abs(vertex.normal.x) > 0.5F) {
                light = 0.8F;
            }
            vertex.color = glm::vec4(color * light, 1.0F);
        }
        box->set_mesh_vertices(vertices);
        return box;
    }

    static glm::mat4 hidden() { return glm::scale(glm::mat4(1.0F), glm::vec3(0.0F)); }

    /** @brief The middle of a cell at floor height. */
    static glm::vec3 cell_position(const Maze& maze, size_t row, size_t col)
    {
        return {
            (static_cast<float>(col) - static_cast<float>(maze.cols - 1) / 2) * cell,
            0.0F,
            (static_cast<float>(row) - static_cast<float>(maze.rows - 1) / 2) * cell,
        };
    }

    Layer make_layer(std::shared_ptr<MayaFlux::Nodes::GpuSync::MeshWriterNode> shape, size_t count) const
    {
        MayaFlux::StructureConfig::Instances instances {};
        instances.prototype_writer = std::move(shape);
        instances.transforms = std::vector<glm::mat4>(count, hidden());
        instances.render.target_window = m_window;
        return MayaFlux::vega.mint(instances);
    }

    void place(const Maze& maze)
    {
        place_layer(*m_floor->get_network(), maze, false);
        place_layer(*m_walls->get_network(), maze, true);
    }

    /** @brief One instance per cell, adding instances if the maze grew and hiding any left over. */
    static void place_layer(MayaFlux::Nodes::Network::InstanceNetwork& network, const Maze& maze, bool walls_only)
    {
        const size_t count = maze.rows * maze.cols;
        const auto shape = network.get_slot(0).node;
        while (network.slot_count() < count) {
            network.add_slot("cell_" + std::to_string(network.slot_count()), shape);
        }

        for (size_t i = 0; i < network.slot_count(); ++i) {
            auto& slot = network.get_slot(static_cast<uint32_t>(i));
            slot.transform = hidden();
            if (i < count) {
                const size_t row = i / maze.cols;
                const size_t col = i % maze.cols;
                if (!walls_only || maze.is_wall(row, col)) {
                    slot.transform = glm::translate(glm::mat4(1.0F), cell_position(maze, row, col));
                }
            }
            slot.dirty = true;
        }
    }
};

}
