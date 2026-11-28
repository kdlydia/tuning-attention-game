#pragma once

#include "MayaFlux/MayaFlux.hpp"

#ifdef GAME_LIVE
#include "MayaFlux/Host/Dependencies.hpp"
#include "MayaFlux/Host/Session.hpp"
#include "MayaFlux/Transitive/Parallel/AsyncGroup.hpp"
#endif

#include <filesystem>

/**
 * Live coding against the running game, through a Lila session that LilaCode (VS Code) or
 * lila.nvim connects to.
 */
namespace game::live {

/**
 * @brief Open a Lila session in this process, on a thread of its own.
 *
 * Returns at once: Clang takes half a minute or more to start, and the game goes on meanwhile.
 * A Parallel::AsyncGroup in MayaFlux's persistent store holds the work. Only in a build that
 * links MayaFluxHost (see CMakeLists.txt); otherwise it does nothing. @p headers goes on the
 * session's include path, so code sent to it can include the game's headers. What the game
 * stores under a key (MayaFlux::store("scene", ...)) is reached there with
 * MayaFlux::live_cast. Lila logs each failed evaluation itself. If the session can't start,
 * the Journal says why (USER, Init) and the game carries on without it.
 */
inline void start([[maybe_unused]] const std::filesystem::path& headers)
{
#ifdef GAME_LIVE
    using MayaFlux::Journal::Component;
    using MayaFlux::Journal::Context;

    auto& starting = MayaFlux::make_persistent<MayaFlux::Parallel::AsyncGroup<void>>([](std::exception_ptr failure) {
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& e) {
            MF_ERROR(Component::USER, Context::Init, "live: the session start threw: {}", e.what());
        } catch (...) {
            MF_ERROR(Component::USER, Context::Init, "live: the session start threw");
        }
    });
    starting.submit(std::async(std::launch::async, [headers] {
        namespace Host = MayaFlux::Host;
        if (const auto root = Host::add_include_root(headers); !root) {
            MF_WARN(Component::USER, Context::Init, "live: the game's headers are not in the session: {}", root.error().message);
        }
        if (const auto started = Host::start_session(); !started) {
            MF_ERROR(Component::USER, Context::Init, "live: no session: {}", started.error().message);
        }
    }));
#endif
}

}
