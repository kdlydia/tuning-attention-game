//! The press test (`editor --test`): place a rectangle, feed the window a press inside it
//! and one outside, and check that only the first reached the Rust callback.

use crate::{MouseButton, Surface, Target, Topology, Vec2};
use std::cell::Cell;
use std::sync::atomic::{AtomicU32, Ordering};

const RECT_MIN: Vec2 = Vec2 { x: -0.5, y: -0.3 };
const RECT_MAX: Vec2 = Vec2 { x: 0.5, y: 0.3 };
const INSIDE: Vec2 = Vec2 { x: 0.0, y: 0.0 };
const OUTSIDE: Vec2 = Vec2 { x: 0.8, y: 0.8 };

static PRESSES: AtomicU32 = AtomicU32::new(0);
static LAST_ID: AtomicU32 = AtomicU32::new(u32::MAX);
static LAST_X: AtomicU32 = AtomicU32::new(0);
static LAST_Y: AtomicU32 = AtomicU32::new(0);

struct Test {
    surface: Surface,
    rect: u32,
    _target: Target,
}

thread_local! {
    // Leaked on purpose, like keep_alive() in a Weave project: Forma objects dropped
    // after MayaFlux::End() reach into an engine that no longer exists.
    static TEST: Cell<Option<&'static Test>> = const { Cell::new(None) };
}

pub(crate) fn start() {
    let surface = match Surface::create("forma selftest", 640, 480) {
        Ok(surface) => surface,
        Err(error) => {
            crate::print(&format!("selftest: could not create the surface: {error}"));
            return;
        }
    };

    let target = surface.slice(Topology::TriangleList);
    target.submit(&crate::rect(RECT_MIN, RECT_MAX, [0.9, 0.45, 0.2, 1.0]));
    let rect = surface.add_element("selftest_rect", RECT_MIN, RECT_MAX, &target);

    surface.on_press(rect, MouseButton::Left, |id, ndc| {
        PRESSES.fetch_add(1, Ordering::Relaxed);
        LAST_ID.store(id, Ordering::Relaxed);
        LAST_X.store(ndc.x.to_bits(), Ordering::Relaxed);
        LAST_Y.store(ndc.y.to_bits(), Ordering::Relaxed);
        crate::print(&format!("selftest: pressed element {id} at ({:.3}, {:.3})", ndc.x, ndc.y));
    });

    let test: &'static Test = Box::leak(Box::new(Test {
        surface,
        rect,
        _target: target,
    }));
    TEST.with(|cell| cell.set(Some(test)));
}

pub(crate) fn poke() {
    let Some(test) = TEST.with(Cell::get) else {
        return;
    };
    test.surface.inject_press(INSIDE, MouseButton::Left);
    test.surface.inject_press(OUTSIDE, MouseButton::Left);
}

pub(crate) fn check() -> bool {
    let Some(test) = TEST.with(Cell::get) else {
        crate::print("selftest: FAIL, no surface was made");
        return false;
    };
    let presses = PRESSES.load(Ordering::Relaxed);
    let id = LAST_ID.load(Ordering::Relaxed);
    let x = f32::from_bits(LAST_X.load(Ordering::Relaxed));
    let y = f32::from_bits(LAST_Y.load(Ordering::Relaxed));

    let ok = presses == 1 && id == test.rect && (x - INSIDE.x).abs() < 0.01 && (y - INSIDE.y).abs() < 0.01;
    crate::print(&format!(
        "selftest: {} presses={presses} (expected 1), element={id} (expected {}), at ({x:.3}, {y:.3}) (expected ({:.3}, {:.3}))",
        if ok { "PASS" } else { "FAIL" },
        test.rect,
        INSIDE.x,
        INSIDE.y
    ));
    ok
}
