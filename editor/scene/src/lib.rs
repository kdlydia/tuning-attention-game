//! Rust access to the game's scene (`src/scene/` in the game) through a small C++ shim
//! (`cpp/scene_shim.cpp`). The C++ host attaches the scene before the editor starts.
//!
//! Call everything from the graphics thread, as the editor's callbacks and frame hook are.

use std::path::Path;

#[cxx::bridge(namespace = "mf_scene")]
mod ffi {
    /// One motion of an asset.
    #[derive(Clone, Debug)]
    struct MotionInfo {
        name: String,
        /// Seconds.
        length: f64,
    }

    /// Where an asset's player is, as of the last frame.
    #[derive(Clone, Copy, Debug, PartialEq)]
    struct PlayerState {
        /// The motion playing or paused, or -1 at rest.
        motion: i32,
        /// Seconds into the motion.
        time: f64,
        /// Seconds the motion lasts, 0 at rest.
        length: f64,
        paused: bool,
        speed: f64,
        looping: bool,
    }

    unsafe extern "C++" {
        include!("scene_shim.h");

        fn load(path: &str) -> i32;
        fn remove(asset: usize) -> bool;
        fn asset_count() -> usize;
        fn present(asset: usize) -> bool;
        fn asset_name(asset: usize) -> String;
        fn asset_file(asset: usize) -> String;
        fn motions(asset: usize) -> Vec<MotionInfo>;
        fn play(asset: usize, motion: usize) -> bool;
        fn pause(asset: usize) -> bool;
        fn resume(asset: usize) -> bool;
        fn stop(asset: usize);
        fn seek(asset: usize, seconds: f64) -> bool;
        fn set_speed(asset: usize, value: f64);
        fn set_loop(asset: usize, looping: bool);
        fn state(asset: usize) -> PlayerState;
    }
}

pub use ffi::{MotionInfo, PlayerState};

/// Load a model or Alembic file into the scene. Returns the asset's index.
pub fn load(path: &Path) -> Option<usize> {
    usize::try_from(ffi::load(&path.to_string_lossy())).ok()
}

/// Take an asset out of the scene. Every other asset keeps its index.
pub fn remove(asset: usize) -> bool {
    ffi::remove(asset)
}

/// One more than the highest asset index given so far, removed ones included.
pub fn asset_count() -> usize {
    ffi::asset_count()
}

/// Whether there is an asset at `asset`: false once it has been removed.
pub fn present(asset: usize) -> bool {
    ffi::present(asset)
}

/// The indices of the assets in the scene, in the order they were loaded.
pub fn assets() -> Vec<usize> {
    (0..asset_count()).filter(|&a| present(a)).collect()
}

pub fn asset_name(asset: usize) -> String {
    ffi::asset_name(asset)
}

/// The file an asset was loaded from, as it was given.
pub fn asset_file(asset: usize) -> String {
    ffi::asset_file(asset)
}

pub fn motions(asset: usize) -> Vec<MotionInfo> {
    ffi::motions(asset)
}

/// Play a motion from its start, crossfading from what showed before. Asking for the
/// motion that is already current resumes it.
pub fn play(asset: usize, motion: usize) -> bool {
    ffi::play(asset, motion)
}

pub fn pause(asset: usize) -> bool {
    ffi::pause(asset)
}

pub fn resume(asset: usize) -> bool {
    ffi::resume(asset)
}

/// Crossfade back to the rest pose.
pub fn stop(asset: usize) {
    ffi::stop(asset);
}

/// Put the current motion at `seconds` from its start.
pub fn seek(asset: usize, seconds: f64) -> bool {
    ffi::seek(asset, seconds)
}

/// Clock seconds per second for every motion of the asset. Negative plays backwards.
pub fn set_speed(asset: usize, value: f64) {
    ffi::set_speed(asset, value);
}

pub fn set_loop(asset: usize, looping: bool) {
    ffi::set_loop(asset, looping);
}

pub fn state(asset: usize) -> PlayerState {
    ffi::state(asset)
}
