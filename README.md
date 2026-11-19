# Seam Carving (CSD3131 Assignment 2)

Content-aware image resizing by seam carving (Avidan & Shamir), with three seam finders:
**dynamic programming**, **greedy** and **graph cut (min-cut / max-flow)**, for both vertical and horizontal seams.

The engine is plain C++17 with no dependencies. It is compiled two ways:

- to **WebAssembly**, for the **Seam Carving Visualiser**: a browser app that steps through carving one seam at a
  time (find, then remove, with undo), shows the energy map, and lets you compare the three algorithms on any image;
- **natively**, for a **benchmark suite** that times every algorithm and variant and checks that they agree.

No image library is used for the carving itself: the energy map, seam search, seam removal and resizing are all
implemented here (see "Third-party code" below).

## Where each part of the assignment is

| Part | What | Where |
|---|---|---|
| 1a | Energy map: Sobel `\|Gx\| + \|Gy\|`, summed over R, G, B, replicated borders | `src/energy.cpp` (`computeEnergy`) |
| 1b | Minimum seam by dynamic programming | `src/seam_dp.cpp` (`findVerticalSeamDP`) |
| 1b | Horizontal seams (DP on the transposed image / energy map) | `src/carve.cpp` (`carveHorizontal`), `src/carver.cpp` |
| 1c | Remove the seam and repeat, recomputing the energy | `src/carve.cpp` (`removeVerticalSeam`, `carveVertical`) |
| 2 | Written answers (seam count, recurrence, divide and conquer for convolution) | Report |
| 3 | Greedy seam finder | `src/seam_greedy.cpp` |
| 4 | Graph cut seam finder: min s-t cut solved with Dinic's max-flow | `src/seam_graphcut.cpp` |
| 5b | Time and space complexity | Report; summary in "How it works" below; comments at the top of each `src/*.cpp` |
| 5c | Comparison of DP, Greedy and Graph Cut: seams, visual quality, running time | Visualiser (side by side on the same image); benchmark section 2 |
| 5d | Compact DP table (2 rows + int8 back-pointers, about 12x less memory) | `src/seam_dp_variants.cpp` (`findVerticalSeamDPCompact`) |
| 5d | Multi-threaded DP (OpenMP) | `src/seam_dp_variants.cpp` (`findVerticalSeamDPParallel`); benchmark section 1 |
| 5d | Incremental energy update: only about 3 pixels per row recomputed after each seam | `src/energy.cpp` (`updateEnergyAfterSeamRemoval`); benchmark section 3 |

## Running the visualiser

The WebAssembly module is already built (`web/dist/`), so only Node.js is needed.

**With Node.js** (20.19 or newer; 22 recommended):
```
npm install
npm run dev
```
Then open the URL it prints (usually http://localhost:5173). `npm run build` writes a static copy of the site to
`site/`.

**Or with Docker**, which builds everything from source, including the C++ engine:
```
docker compose up --build
```
Then open http://localhost:8080. Stop it with `docker compose down`.

**Using it.** The visualiser opens with a generated sample picture. Use **Open image…** (or drop a file onto the
picture) to load your own; test photos are in `testimages/test_photos.zip`. Each press of **Find seam →** /
**Remove seam →** is one step: the next seam is found and drawn in red, then removed. **← Back** undoes a step exactly.
Choose the algorithm and the direction (vertical seams make the image narrower, horizontal seams make it shorter) under
Settings, and switch the view between the image, its energy map, and every removed seam drawn on the original.
**Remove many at once** carves several seams in one go. New images are scaled to 400 px on their longest side, because
Graph Cut takes seconds per seam on larger ones; set another size (up to 2000 px) under Image.

| Key | Action |
|---|---|
| → / ← | step forward / back |
| Space | play / pause |
| A | next algorithm |
| D | toggle direction |
| E | next view |
| R | reset |
| + / - | faster / slower playback |
| O | open an image |

The engine runs in a Web Worker (`web/worker.js`), so the page stays responsive while Graph Cut works.

## Running the benchmark

Needs a C++17 compiler and CMake 3.16 or newer (Visual Studio 2022, GCC or Clang). No Emscripten, no OpenCV.
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
build/seambench                       # Windows: build\Release\seambench.exe
```

It takes about 20 seconds and prints three tables, also written to `bench-results/` as CSV files and a Markdown
`summary.md`:

1. **DP seam finder:** full table vs compact vs OpenMP with 1, 2, 4, … threads, with the memory each one needs.
2. **DP vs Greedy vs Graph Cut:** time per seam and seam cost. Graph Cut is only timed up to 512 px because it is slow.
3. **Carving N seams:** recomputing the whole energy map after each seam vs updating it incrementally.

Every timed variant is first checked against the baseline: the same seam as the full-table DP, the same carved image,
Graph Cut's seam cost and max-flow value both equal to DP's seam cost, Greedy never cheaper than DP, incremental energy
equal to a full recompute after every seam, and undo restoring the original image exactly. If any check fails, the
program says which and exits with code 2.

| Option | Default | Meaning |
|---|---|---|
| `--out DIR` | `bench-results` | Where to write the results |
| `--sizes A,B,...` | `256,512,1024,2048` | Longest image side of each test size |
| `--seams N` | `50` | Seams removed in the carving test |
| `--graphcut-max S` | `512` | Largest size Graph Cut is timed at |
| `--image FILE.ppm` | generated scene | Use your own photo (binary PPM, e.g. `magick photo.jpg photo.ppm`) |
| `--quick` | | Sizes 256 and 512 only, fewer repetitions (a few seconds) |

The default input is a generated landscape (sky, sun, ridges, a striped tower, a little noise), the same on every
machine. OpenMP is used when the compiler supports it; otherwise the parallel DP runs on one thread.

On GitHub, `.github/workflows/benchmark.yml` runs the benchmark on Linux (GCC) and Windows (MSVC) and shows the tables
on each run's Summary page.

## How it works

For an image with **m** rows and **n** columns (one vertical seam):

| Step | Method | Time | Extra space |
|---|---|---|---|
| Energy map | 3x3 Sobel in x and y, `\|Gx\| + \|Gy\|` summed over R, G, B. Integer arithmetic, so energies are exact | O(mn) | O(mn) |
| DP | `M[i][j] = e[i][j] + min(M[i-1][j-1], M[i-1][j], M[i-1][j+1])`, then backtrack from the smallest `M[m-1][j]` | O(mn) | O(mn): costs + back-pointers |
| Compact DP | Keeps only two cost rows and an int8 offset (-1, 0, +1) per pixel for backtracking | O(mn) | 2n doubles + mn bytes |
| Parallel DP | Each row's n cells are independent, so they are split across threads, with a barrier between rows | O(mn / p) + m barriers | as compact DP |
| Greedy | Start at the cheapest pixel of the first row, then step to the cheapest of the 3 pixels below | O(m + n) | O(m) |
| Graph Cut | One node per pixel; source to the first column, a dummy last column to the sink; each pixel's edge to its right neighbour has capacity e[i][j]; infinite backward and diagonal edges force one connected cut per row. Max-flow with Dinic's algorithm, then the seam is the rightmost source-side pixel in each row | O(V²E) worst case, V ≈ mn, E ≈ 4mn | O(mn) |
| Remove seam | Shift each row left past the seam pixel | O(mn) | O(mn) new image |
| Incremental energy | After a removal, only pixels whose 3x3 neighbourhood changed are recomputed: columns [min(s) − 1, max(s)] of each row, about 3 per row | O(m) recomputed + O(mn) shift | none |

Carving k seams costs k times one seam. **Horizontal seams** use the same code on the transposed image; the energy of
the transposed image is the transposed energy map, so it is not recomputed.

All three DP versions use the same tie-breaking (straight down, then left, then right), so they return identical
seams. Graph Cut returns a seam of exactly the same cost, which the benchmark checks against the max-flow value
(max-flow = min-cut = seam cost). Greedy only looks one row ahead, so its seams cost more (typically 20–30% more on
the test scene) and cut through objects more often.

The **step engine** (`src/carver.cpp`) powers the visualiser. It keeps the removed pixels of each seam, so undo puts
them back exactly, and caches computed steps so redo is instant.

## Building the WebAssembly module

Only needed after changing the C++ code; `web/dist/` is already built. Install
[Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (version 3.1.74 is what CI uses), then:
```
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```
This rewrites `web/dist/seam.js`, `seam.wasm` and `seam.d.ts`. The JavaScript API of the module is documented in
[docs/API.md](docs/API.md).

## Project structure

| Path | Contents |
|---|---|
| `src/image.h` | RGBA image type (same layout as a browser canvas's `ImageData`) |
| `src/seam.h` | Energy map type, seam type, and every seam finder's declaration |
| `src/energy.cpp` | Sobel energy map, transposes, incremental energy update |
| `src/seam_dp.cpp` | Dynamic programming seam finder (full table) |
| `src/seam_dp_variants.cpp` | Compact DP and OpenMP parallel DP |
| `src/seam_greedy.cpp` | Greedy seam finder |
| `src/seam_graphcut.cpp` | Graph cut seam finder (Dinic's max-flow) |
| `src/carve.h`, `carve.cpp` | Removing and re-inserting seams; carving k seams in a loop |
| `src/carver.h`, `carver.cpp` | `Carver`: the step engine (find / remove, undo / redo) used by the visualiser |
| `web/bindings.cpp` | Exposes `Carver` to JavaScript (Emscripten embind) |
| `web/dist/` | The built WebAssembly module (`seam.js`, `seam.wasm`, TypeScript types `seam.d.ts`) |
| `web/worker.js` | Web Worker that runs the engine off the page's main thread |
| `web/src/` | The visualiser (React): `App.jsx` user interface, `useCarver.js` worker hook, `sampleImage.js` generated sample picture |
| `bench/bench.cpp` | Native benchmark suite |
| `testimages/test_photos.zip` | Test photos, each at original size and scaled to 400 px |
| `docs/API.md` | JavaScript API of the WebAssembly module |
| `CMakeLists.txt` | Builds the WebAssembly module (with Emscripten) or the benchmark (native) |
| `package.json`, `vite.config.js` | Node.js setup for the visualiser |
| `Dockerfile`, `compose.yaml` | Builds and serves the whole site in Docker |
| `.github/workflows/` | GitHub Actions: build the module and the site, run the benchmark |

## Third-party code

The brief allows image and math libraries but not existing content-aware resizing functions. This project uses no
image library at all: images are decoded and drawn by the browser's canvas, and everything else is in `src/`.

- [Emscripten](https://emscripten.org/) compiles the C++ engine to WebAssembly.
- [React](https://react.dev/) and [Vite](https://vite.dev/) build the visualiser's user interface.
- [OpenMP](https://www.openmp.org/), from the compiler, provides the threads for the parallel DP (native only).
