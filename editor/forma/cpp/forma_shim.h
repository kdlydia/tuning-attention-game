#pragma once

// The C++ half of the Rust bridge. This header names no MayaFlux type, so cargo can
// compile the generated bridge without MayaFlux's flags. The MayaFlux objects live in
// each class's Impl, defined in forma_shim.cpp.

#include "rust/cxx.h"

#include <cstdint>
#include <memory>

namespace mf_rs {

// Defined by the generated bridge header.
struct Vec2;
struct Vertex;
enum class Topology : std::uint8_t;
enum class MouseButton : std::uint8_t;
struct PressFn;

class Target {
public:
    struct Impl;
    explicit Target(std::unique_ptr<Impl> impl);
    ~Target();

    void submit(rust::Slice<const Vertex> vertices) const;

    [[nodiscard]] Impl& impl() const { return *m_impl; }

private:
    std::unique_ptr<Impl> m_impl;
};

class Surface {
public:
    struct Impl;
    explicit Surface(std::unique_ptr<Impl> impl);
    ~Surface();

    [[nodiscard]] std::unique_ptr<Target> slice(Topology topology) const;
    std::uint32_t add_element(rust::Str name, Vec2 min, Vec2 max, const Target& target) const;
    void on_press(std::uint32_t id, MouseButton button, rust::Box<PressFn> callback) const;
    void inject_press(Vec2 ndc, MouseButton button) const;

    [[nodiscard]] Impl& impl() const { return *m_impl; }

private:
    std::unique_ptr<Impl> m_impl;
};

std::shared_ptr<Surface> create_surface(rust::Str title, std::uint32_t width, std::uint32_t height);
void print(rust::Str message);

}
