// Compiles the generated half of the bridge. The shim header it includes names no
// MayaFlux type, so this step needs no MayaFlux flags; CMake compiles the shim itself.
fn main() {
    cxx_build::bridge("src/lib.rs")
        .include("cpp")
        .std("c++20")
        .compile("scene_bridge");

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=cpp/scene_shim.h");
}
