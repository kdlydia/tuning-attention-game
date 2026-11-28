//! The game editor. The C++ host calls `editor_start` once and `editor_frame` every frame,
//! both on the graphics thread. Each tool is its own window.
//!
//! - The maze window: click a cell to toggle a wall. Every edit is saved to the game's
//!   `data/maze.txt` (format in `maze.rs`).
//! - The assets window (`assets.rs`): the files under the game's `assets/`. Click one to
//!   load it into the scene drawn in the game view.
//! - The player window (`player.rs`): the motions of a loaded asset and their transport.
//!   It shows the asset chosen in the game view: left-click one there, or load one.

mod assets;
mod maze;
mod player;
mod ui;

use assets::AssetsWindow;
use forma::{MouseButton, Surface, Target, Topology, Vec2, Vertex};
use maze::Maze;
use player::{PlayerWindow, CONTROL_ORDER};
use std::cell::{Cell, RefCell};
use std::path::{Path, PathBuf};

const GRID_MIN: Vec2 = Vec2 { x: -0.94, y: -0.94 };
const GRID_MAX: Vec2 = Vec2 { x: 0.94, y: 0.94 };
const FLOOR: [f32; 4] = [0.16, 0.17, 0.19, 1.0];
const WALL: [f32; 4] = [0.85, 0.83, 0.78, 1.0];
const NEW_MAZE_SIZE: usize = 16;

struct Editor {
    maze: MazeWindow,
    assets: Option<AssetsWindow>,
    player: Option<PlayerWindow>,
    frames: u64,
}

impl Editor {
    fn frame(&mut self) {
        self.frames += 1;
        if self.frames == 1 {
            forma::print("editor: frame hook running");
        }
        if let Some(player) = self.player.as_mut() {
            player.frame();
        }
    }
}

/// Load the file on a row of the assets window, then show it in the player. The editor is
/// not borrowed while the scene loads.
fn load_row(editor: &'static RefCell<Editor>, row: usize) {
    let file = editor.borrow().assets.as_ref().and_then(|a| a.file(row)).map(Path::to_path_buf);
    let Some(file) = file else {
        return;
    };
    let Some(asset) = scene::load(&file) else {
        forma::print(&format!("editor: could not load {}", file.display()));
        return;
    };
    let mut editor = editor.borrow_mut();
    if let Some(assets) = editor.assets.as_ref() {
        assets.redraw();
    }
    if let Some(player) = editor.player.as_mut() {
        player.show(asset);
    }
}

/// Take the copy of a row's file loaded last out of the scene.
fn remove_row(editor: &'static RefCell<Editor>, row: usize) {
    let mut editor = editor.borrow_mut();
    let Some(removed) = editor.assets.as_ref().and_then(|a| a.remove_last(row)) else {
        return;
    };
    if let Some(player) = editor.player.as_mut() {
        player.forget(removed);
    }
}

/// Run `f` on the player window, if it opened.
fn with_player(editor: &'static RefCell<Editor>, f: impl FnOnce(&mut PlayerWindow)) {
    if let Some(player) = editor.borrow_mut().player.as_mut() {
        f(player);
    }
}

/// Route the assets and player windows' presses and drags to the editor.
fn bind_tools(editor: &'static RefCell<Editor>) {
    let tools = editor.borrow();
    if let Some(assets) = &tools.assets {
        for (row, id) in assets.row_ids().into_iter().enumerate() {
            assets.surface().on_press(id, MouseButton::Left, move |_, _| load_row(editor, row));
            assets.surface().on_press(id, MouseButton::Right, move |_, _| remove_row(editor, row));
        }
    }
    if let Some(player) = &tools.player {
        let surface = player.surface();
        for (row, id) in player.motion_ids().into_iter().enumerate() {
            surface.on_press(id, MouseButton::Left, move |_, _| with_player(editor, |p| p.press_motion(row)));
        }
        for (id, control) in player.control_ids().into_iter().zip(CONTROL_ORDER) {
            surface.on_press(id, MouseButton::Left, move |_, _| with_player(editor, |p| p.press_control(control)));
        }
        let track = player.track_id();
        surface.on_press(track, MouseButton::Left, move |_, ndc| with_player(editor, |p| p.scrub(ndc)));
        surface.on_drag(track, MouseButton::Left, move |_, ndc| with_player(editor, |p| p.scrub(ndc)));
    }
}

struct MazeWindow {
    _surface: Surface,
    grid: Target,
    maze: Maze,
    path: PathBuf,
}

impl MazeWindow {
    fn press(&mut self, ndc: Vec2) {
        let Some((row, col)) = cell_at(&self.maze, ndc) else {
            return;
        };
        self.maze.toggle(row, col);
        self.grid.submit(&grid_vertices(&self.maze));
        save(&self.path, &self.maze);
    }
}

/// Write the maze through a temporary file and a rename, so the game view never
/// reads a half-written file.
fn save(path: &Path, maze: &Maze) {
    let tmp = path.with_extension("txt.tmp");
    let result = path
        .parent()
        .map_or(Ok(()), std::fs::create_dir_all)
        .and_then(|()| std::fs::write(&tmp, maze.to_text()))
        .and_then(|()| std::fs::rename(&tmp, path));
    if let Err(error) = result {
        forma::print(&format!("maze: could not save {}: {error}", path.display()));
    }
}

thread_local! {
    // Leaked on purpose, like keep_alive() in a Weave project: Forma objects dropped
    // after MayaFlux::End() reach into an engine that no longer exists.
    // Forma runs a callback inside the window event that causes it, and the callbacks
    // borrow the editor, so never signal a window event while the editor is borrowed.
    static EDITOR: Cell<Option<&'static RefCell<Editor>>> = const { Cell::new(None) };
}

/// The game repo, two folders above this crate.
fn game_root() -> PathBuf {
    let crate_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
    crate_dir.ancestors().nth(2).unwrap_or(crate_dir).to_path_buf()
}

fn maze_path() -> PathBuf {
    game_root().join("data/maze.txt")
}

/// The saved maze, a new bordered one (saved right away, so the game view has a file)
/// if there is none, or None if the file can't be used.
fn load(path: &Path) -> Option<Maze> {
    match std::fs::read_to_string(path) {
        Ok(text) => match Maze::parse(&text) {
            Ok(maze) => {
                forma::print(&format!("maze: loaded {}x{} from {}", maze.cols, maze.rows, path.display()));
                Some(maze)
            }
            Err(error) => {
                forma::print(&format!("maze: {} is not a maze: {error}. Fix or delete it.", path.display()));
                None
            }
        },
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
            forma::print(&format!(
                "maze: no {}; starting a {NEW_MAZE_SIZE}x{NEW_MAZE_SIZE} maze there",
                path.display()
            ));
            let maze = Maze::bordered(NEW_MAZE_SIZE, NEW_MAZE_SIZE);
            save(path, &maze);
            Some(maze)
        }
        Err(error) => {
            forma::print(&format!("maze: could not read {}: {error}", path.display()));
            None
        }
    }
}

/// The cell under `p`, as (row, column) with row 0 at the top.
fn cell_at(maze: &Maze, p: Vec2) -> Option<(usize, usize)> {
    let fx = (p.x - GRID_MIN.x) / (GRID_MAX.x - GRID_MIN.x);
    let fy = (GRID_MAX.y - p.y) / (GRID_MAX.y - GRID_MIN.y);
    if !(0.0..1.0).contains(&fx) || !(0.0..1.0).contains(&fy) {
        return None;
    }
    let col = ((fx * maze.cols as f32) as usize).min(maze.cols - 1);
    let row = ((fy * maze.rows as f32) as usize).min(maze.rows - 1);
    Some((row, col))
}

/// One quad per cell, with a small gap so the grid shows.
fn grid_vertices(maze: &Maze) -> Vec<Vertex> {
    let w = (GRID_MAX.x - GRID_MIN.x) / maze.cols as f32;
    let h = (GRID_MAX.y - GRID_MIN.y) / maze.rows as f32;
    let (gap_x, gap_y) = (w * 0.08, h * 0.08);
    let mut vertices = Vec::with_capacity(maze.rows * maze.cols * 6);
    for row in 0..maze.rows {
        for col in 0..maze.cols {
            let min = Vec2 {
                x: GRID_MIN.x + col as f32 * w + gap_x / 2.0,
                y: GRID_MAX.y - (row + 1) as f32 * h + gap_y / 2.0,
            };
            let max = Vec2 {
                x: min.x + w - gap_x,
                y: min.y + h - gap_y,
            };
            let color = if maze.is_wall(row, col) { WALL } else { FLOOR };
            vertices.extend(forma::rect(min, max, color));
        }
    }
    vertices
}

#[no_mangle]
pub extern "C" fn editor_start() {
    let path = maze_path();
    let Some(maze) = load(&path) else {
        return;
    };

    let surface = match Surface::create("maze", 720, 720) {
        Ok(surface) => surface,
        Err(error) => {
            forma::print(&format!("editor: could not create the maze window: {error}"));
            return;
        }
    };
    let grid = surface.slice(Topology::TriangleList);
    grid.submit(&grid_vertices(&maze));
    let id = surface.add_element("maze_grid", GRID_MIN, GRID_MAX, &grid);

    let assets = AssetsWindow::open(game_root().join("assets"))
        .inspect_err(|error| forma::print(&format!("editor: could not open the assets window: {error}")))
        .ok();
    let mut player = PlayerWindow::open()
        .inspect_err(|error| forma::print(&format!("editor: could not open the player window: {error}")))
        .ok();
    if let (Some(player), Some(last)) = (player.as_mut(), scene::asset_count().checked_sub(1)) {
        player.show(last);
    }

    let editor: &'static RefCell<Editor> = Box::leak(Box::new(RefCell::new(Editor {
        maze: MazeWindow {
            _surface: surface.clone(),
            grid,
            maze,
            path,
        },
        assets,
        player,
        frames: 0,
    })));
    surface.on_press(id, MouseButton::Left, move |_, ndc| editor.borrow_mut().maze.press(ndc));
    bind_tools(editor);
    EDITOR.with(|cell| cell.set(Some(editor)));
}

#[no_mangle]
pub extern "C" fn editor_frame() {
    if let Some(editor) = EDITOR.with(Cell::get) {
        editor.borrow_mut().frame();
    }
}
