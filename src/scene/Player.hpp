#pragma once

#include "MayaFlux/MayaFlux.hpp"

#include "MayaFlux/Kakshya/Semantic/MeshAssemblyContainer.hpp"
#include "MayaFlux/Kakshya/Semantic/MeshAssemblyMotionFrames.hpp"
#include "MayaFlux/Kriya/MotionReflow.hpp"
#include "MayaFlux/Kriya/Realization/MeshAssemblyRealization.hpp"
#include "MayaFlux/Kriya/ReflowHelpers/FrameBlend.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace game {

/**
 * @brief Plays the motions of one assembly into its realization.
 *
 * One MotionReflow holds a course for the rest pose and a paused course for each motion. The
 * courses are mixed by weight (placement_blend), so playing another motion crossfades to it.
 * After the mix, a placement_edit puts the whole assembly where the scene places it. Course
 * commands are queued until the reflow's next advance, so every call here is safe from any thread.
 */
class Player {
public:
    struct Motion {
        std::string name;
        double length {};
    };

    /**
     * @brief Start the reflow over @p realization, showing the rest pose at @p placement.
     *        Call from a FRAME_ACCURATE task, after the realization is presented.
     * @return nullptr if the assembly has no rows or the reflow can't start.
     */
    static std::shared_ptr<Player> create(
        const std::shared_ptr<const MayaFlux::Kakshya::MeshAssemblyContainer>& assembly,
        const std::shared_ptr<const MayaFlux::Kriya::MeshAssemblyRealization>& realization,
        const glm::mat4& placement)
    {
        namespace Kakshya = MayaFlux::Kakshya;
        namespace Kriya = MayaFlux::Kriya;

        auto rest = Kakshya::MeshAssemblyRestFrames::create(assembly);
        if (!rest || !realization) {
            return nullptr;
        }

        auto player = std::shared_ptr<Player>(new Player());
        player->m_placed->transform = placement;
        for (size_t row = 0; row < assembly->get_total_elements(); ++row) {
            if (const auto p = assembly->placement(row); p && !p->parent) {
                player->m_placed->roots.push_back(row);
            }
        }

        auto builder = Kriya::MotionReflowBuilder::sampling(rest, realization->placement_sink(), 0.0, MayaFlux::get_scheduler())
                           .course({ .in = 0.0, .out = 1.0, .edge = Kriya::CourseEdge::Loop });
        const auto& motions = assembly->get_data().motions;
        for (size_t m = 0; m < motions.size(); ++m) {
            auto frames = Kakshya::MeshAssemblyMotionFrames::create(assembly, m);
            if (!frames) {
                continue;
            }
            const auto [first, last] = frames->extent();
            if (!(last > first)) {
                continue;
            }
            builder.route({ .course = { .in = first, .out = last, .edge = Kriya::CourseEdge::Loop }, .paused = true, .sampled = frames });
            player->m_motions.push_back({ .name = motions[m].name.empty() ? "motion " + std::to_string(m) : motions[m].name, .length = last - first });
            player->m_starts.push_back(first);
        }

        auto placed = player->m_placed;
        player->m_reflow = builder.operation(Kriya::placement_blend(assembly))
                               .operation(Kriya::placement_edit(assembly, [placed](std::span<glm::mat4> locals, const Kriya::FrameContext&) {
                                   const std::scoped_lock lock(placed->mutex);
                                   for (const size_t row : placed->roots) {
                                       if (row < locals.size()) {
                                           locals[row] = placed->transform * locals[row];
                                       }
                                   }
                               }))
                               .start();
        if (!player->m_reflow) {
            return nullptr;
        }

        const auto courses = player->m_reflow->courses();
        if (courses.size() != player->m_motions.size() + 1) {
            return nullptr;
        }
        player->m_rest = courses.front();
        player->m_lanes.assign(courses.begin() + 1, courses.end());
        for (const auto& lane : player->m_lanes) {
            lane->weight(0.0);
        }
        return player;
    }

    [[nodiscard]] const std::vector<Motion>& motions() const { return m_motions; }

    /** @brief The motion playing or paused, or nullopt for the rest pose. */
    [[nodiscard]] std::optional<size_t> current() const
    {
        const auto m = m_current.load();
        return m < 0 ? std::nullopt : std::optional<size_t>(static_cast<size_t>(m));
    }

    /**
     * @brief Play @p motion from its start, crossfading from what showed before over @p fade seconds.
     *        Asking for the current motion resumes it instead.
     */
    bool play(size_t motion, double fade = 0.3)
    {
        if (motion >= m_lanes.size()) {
            return false;
        }
        if (current() == motion) {
            return m_lanes[motion]->start();
        }
        for (size_t i = 0; i < m_lanes.size(); ++i) {
            if (i != motion) {
                m_lanes[i]->fade_weight(0.0, fade);
            }
        }
        m_rest->fade_weight(0.0, fade);
        m_lanes[motion]->speed(m_speed.load());
        m_lanes[motion]->retrigger();
        m_lanes[motion]->fade_weight(1.0, fade);
        m_current = static_cast<int64_t>(motion);
        return true;
    }

    /** @brief Hold the current motion where it is. */
    bool pause() { return with_current([](auto& lane) { return lane.pause(); }); }

    /** @brief Run the current motion on from where it is. */
    bool resume() { return with_current([](auto& lane) { return lane.start(); }); }

    /** @brief Crossfade back to the rest pose and stop every motion. */
    void stop(double fade = 0.3)
    {
        for (const auto& lane : m_lanes) {
            lane->fade_weight(0.0, fade);
        }
        m_rest->fade_weight(1.0, fade);
        if (const auto m = current()) {
            m_lanes[*m]->pause();
        }
        m_current = -1;
    }

    /** @brief Put the current motion at @p seconds from its start. */
    bool seek(double seconds)
    {
        const auto m = current();
        return m && m_lanes[*m]->seek(m_starts[*m] + seconds);
    }

    /** @brief Clock seconds per second for every motion. Negative plays backwards. */
    void speed(double value)
    {
        m_speed = value;
        for (const auto& lane : m_lanes) {
            lane->speed(value);
        }
    }

    /** @brief Loop every motion, or hold it at its end. */
    void loop(bool looping)
    {
        m_looping = looping;
        for (const auto& lane : m_lanes) {
            lane->edge(looping ? MayaFlux::Kriya::CourseEdge::Loop : MayaFlux::Kriya::CourseEdge::Hold);
        }
    }

    [[nodiscard]] bool looping() const { return m_looping.load(); }
    [[nodiscard]] double speed() const { return m_speed.load(); }

    /** @brief Seconds into the current motion, as of the reflow's last advance. 0 at rest. */
    [[nodiscard]] double time() const
    {
        const auto m = current();
        return m ? m_lanes[*m]->time() - m_starts[*m] : 0.0;
    }

    [[nodiscard]] bool paused() const
    {
        const auto m = current();
        return !m || m_lanes[*m]->paused();
    }

    /** @brief Where the whole assembly stands in the scene. */
    [[nodiscard]] glm::mat4 placement() const
    {
        const std::scoped_lock lock(m_placed->mutex);
        return m_placed->transform;
    }

    void set_placement(const glm::mat4& transform)
    {
        const std::scoped_lock lock(m_placed->mutex);
        m_placed->transform = transform;
    }

private:
    Player() = default;

    /** The placement the edit applies to the root rows, shared with the reflow's thread. */
    struct Placed {
        mutable std::mutex mutex;
        glm::mat4 transform { 1.0F };
        std::vector<size_t> roots;
    };

    std::shared_ptr<Placed> m_placed { std::make_shared<Placed>() };
    std::shared_ptr<MayaFlux::Kriya::MotionReflow> m_reflow;
    std::shared_ptr<MayaFlux::Kriya::Course> m_rest;
    std::vector<std::shared_ptr<MayaFlux::Kriya::Course>> m_lanes;
    std::vector<Motion> m_motions;
    std::vector<double> m_starts;
    std::atomic<int64_t> m_current { -1 };
    std::atomic<double> m_speed { 1.0 };
    std::atomic<bool> m_looping { true };

    template <typename Fn>
    bool with_current(Fn&& fn)
    {
        const auto m = current();
        return m && fn(*m_lanes[*m]);
    }
};

}
