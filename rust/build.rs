use std::path::PathBuf;

/// `OUT_DIR` is `<target>/<profile>/build/<pkg>-<hash>/out`; the binary lands in
/// `<target>/<profile>`.
fn out_dir_to_profile_dir() -> Option<PathBuf> {
    let out = PathBuf::from(std::env::var("OUT_DIR").ok()?);
    Some(out.parent()?.parent()?.parent()?.to_path_buf())
}

fn main() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("crate lives one level under the repo root")
        .to_path_buf();

    // Where the C half of this build put its single-arch artifacts. The Makefile
    // overrides both when cross-compiling (build/<arch>, zig-out-<arch>); a bare
    // `cargo build` keeps the host layout. Getting these wrong does not fail the
    // build — it links the *other* architecture's archive and ld reports an
    // undefined-symbol wall, so they are read from the environment rather than
    // guessed from TARGET.
    let lib_dir = std::env::var("WAVE_LIB_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| root.join("build"));
    let ghostty_prefix = std::env::var("WAVE_GHOSTTY_PREFIX")
        .map(PathBuf::from)
        .unwrap_or_else(|_| root.join("vendor/ghostty/zig-out"));

    // The version this build reports to the updater. The Makefile passes its own
    // VERSION so the release binary and the bundle's Info.plist agree; a bare
    // `cargo build` falls back to the crate version. Either way an installed
    // Wave.app prefers CFBundleShortVersionString at runtime, so this only
    // decides what an unbundled binary compares against.
    let version = std::env::var("WAVE_VERSION")
        .unwrap_or_else(|_| std::env::var("CARGO_PKG_VERSION").unwrap());

    // Same flags the Makefile compiles CORE_SRC with, so the shim agrees with
    // libwave.a on struct layout (notably the ghostty-VT-dependent Terminal).
    cc::Build::new()
        .file("shim/wave_ffi.c")
        .include(root.join("src"))
        .include(root.join("vendor"))
        .include(root.join("vendor/tree-sitter/lib/include"))
        .include(ghostty_prefix.join("include"))
        .define("WAVE_USE_GHOSTTY_VT", None)
        .define("GHOSTTY_STATIC", None)
        .define("WAVE_VERSION", format!("\"{version}\"").as_str())
        .flag("-std=c11")
        // The Makefile's CFLAGS carry this too. Several shim entry points take a
        // session they do not read, to keep one shape across the whole ABI.
        .flag("-Wno-unused-parameter")
        .compile("wave_ffi");

    // Link both archives by absolute path, exactly as the Makefile does.
    // `rustc-link-lib=static=` is not enough on Mach-O: with a libghostty-vt
    // .dylib sitting beside the .a, ld picks the dylib and the binary dies at
    // launch with "no LC_RPATH's found".
    println!(
        "cargo:rustc-link-arg={}",
        lib_dir.join("libwave.a").display()
    );
    println!(
        "cargo:rustc-link-arg={}",
        ghostty_prefix.join("lib/libghostty-vt.a").display()
    );

    // What the Makefile's TEST_LIBS links for the headless core.
    println!("cargo:rustc-link-lib=framework=CoreServices");
    println!("cargo:rustc-link-lib=framework=CoreFoundation");
    // updater_mac.o (now a core object) needs NSURLSession/NSTask, and one
    // AppKit call to quit once the installer is running.
    println!("cargo:rustc-link-lib=framework=Foundation");
    println!("cargo:rustc-link-lib=framework=AppKit");

    // runtime.c resolves the bundled ripgrep and TS language server *relative to
    // the executable* (`<exe_dir>/vendor/...`, `<exe_dir>/../vendor/...`). That
    // works for the Makefile's `build/wave`, but `target/debug/wave-gpui` is two
    // levels deeper, so project search would silently report "rg unavailable".
    // Link the vendor tree next to the binary so those probes hit.
    if let Some(profile_dir) = out_dir_to_profile_dir() {
        let link = profile_dir.join("vendor");
        let target = root.join("vendor");
        if target.is_dir() && !link.exists() {
            #[cfg(unix)]
            let _ = std::os::unix::fs::symlink(&target, &link);
        }
    }

    println!("cargo:rerun-if-env-changed=WAVE_VERSION");
    println!("cargo:rerun-if-env-changed=WAVE_LIB_DIR");
    println!("cargo:rerun-if-env-changed=WAVE_GHOSTTY_PREFIX");
    println!("cargo:rerun-if-changed=shim/wave_ffi.c");
    println!(
        "cargo:rerun-if-changed={}",
        lib_dir.join("libwave.a").display()
    );
}
