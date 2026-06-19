# Clay Raylib example

Tiny C demo using [Clay](https://github.com/nicbarker/clay) for layout and
[Raylib](https://www.raylib.com/) for rendering. It builds as a native
fullscreen desktop app and as a web canvas app.

## Project Structure

- `src/main.c` contains the app and high-DPI render-texture path.
- `src/clay_raylib_renderer.c` is the unity-included Clay-to-Raylib renderer.
- `include/clay.h` is the vendored Clay single header.
- `web/shell.html` is the minimal Emscripten shell. The canvas fills the browser
  viewport and the C code sizes the backing store for `devicePixelRatio`.
- `Makefile` builds native and web targets.
- `vendor/` is used by Make for downloaded Raylib source needed to build the web
  library.
- `build/` contains generated native, wasm, JS, and Emscripten cache output.

## Native Dependencies

Install the native and web toolchains with Homebrew, for example:

```sh
brew install raylib emscripten
```

The native build uses Clang and Homebrew's Raylib package through `pkg-config`.
The web build uses Emscripten and builds a local `libraylib.a` from Raylib 5.5
source, matching the Homebrew Raylib version.

## Build

```sh
make native
make web
```

Or build both:

```sh
make
```

Outputs:

- Native app: `build/native/clay_raylib_example`
- Web app: `build/web/index.html`

Run the native app:

```sh
make run
```

Serve the web build:

```sh
make serve
```

Then open `http://localhost:8080`.

Regenerate the clangd flags file:

```sh
make compile-flags.txt
```

This writes `compile_flags.txt`, which clangd reads from the project root.

## High-DPI Notes

Raylib 5.5 supports `FLAG_WINDOW_HIGHDPI` on desktop, so the native app lays out
in logical window pixels and renders into a texture sized with
`GetRenderWidth()`/`GetRenderHeight()`.

Raylib 5.5's web backend does not implement `GetWindowScaleDPI()`, so the web
app reads the canvas CSS size and browser `devicePixelRatio` with Emscripten,
resizes the canvas backing store to device pixels, renders Clay commands into a
matching texture, then draws that texture to the canvas.
