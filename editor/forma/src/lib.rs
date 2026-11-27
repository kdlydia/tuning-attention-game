//! Rust access to MayaFlux Forma through a small C++ shim (`cpp/forma_shim.cpp`).
//!
//! Everything here runs on the graphics thread. The shim refuses to create a surface
//! anywhere else, and the handles are not `Send`, so they and their callbacks stay there.

mod selftest;

use selftest::{check as selftest_check, poke as selftest_poke, start as selftest_start};

#[cxx::bridge(namespace = "mf_rs")]
mod ffi {
    /// A point in normalised device coordinates.
    #[derive(Clone, Copy, Debug, PartialEq)]
    struct Vec2 {
        x: f32,
        y: f32,
    }

    /// Kakshya::Vertex field for field (68 bytes). The shim checks the layout at compile time.
    #[derive(Clone, Copy, Debug)]
    struct Vertex {
        position: [f32; 3],
        color: [f32; 4],
        scalar: f32,
        uv: [f32; 2],
        normal: [f32; 3],
        tangent: [f32; 4],
    }

    /// Portal::Graphics::PrimitiveTopology.
    #[repr(u8)]
    enum Topology {
        PointList = 0,
        LineList = 1,
        LineStrip = 2,
        TriangleList = 3,
        TriangleStrip = 4,
        TriangleFan = 5,
    }

    /// IO::MouseButtons.
    #[repr(u8)]
    enum MouseButton {
        Left = 0,
        Right = 1,
        Middle = 2,
    }

    extern "Rust" {
        type PressFn;
        fn call(self: &mut PressFn, id: u32, ndc: Vec2);

        // The press test, called by the C++ host on the graphics thread (`editor --test`).
        fn selftest_start();
        fn selftest_poke();
        fn selftest_check() -> bool;
    }

    unsafe extern "C++" {
        include!("forma_shim.h");

        type Surface;
        type Target;
        type Text;

        fn create_surface(title: &str, width: u32, height: u32) -> Result<SharedPtr<Surface>>;
        fn slice(self: &Surface, topology: Topology) -> UniquePtr<Target>;
        fn add_element(self: &Surface, name: &str, min: Vec2, max: Vec2, target: &Target) -> u32;
        fn add_text(
            self: &Surface,
            name: &str,
            min: Vec2,
            max: Vec2,
            text: &str,
            color: [f32; 4],
            background: [f32; 4],
        ) -> UniquePtr<Text>;
        fn on_press(self: &Surface, id: u32, button: MouseButton, callback: Box<PressFn>);
        fn on_drag(self: &Surface, id: u32, button: MouseButton, callback: Box<PressFn>);
        fn inject_press(self: &Surface, ndc: Vec2, button: MouseButton);
        fn submit(self: &Target, vertices: &[Vertex]);
        fn id(self: &Text) -> u32;
        fn set(self: &Text, text: &str, color: [f32; 4], background: [f32; 4]);
        fn print(message: &str);
    }
}

pub use ffi::{MouseButton, Topology, Vec2, Vertex};

/// A Rust closure held by a Forma callback.
pub struct PressFn(Box<dyn FnMut(u32, Vec2)>);

impl PressFn {
    fn call(&mut self, id: u32, ndc: Vec2) {
        (self.0)(id, ndc);
    }
}

/// A Forma surface: a window, its layer and its event context.
#[derive(Clone)]
pub struct Surface(cxx::SharedPtr<ffi::Surface>);

impl Surface {
    /// Open a window and its surface. Call from a FRAME_ACCURATE task.
    pub fn create(title: &str, width: u32, height: u32) -> Result<Self, cxx::Exception> {
        ffi::create_surface(title, width, height).map(Surface)
    }

    /// A slice of the surface's shared buffer to draw into.
    pub fn slice(&self, topology: Topology) -> Target {
        Target(self.0.slice(topology))
    }

    /// Add an element with rectangular bounds drawn by `target`. Returns its id.
    pub fn add_element(&self, name: &str, min: Vec2, max: Vec2, target: &Target) -> u32 {
        self.0.add_element(name, min, max, &target.0)
    }

    /// Add a line of text filling `min`..`max`, over `background`. Its pixel size is fixed
    /// from the window's size now. It takes no presses; put an element under it for that.
    pub fn add_text(&self, name: &str, min: Vec2, max: Vec2, text: &str, color: [f32; 4], background: [f32; 4]) -> Text {
        Text(self.0.add_text(name, min, max, text, color, background))
    }

    /// Run `f` with the element id and the pointer in NDC when `button` is pressed over `id`.
    pub fn on_press(&self, id: u32, button: MouseButton, f: impl FnMut(u32, Vec2) + 'static) {
        self.0.on_press(id, button, Box::new(PressFn(Box::new(f))));
    }

    /// Run `f` with the element id and the pointer in NDC while `button` drags from `id`.
    /// The pointer may be outside the element.
    pub fn on_drag(&self, id: u32, button: MouseButton, f: impl FnMut(u32, Vec2) + 'static) {
        self.0.on_drag(id, button, Box::new(PressFn(Box::new(f))));
    }

    /// Feed the window a pointer move to `ndc`, then a press and release of `button`.
    pub fn inject_press(&self, ndc: Vec2, button: MouseButton) {
        self.0.inject_press(ndc, button);
    }
}

/// A line of text added with `Surface::add_text`.
pub struct Text(cxx::UniquePtr<ffi::Text>);

impl Text {
    /// The element id.
    pub fn id(&self) -> u32 {
        self.0.id()
    }

    /// Replace the text and its colours.
    pub fn set(&self, text: &str, color: [f32; 4], background: [f32; 4]) {
        self.0.set(text, color, background);
    }
}

/// Where an element's vertices go.
pub struct Target(cxx::UniquePtr<ffi::Target>);

impl Target {
    /// Replace the slice's vertices.
    pub fn submit(&self, vertices: &[Vertex]) {
        self.0.submit(vertices);
    }
}

impl Vertex {
    /// A flat-coloured vertex at `position`, with Kakshya::Vertex's defaults elsewhere.
    pub fn flat(position: Vec2, color: [f32; 4]) -> Self {
        Vertex {
            position: [position.x, position.y, 0.0],
            color,
            scalar: 1.0,
            uv: [0.0, 0.0],
            normal: [0.0, 0.0, 1.0],
            tangent: [1.0, 0.0, 0.0, 1.0],
        }
    }
}

/// A flat-coloured rectangle as two triangles, for a `TriangleList` slice.
pub fn rect(min: Vec2, max: Vec2, color: [f32; 4]) -> [Vertex; 6] {
    let corner = |x, y| Vertex::flat(Vec2 { x, y }, color);
    [
        corner(min.x, min.y),
        corner(max.x, min.y),
        corner(max.x, max.y),
        corner(min.x, min.y),
        corner(max.x, max.y),
        corner(min.x, max.y),
    ]
}

/// Print through MayaFlux's journal.
pub fn print(message: &str) {
    ffi::print(message);
}
