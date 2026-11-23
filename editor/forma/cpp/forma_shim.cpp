#include "forma_shim.h"

#include "forma/src/lib.rs.h"

#include "MayaFlux/Core/Engine.hpp"
#include "MayaFlux/Core/Subsystems/GraphicsSubsystem.hpp"
#include "MayaFlux/MayaFlux.hpp"

#include <chrono>
#include <cstddef>
#include <stdexcept>

namespace F = MayaFlux::Portal::Forma;
namespace Core = MayaFlux::Core;
using MayaFlux::IO::MouseButtons;
using MayaFlux::Kakshya::Vertex;
using MayaFlux::Portal::Graphics::PrimitiveTopology;

namespace mf_rs {

static_assert(sizeof(mf_rs::Vertex) == sizeof(Vertex), "Rust Vertex must match Kakshya::Vertex");
static_assert(offsetof(mf_rs::Vertex, position) == offsetof(Vertex, position));
static_assert(offsetof(mf_rs::Vertex, color) == offsetof(Vertex, color));
static_assert(offsetof(mf_rs::Vertex, scalar) == offsetof(Vertex, scalar));
static_assert(offsetof(mf_rs::Vertex, uv) == offsetof(Vertex, uv));
static_assert(offsetof(mf_rs::Vertex, normal) == offsetof(Vertex, normal));
static_assert(offsetof(mf_rs::Vertex, tangent) == offsetof(Vertex, tangent));

static_assert(static_cast<int>(Topology::PointList) == static_cast<int>(PrimitiveTopology::POINT_LIST));
static_assert(static_cast<int>(Topology::LineList) == static_cast<int>(PrimitiveTopology::LINE_LIST));
static_assert(static_cast<int>(Topology::LineStrip) == static_cast<int>(PrimitiveTopology::LINE_STRIP));
static_assert(static_cast<int>(Topology::TriangleList) == static_cast<int>(PrimitiveTopology::TRIANGLE_LIST));
static_assert(static_cast<int>(Topology::TriangleStrip) == static_cast<int>(PrimitiveTopology::TRIANGLE_STRIP));
static_assert(static_cast<int>(Topology::TriangleFan) == static_cast<int>(PrimitiveTopology::TRIANGLE_FAN));

static_assert(static_cast<int>(MouseButton::Left) == static_cast<int>(MouseButtons::Left));
static_assert(static_cast<int>(MouseButton::Right) == static_cast<int>(MouseButtons::Right));
static_assert(static_cast<int>(MouseButton::Middle) == static_cast<int>(MouseButtons::Middle));

struct Surface::Impl {
    F::Surface surface;
};

struct Target::Impl {
    F::FormaTarget target;
};

Target::Target(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

Target::~Target() = default;

Surface::Surface(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

Surface::~Surface() = default;

namespace {

    bool on_graphics_thread()
    {
        auto subsystems = MayaFlux::get_context().get_subsystem_manager();
        auto graphics = subsystems ? subsystems->get_graphics_subsystem() : nullptr;
        return graphics && graphics->is_graphics_thread();
    }

    double now_seconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

}

std::shared_ptr<Surface> create_surface(rust::Str title, std::uint32_t width, std::uint32_t height)
{
    if (!on_graphics_thread())
        throw std::runtime_error("create_surface must run on the graphics thread, from a FRAME_ACCURATE task");

    std::string name(title);
    auto window = MayaFlux::create_window({ .title = name, .width = width, .height = height });
    if (!window)
        throw std::runtime_error("could not create the window");
    window->show();

    auto surface = F::create_surface(F::SurfaceConfig { .window = window, .name = name });
    return std::make_shared<Surface>(std::make_unique<Surface::Impl>(Surface::Impl { std::move(surface) }));
}

std::unique_ptr<Target> Surface::slice(Topology topology) const
{
    auto target = F::slice(m_impl->surface, static_cast<PrimitiveTopology>(topology));
    return std::make_unique<Target>(std::make_unique<Target::Impl>(Target::Impl { std::move(target) }));
}

void Target::submit(rust::Slice<const mf_rs::Vertex> vertices) const
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(vertices.data());
    m_impl->target.submit(std::vector<std::uint8_t>(bytes, bytes + vertices.size() * sizeof(mf_rs::Vertex)));
}

std::uint32_t Surface::add_element(rust::Str name, Vec2 min, Vec2 max, const Target& target) const
{
    auto element = F::Element {}
                       .with_name(std::string(name))
                       .with_rect({ min.x, min.y }, { max.x, max.y })
                       .with_buffer(target.impl().target);
    return m_impl->surface.add(std::move(element));
}

void Surface::on_press(std::uint32_t id, MouseButton button, rust::Box<PressFn> callback) const
{
    auto held = std::make_shared<rust::Box<PressFn>>(std::move(callback));
    m_impl->surface.ctx().on_press(id, static_cast<MouseButtons>(button),
        [held](std::uint32_t element, glm::vec2 ndc) { (*held)->call(element, Vec2 { ndc.x, ndc.y }); });
}

void Surface::inject_press(Vec2 ndc, MouseButton button) const
{
    const auto& window = m_impl->surface.window();
    const auto& state = window->get_state();
    const double px = (static_cast<double>(ndc.x) + 1.0) * 0.5 * state.current_width;
    const double py = (1.0 - static_cast<double>(ndc.y)) * 0.5 * state.current_height;

    auto& source = window->get_event_source();

    Core::WindowEvent move;
    move.type = Core::WindowEventType::MOUSE_MOTION;
    move.timestamp = now_seconds();
    move.data = Core::WindowEvent::MousePosData { .x = px, .y = py };
    source.signal(move);

    Core::WindowEvent press;
    press.type = Core::WindowEventType::MOUSE_BUTTON_PRESSED;
    press.timestamp = now_seconds();
    press.data = Core::WindowEvent::MouseButtonData { .button = static_cast<std::int8_t>(button), .mods = {} };
    source.signal(press);

    press.type = Core::WindowEventType::MOUSE_BUTTON_RELEASED;
    press.timestamp = now_seconds();
    source.signal(press);
}

void print(rust::Str message)
{
    MF_PRINT("{}", std::string(message));
}

}
