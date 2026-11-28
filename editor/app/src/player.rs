//! The player window: the motions of one loaded asset, play, pause, stop, loop and speed,
//! and a timeline to click or drag. Playing another motion crossfades to it.

use crate::ui::{self, row_bounds, Column, Row};
use forma::{Surface, Target, Text, Topology, Vec2};
use scene::{MotionInfo, PlayerState};

const WINDOW: (u32, u32) = (640, 420);
const MOTION_ROWS: usize = 8;
const MOTIONS: Column = Column { x_min: -0.95, x_max: 0.10, top: 0.74, pitch: 0.13, height: 0.11 };
const CONTROLS: Column = Column { x_min: 0.20, x_max: 0.95, top: 0.74, pitch: 0.13, height: 0.11 };
const TRACK_MIN: Vec2 = Vec2 { x: -0.95, y: -0.62 };
const TRACK_MAX: Vec2 = Vec2 { x: 0.95, y: -0.46 };
const CLOCK: (Vec2, Vec2) = (Vec2 { x: -0.95, y: -0.88 }, Vec2 { x: 0.95, y: -0.72 });

/// The speeds the speed control steps through.
const SPEEDS: [f64; 5] = [1.0, 0.5, 0.25, -1.0, 2.0];

/// The transport controls, in their order down the right column.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Control {
    PlayPause,
    Stop,
    Loop,
    Speed,
    NextAsset,
}

pub const CONTROL_ORDER: [Control; 5] = [Control::PlayPause, Control::Stop, Control::Loop, Control::Speed, Control::NextAsset];

pub struct PlayerWindow {
    surface: Surface,
    title: Text,
    motion_rows: Vec<Row>,
    controls: Vec<Row>,
    clock: Text,
    _track: Target,
    fill: Target,
    track_id: u32,
    asset: Option<usize>,
    motions: Vec<MotionInfo>,
    shown: Option<PlayerState>,
}

impl PlayerWindow {
    pub fn open() -> Result<Self, String> {
        let surface = Surface::create("player", WINDOW.0, WINDOW.1).map_err(|e| e.to_string())?;
        let (title_min, title_max) = ui::text_inset(ui::TITLE.0, ui::TITLE.1, WINDOW);
        let title = surface.add_text("player_title", title_min, title_max, "", ui::TEXT, ui::CLEAR);
        let motion_rows = (0..MOTION_ROWS)
            .map(|i| {
                let (min, max) = row_bounds(&MOTIONS, i);
                Row::add(&surface, &format!("motion_{i}"), min, max, WINDOW)
            })
            .collect();
        let controls = CONTROL_ORDER
            .iter()
            .enumerate()
            .map(|(i, _)| {
                let (min, max) = row_bounds(&CONTROLS, i);
                Row::add(&surface, &format!("control_{i}"), min, max, WINDOW)
            })
            .collect();
        let (clock_min, clock_max) = ui::text_inset(CLOCK.0, CLOCK.1, WINDOW);
        let clock = surface.add_text("clock", clock_min, clock_max, "", ui::DIM, ui::CLEAR);

        let track = surface.slice(Topology::TriangleList);
        track.submit(&forma::rect(TRACK_MIN, TRACK_MAX, ui::TRACK));
        let track_id = surface.add_element("timeline", TRACK_MIN, TRACK_MAX, &track);
        let fill = surface.slice(Topology::TriangleList);
        surface.add_element("timeline_fill", TRACK_MIN, TRACK_MIN, &fill);

        let mut window = PlayerWindow {
            surface,
            title,
            motion_rows,
            controls,
            clock,
            _track: track,
            fill,
            track_id,
            asset: None,
            motions: Vec::new(),
            shown: None,
        };
        window.redraw_lists();
        Ok(window)
    }

    pub fn surface(&self) -> &Surface {
        &self.surface
    }

    pub fn motion_ids(&self) -> Vec<u32> {
        self.motion_rows.iter().map(Row::id).collect()
    }

    pub fn control_ids(&self) -> Vec<u32> {
        self.controls.iter().map(Row::id).collect()
    }

    pub fn track_id(&self) -> u32 {
        self.track_id
    }

    /// Show the player of `asset`, and choose it in the game view.
    pub fn show(&mut self, asset: usize) {
        scene::select(Some(asset));
        self.asset = Some(asset);
        self.motions = scene::motions(asset);
        self.shown = None;
        self.redraw_lists();
    }

    /// `asset` left the scene: if it was shown, show the next asset still there, or none.
    pub fn forget(&mut self, asset: usize) {
        if self.asset != Some(asset) {
            return;
        }
        match next_after(Some(asset), &scene::assets()) {
            Some(next) => self.show(next),
            None => {
                scene::select(None);
                self.asset = None;
                self.motions.clear();
                self.shown = None;
                self.fill.submit(&[]);
                self.clock.set("", ui::DIM, ui::CLEAR);
                self.redraw_lists();
            }
        }
    }

    pub fn press_motion(&mut self, row: usize) {
        if let Some(asset) = self.asset.filter(|_| row < self.motions.len()) {
            scene::play(asset, row);
        }
    }

    pub fn press_control(&mut self, control: Control) {
        if control == Control::NextAsset {
            if let Some(next) = next_after(self.asset, &scene::assets()) {
                self.show(next);
            }
            return;
        }
        let Some(asset) = self.asset else {
            return;
        };
        let state = scene::state(asset);
        match control {
            Control::PlayPause if state.motion < 0 => {
                if !self.motions.is_empty() {
                    scene::play(asset, 0);
                }
            }
            Control::PlayPause if state.paused => {
                scene::resume(asset);
            }
            Control::PlayPause => {
                scene::pause(asset);
            }
            Control::Stop => scene::stop(asset),
            Control::Loop => scene::set_loop(asset, !state.looping),
            Control::Speed => scene::set_speed(asset, next_speed(state.speed)),
            Control::NextAsset => {}
        }
    }

    /// Put the current motion where the pointer is along the timeline.
    pub fn scrub(&mut self, ndc: Vec2) {
        let Some(asset) = self.asset else {
            return;
        };
        let state = scene::state(asset);
        if state.motion >= 0 {
            scene::seek(asset, timeline_fraction(ndc.x) * state.length);
        }
    }

    /// Follow a choice made in the game view, then redraw what changed since the last frame.
    pub fn frame(&mut self) {
        if let Some(chosen) = scene::selected().filter(|&c| self.asset != Some(c)) {
            self.show(chosen);
        }
        let Some(asset) = self.asset else {
            return;
        };
        let state = scene::state(asset);
        if self.shown.is_some_and(|s| same_display(&s, &state)) {
            return;
        }
        let motion_changed = self.shown.is_none_or(|s| s.motion != state.motion);
        let controls_changed = self.shown.is_none_or(|s| (s.paused, s.looping, s.speed) != (state.paused, state.looping, state.speed));
        self.shown = Some(state);

        if motion_changed {
            self.redraw_motions();
        }
        if controls_changed {
            self.redraw_controls();
        }
        let fraction = if state.length > 0.0 { (state.time / state.length).clamp(0.0, 1.0) } else { 0.0 };
        let fill_max = Vec2 {
            x: TRACK_MIN.x + (TRACK_MAX.x - TRACK_MIN.x) * fraction as f32,
            y: TRACK_MAX.y,
        };
        self.fill.submit(&forma::rect(TRACK_MIN, fill_max, ui::FILL));
        self.clock.set(&clock_text(&state, &self.motions), ui::DIM, ui::CLEAR);
    }

    fn redraw_lists(&mut self) {
        let title = match self.asset {
            Some(asset) => format!("player: {}  ({} motions)", scene::asset_name(asset), self.motions.len()),
            None => "player: load an asset from the assets window".to_string(),
        };
        self.title.set(&title, ui::TEXT, ui::CLEAR);
        self.redraw_motions();
        self.redraw_controls();
    }

    fn redraw_motions(&self) {
        let current = self.shown.map_or(-1, |s| s.motion);
        for (i, row) in self.motion_rows.iter().enumerate() {
            match self.motions.get(i) {
                Some(motion) => {
                    let background = if i as i32 == current { ui::SELECTED } else { ui::ROW };
                    row.set(&format!("{}  {:.2} s", motion.name, motion.length), ui::TEXT, background);
                }
                None if i == 0 && self.asset.is_some() => row.set("no motions, shown at rest", ui::DIM, ui::CLEAR),
                None => row.set("", ui::TEXT, ui::CLEAR),
            }
        }
    }

    fn redraw_controls(&self) {
        let state = self.shown;
        for (row, control) in self.controls.iter().zip(CONTROL_ORDER) {
            row.set(&control_label(control, state.as_ref()), ui::TEXT, ui::ROW);
        }
    }
}

fn control_label(control: Control, state: Option<&PlayerState>) -> String {
    match control {
        Control::PlayPause => match state {
            Some(s) if s.motion >= 0 && !s.paused => "pause".into(),
            _ => "play".into(),
        },
        Control::Stop => "stop (back to rest)".into(),
        Control::Loop => format!("loop: {}", if state.is_none_or(|s| s.looping) { "on" } else { "off" }),
        Control::Speed => format!("speed: {}x", state.map_or(1.0, |s| s.speed)),
        Control::NextAsset => "next asset".into(),
    }
}

/// The asset of `present` after `current`, wrapping round, or the first one when `current`
/// is none or gone. None when `present` is empty.
pub fn next_after(current: Option<usize>, present: &[usize]) -> Option<usize> {
    let after = current.and_then(|c| present.iter().copied().find(|&a| a > c));
    after.or_else(|| present.first().copied())
}

/// The speed after `speed` in SPEEDS, or the first one.
pub fn next_speed(speed: f64) -> f64 {
    let at = SPEEDS.iter().position(|s| (s - speed).abs() < 1e-9);
    SPEEDS[at.map_or(0, |i| (i + 1) % SPEEDS.len())]
}

/// Where `x` lies along the timeline, 0 at its left end and 1 at its right, clamped.
pub fn timeline_fraction(x: f32) -> f64 {
    f64::from(((x - TRACK_MIN.x) / (TRACK_MAX.x - TRACK_MIN.x)).clamp(0.0, 1.0))
}

fn clock_text(state: &PlayerState, motions: &[MotionInfo]) -> String {
    match usize::try_from(state.motion).ok().and_then(|m| motions.get(m)) {
        Some(motion) => format!(
            "{}  {:.2} / {:.2} s{}",
            motion.name,
            state.time,
            state.length,
            if state.paused { "  paused" } else { "" }
        ),
        None => "at rest".into(),
    }
}

/// Whether two states draw the same, to within a hundredth of a second.
fn same_display(a: &PlayerState, b: &PlayerState) -> bool {
    a.motion == b.motion
        && a.paused == b.paused
        && a.looping == b.looping
        && a.speed == b.speed
        && (a.time - b.time).abs() < 0.01
        && a.length == b.length
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn speed_steps_through_and_wraps() {
        assert_eq!(next_speed(1.0), 0.5);
        assert_eq!(next_speed(2.0), 1.0);
        assert_eq!(next_speed(3.0), 1.0);
    }

    #[test]
    fn next_asset_skips_removed_and_wraps() {
        assert_eq!(next_after(Some(1), &[0, 3, 4]), Some(3));
        assert_eq!(next_after(Some(4), &[0, 3, 4]), Some(0));
        assert_eq!(next_after(None, &[2, 5]), Some(2));
        assert_eq!(next_after(Some(2), &[]), None);
    }

    #[test]
    fn timeline_maps_and_clamps() {
        assert_eq!(timeline_fraction(TRACK_MIN.x), 0.0);
        assert_eq!(timeline_fraction(TRACK_MAX.x), 1.0);
        assert!((timeline_fraction(0.0) - 0.5).abs() < 1e-6);
        assert_eq!(timeline_fraction(5.0), 1.0);
    }
}
