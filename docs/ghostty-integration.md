# Ghostty terminal integration

Wave's terminal tabs are PTY-backed and use Ghostty VT as their terminal
backend.

- Terminal output is fed through `libghostty-vt` via
  `ghostty_terminal_vt_write`, snapshotted through `GhosttyRenderState`, and
  copied from Ghostty row/cell data into Wave's current text renderer.
- `USE_GHOSTTY_INTERNAL=1`: additionally force-loads Ghostty's full static
  `libghostty-internal-fat.a` artifact into the app binary. This is experimental
  and much larger, but it is still a static link path.

Build `libghostty-vt` with:

```sh
make ghostty-vt
make test
make app
```

This requires Zig 0.15.2, because Ghostty pins the C library build to that Zig
release. If another Zig is first on `PATH`, pass the version explicitly:

```sh
make ZIG=/opt/homebrew/opt/zig@0.15/bin/zig ghostty-vt
make ZIG=/opt/homebrew/opt/zig@0.15/bin/zig app
```

There is also an experimental probe for Ghostty's non-VT library artifacts:

```sh
make ZIG=/opt/homebrew/opt/zig@0.15/bin/zig ghostty-internal
make ZIG=/opt/homebrew/opt/zig@0.15/bin/zig USE_GHOSTTY_INTERNAL=1 app
```

Wave's terminal code uses the `ghostty/vt.h` C API directly. The Makefile links
static archives only:

- `vendor/ghostty/zig-out/lib/libghostty-vt.a`
- `vendor/ghostty/macos/GhosttyKit.xcframework/macos-arm64/libghostty-internal-fat.a`

Do not link the Ghostty `.dylib` symlinks from `zig-out/lib`; release builds
should stay self-contained.

The larger `include/ghostty.h` surface exists, but Ghostty's own source marks
that artifact as historical/internal GUI glue rather than the stable embeddable
API. For Wave, the safer path is:

1. Use `libghostty-vt` for terminal state, escape parsing, scrollback, effects,
   and input encoding.
2. Keep `GhosttyRenderState` as the bridge from terminal state into Wave's
   renderer.
3. Extend Wave's OpenGL text path to consume Ghostty cell attributes so colors,
   cursor style, selection, and dirty rows render directly instead of being
   flattened to strings.
4. Treat the full static `libghostty` archive as an experimental embedded
   payload until Ghostty publishes a stable app/surface API that can accept
   Wave's GLFW/OpenGL or macOS layer ownership cleanly.
