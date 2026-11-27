//! The assets window: the model and Alembic files under the game's `assets/` folder.
//! Click one to load it into the scene, where it stands in the game view, and right-click
//! it to take the copy loaded last back out. A row shows how many copies are in the scene,
//! read from the scene itself, so assets loaded some other way count too.

use crate::ui::{self, row_bounds, Column, Row};
use forma::{Surface, Text};
use std::path::{Path, PathBuf};

/// What the scene can load: Assimp's common formats and Alembic.
const EXTENSIONS: [&str; 6] = ["glb", "gltf", "fbx", "obj", "dae", "abc"];
const WINDOW: (u32, u32) = (520, 640);
const ROWS: usize = 14;
const LIST: Column = Column { x_min: -0.95, x_max: 0.95, top: 0.78, pitch: 0.12, height: 0.10 };

pub struct AssetsWindow {
    _surface: Surface,
    title: Text,
    rows: Vec<Row>,
    root: PathBuf,
    files: Vec<PathBuf>,
}

impl AssetsWindow {
    /// Open the window over the files under `root`.
    pub fn open(root: PathBuf) -> Result<Self, String> {
        let surface = Surface::create("assets", WINDOW.0, WINDOW.1).map_err(|e| e.to_string())?;
        let (title_min, title_max) = ui::text_inset(ui::TITLE.0, ui::TITLE.1, WINDOW);
        let title = surface.add_text("assets_title", title_min, title_max, "", ui::TEXT, ui::CLEAR);
        let rows = (0..ROWS)
            .map(|i| {
                let (min, max) = row_bounds(&LIST, i);
                Row::add(&surface, &format!("asset_{i}"), min, max, WINDOW)
            })
            .collect();
        let mut window = AssetsWindow { _surface: surface, title, rows, root, files: Vec::new() };
        window.refresh();
        Ok(window)
    }

    pub fn surface(&self) -> &Surface {
        &self._surface
    }

    /// The element ids of the rows, in order.
    pub fn row_ids(&self) -> Vec<u32> {
        self.rows.iter().map(Row::id).collect()
    }

    /// Look at the folder again and redraw the list.
    pub fn refresh(&mut self) {
        self.files = find(&self.root);
        let more = if self.files.len() > ROWS { format!(", first {ROWS} shown") } else { String::new() };
        let folder = self.root.file_name().map_or_else(|| self.root.display().to_string(), |n| n.to_string_lossy().into_owned());
        self.title.set(
            &format!("{folder}: {} files{more}. Click to load, right-click to remove", self.files.len()),
            ui::TEXT,
            ui::CLEAR,
        );
        self.redraw();
    }

    /// Redraw every row from what the scene holds now.
    pub fn redraw(&self) {
        let loaded: Vec<(usize, String)> = scene::assets().into_iter().map(|a| (a, scene::asset_file(a))).collect();
        for (i, row) in self.rows.iter().enumerate() {
            let Some(file) = self.file(i) else {
                row.set("", ui::TEXT, ui::CLEAR);
                continue;
            };
            let copies = copies_of(file, &loaded).len();
            let name = file.strip_prefix(&self.root).unwrap_or(file).display();
            let (label, background) = match copies {
                0 => (name.to_string(), ui::ROW),
                1 => (format!("{name}  (in the scene)"), ui::SELECTED),
                n => (format!("{name}  ({n} in the scene)"), ui::SELECTED),
            };
            row.set(&label, ui::TEXT, background);
        }
    }

    /// The file on a row, if there is one.
    pub fn file(&self, row: usize) -> Option<&Path> {
        self.files.get(row).filter(|_| row < ROWS).map(PathBuf::as_path)
    }

    /// Take the copy of a row's file loaded last out of the scene. Returns its asset index.
    pub fn remove_last(&self, row: usize) -> Option<usize> {
        let file = self.file(row)?;
        let loaded: Vec<(usize, String)> = scene::assets().into_iter().map(|a| (a, scene::asset_file(a))).collect();
        let last = copies_of(file, &loaded).pop()?;
        let removed = scene::remove(last).then_some(last);
        self.redraw();
        removed
    }
}

/// The assets of `loaded`, given as (index, file), that came from `file`, in index order.
/// Paths are compared once resolved, since the scene keeps a file as it was given.
pub fn copies_of(file: &Path, loaded: &[(usize, String)]) -> Vec<usize> {
    let wanted = std::fs::canonicalize(file).unwrap_or_else(|_| file.to_path_buf());
    loaded
        .iter()
        .filter(|(_, other)| std::fs::canonicalize(other).unwrap_or_else(|_| PathBuf::from(other)) == wanted)
        .map(|&(asset, _)| asset)
        .collect()
}

/// Every file under `root` with a loadable extension, sorted by path.
pub fn find(root: &Path) -> Vec<PathBuf> {
    let mut found = Vec::new();
    let mut pending = vec![root.to_path_buf()];
    while let Some(dir) = pending.pop() {
        let Ok(entries) = std::fs::read_dir(&dir) else {
            continue;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                pending.push(path);
            } else if loadable(&path) {
                found.push(path);
            }
        }
    }
    found.sort();
    found
}

fn loadable(path: &Path) -> bool {
    path.extension()
        .and_then(|e| e.to_str())
        .is_some_and(|e| EXTENSIONS.iter().any(|x| x.eq_ignore_ascii_case(e)))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_models_in_subfolders_sorted() {
        let root = std::env::temp_dir().join(format!("assets_test_{}", std::process::id()));
        std::fs::create_dir_all(root.join("samples")).unwrap();
        for name in ["b.glb", "samples/a.FBX", "notes.md", "samples/clip.mp4", "walk.abc"] {
            std::fs::write(root.join(name), b"").unwrap();
        }
        let names: Vec<_> = find(&root)
            .iter()
            .map(|p| p.strip_prefix(&root).unwrap().to_string_lossy().into_owned())
            .collect();
        std::fs::remove_dir_all(&root).unwrap();
        assert_eq!(names, ["b.glb", "samples/a.FBX", "walk.abc"]);
    }

    #[test]
    fn copies_match_a_file_however_its_path_was_written() {
        let root = std::env::temp_dir().join(format!("copies_test_{}", std::process::id()));
        std::fs::create_dir_all(root.join("sub")).unwrap();
        std::fs::write(root.join("sub/fox.glb"), b"").unwrap();
        std::fs::write(root.join("barrel.glb"), b"").unwrap();
        let fox = root.join("sub/fox.glb");
        let loaded = vec![
            (0, root.join("sub/../sub/fox.glb").display().to_string()),
            (2, root.join("barrel.glb").display().to_string()),
            (5, fox.display().to_string()),
        ];
        let copies = copies_of(&fox, &loaded);
        std::fs::remove_dir_all(&root).unwrap();
        assert_eq!(copies, [0, 5]);
    }
}
