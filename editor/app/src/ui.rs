//! Colours and row layout shared by the tool windows. Positions are in NDC.
//!
//! The windows are sRGB, so colours here are linear: each is the sRGB colour in its
//! comment, decoded.

use forma::{Surface, Target, Text, Topology, Vec2};

pub const TEXT: [f32; 4] = [0.807, 0.807, 0.776, 1.0]; // #E8E8E4
pub const DIM: [f32; 4] = [0.270, 0.283, 0.305, 1.0]; // #8E9196
pub const CLEAR: [f32; 4] = [0.0, 0.0, 0.0, 0.0];
pub const ROW: [f32; 4] = [0.019, 0.021, 0.025, 1.0]; // #26282C
pub const SELECTED: [f32; 4] = [0.044, 0.098, 0.209, 1.0]; // #3B587E
pub const TRACK: [f32; 4] = ROW;
pub const FILL: [f32; 4] = [0.147, 0.301, 0.578, 1.0]; // #6B95C8

/// The line across the top of a tool window.
pub const TITLE: (Vec2, Vec2) = (Vec2 { x: -0.95, y: 0.84 }, Vec2 { x: 0.95, y: 0.96 });

/// A column of equal rows, top down.
pub struct Column {
    pub x_min: f32,
    pub x_max: f32,
    /// Top edge of the first row.
    pub top: f32,
    /// Distance from one row's top to the next.
    pub pitch: f32,
    pub height: f32,
}

/// The bounds of row `i` of `column`.
pub fn row_bounds(column: &Column, i: usize) -> (Vec2, Vec2) {
    let top = column.top - column.pitch * i as f32;
    (Vec2 { x: column.x_min, y: top - column.height }, Vec2 { x: column.x_max, y: top })
}

/// Pixels a line of the editor's font takes (Inter at 16 px, with its line gap).
const LINE_PX: f32 = 20.0;
/// Pixels between a row's left and right edges and its text.
const PAD_PX: f32 = 8.0;

/// The region a row's text takes: one line tall, centred vertically, inset from the sides.
/// `window` is the window's size in pixels.
pub fn text_inset(min: Vec2, max: Vec2, window: (u32, u32)) -> (Vec2, Vec2) {
    let px = Vec2 { x: 2.0 / window.0 as f32, y: 2.0 / window.1 as f32 };
    let line = (LINE_PX * px.y).min(max.y - min.y);
    let middle = (min.y + max.y) / 2.0;
    (
        Vec2 { x: min.x + PAD_PX * px.x, y: middle - line / 2.0 },
        Vec2 { x: max.x - PAD_PX * px.x, y: middle + line / 2.0 },
    )
}

/// A row that can be clicked: a background rectangle, which takes the presses, with a
/// line of text centred on it.
pub struct Row {
    background: Target,
    text: Text,
    id: u32,
    min: Vec2,
    max: Vec2,
}

impl Row {
    pub fn add(surface: &Surface, name: &str, min: Vec2, max: Vec2, window: (u32, u32)) -> Self {
        let background = surface.slice(Topology::TriangleList);
        background.submit(&forma::rect(min, max, CLEAR));
        let id = surface.add_element(name, min, max, &background);
        let (text_min, text_max) = text_inset(min, max, window);
        let text = surface.add_text(&format!("{name}_text"), text_min, text_max, "", TEXT, CLEAR);
        Row { background, text, id, min, max }
    }

    /// The element id that takes the row's presses.
    pub fn id(&self) -> u32 {
        self.id
    }

    pub fn set(&self, label: &str, color: [f32; 4], background: [f32; 4]) {
        self.background.submit(&forma::rect(self.min, self.max, background));
        self.text.set(label, color, CLEAR);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rows_step_down_by_pitch() {
        let column = Column { x_min: -1.0, x_max: 0.0, top: 0.5, pitch: 0.2, height: 0.1 };
        let (min, max) = row_bounds(&column, 2);
        assert!((max.y - 0.1).abs() < 1e-6 && (min.y - 0.0).abs() < 1e-6);
        assert_eq!((min.x, max.x), (-1.0, 0.0));
    }

    #[test]
    fn text_is_one_line_centred_and_inset() {
        // A 40 px row in a 400 px window, 200 px wide.
        let (min, max) = text_inset(Vec2 { x: -0.5, y: 0.0 }, Vec2 { x: 0.5, y: 0.2 }, (200, 400));
        assert!((max.y - min.y - 0.1).abs() < 1e-6, "one 20 px line");
        assert!(((min.y + max.y) / 2.0 - 0.1).abs() < 1e-6, "centred");
        assert!((min.x - (-0.5 + 0.08)).abs() < 1e-6 && (max.x - (0.5 - 0.08)).abs() < 1e-6, "8 px in");
    }
}
