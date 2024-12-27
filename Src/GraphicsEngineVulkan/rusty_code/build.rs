// Build-scripts also need to be linked, so just add a dummy buildscript ensuring this works.
fn main() {
	cxx_build::bridge("src/lib.rs")
        .std("c++17")
        .compile("rusty_code");
    println!("Build-script is running.");
}
