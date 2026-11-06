# Seam Carving (CSD3131 Assignment 2)

C++17. The algorithm core (`seamcore`) is plain C++ with **no OpenCV**, so the same code builds:
- **natively** (Visual Studio / CMake), where OpenCV handles only image files and the viewer window, and
- **for the web** (Emscripten → WebAssembly), running entirely in the browser.

Programs:
- **seamcarve**: batch runner. Carves with DP, Greedy and Graph Cut, saves images + timings.
- **seamviewer**: desktop window. Step through each seam being found and removed, forwards and backwards.
- **seambench**: Part 5(d) benchmark. Compact DP, parallel DP and incremental energy vs the baseline.
- **web/**: the same step-by-step visualiser in the browser, plus the `seam.js` / `seam.wasm` API for a web app.
  See **[docs/API.md](docs/API.md)**.

## Layout

| File | What |
|---|---|
| `src/image.h` | RGBA `Image` type used by the core (same layout as a browser canvas) |
| `src/seam.h` | Shared types (`EnergyMap`, `Seam`, `SeamFinder`) and finder declarations |
| `src/energy.cpp` | Sobel energy `|gx| + |gy|` summed over R, G, B; incremental update (5d); transpose |
| `src/seam_dp.cpp` | Part 1: DP seam finder (full table) |
| `src/seam_dp_variants.cpp` | Part 5d: compact DP (2 rows + int8) and OpenMP parallel DP |
| `src/seam_greedy.cpp` | Part 3: greedy seam finder |
| `src/seam_graphcut.cpp` | Part 4: min-cut / max-flow seam finder (Dinic) |
| `src/carve.cpp/.h` | Remove/insert seams, remove-and-repeat loop, horizontal via transpose, timing, overlays |
| `src/carver.cpp/.h` | `Carver`: stateful step-by-step engine (find/remove, undo/redo, incremental energy), shared by the desktop viewer and the web build |
| `src/cv_bridge.h` | Native only: `cv::Mat` ⟷ `Image` conversion |
| `src/main.cpp` | `seamcarve` batch runner |
| `src/bench.cpp` | `seambench` Part 5d benchmark |
| `src/viewer_app.cpp/.h` | Desktop viewer UI + rendering (on top of `Carver`) |
| `src/viewer_main.cpp` | Desktop viewer window, keyboard, mouse, file picker |
| `web/bindings.cpp` | Emscripten/embind bindings: exposes `Carver` to JavaScript |
| `web/index.html`, `web/src/` | React visualiser, built with Vite |
| `web/worker.js` | Web Worker that runs the engine for the visualiser |
| `web/dist/` | Built `seam.js`, `seam.wasm`, `seam.d.ts` |
| `Dockerfile` | Builds the WASM module and the visualiser from source, serves them with nginx |
| `docs/API.md` | JavaScript API guide for the web app |
| `SeamCarving.sln`, `vs/` | Visual Studio solution, projects, shared property sheets |
| `.github/workflows/build.yml` | CI: WASM and visualiser builds, Docker image, Releases, GitHub Pages |

## Build with Visual Studio 2022 (recommended)

**1. Install OpenCV (one time).** Download the Windows package from https://opencv.org/releases/
(`opencv-4.x.x-windows.exe`), run it, and extract to `C:\`. You should end up with `C:\opencv\build\include`.
- Somewhere else? Set an `OPENCV_DIR` environment variable to that `...\opencv\build` folder and restart VS,
  or edit the `OpenCVDir` line in `vs\OpenCV.props`.
- The library names (`opencv_world4xx.lib`) are detected automatically, and the DLL is copied next to the .exe after each build. No PATH setup is needed.

**2. Open `SeamCarving.sln`.** It has 4 projects:

| Project | Type | |
|---|---|---|
| `seamviewer` | exe | step-by-step viewer (**startup project**: F5 opens a file picker) |
| `seamcarve` | exe | batch runner |
| `seambench` | exe | Part 5d benchmark |
| `seamcore` | static lib | all the algorithms, shared by the three exes |

**3. Pick the configuration** in the toolbar: **Release | x64** for anything you time, Debug | x64 for stepping through code in the debugger.

**4. Run.**
- To run another project, right-click it > **Set as Startup Project**.
- `seamcarve` and `seambench` come with preset debug arguments (`images\photo.jpg ...`), so put a photo there or change them in
  Project > Properties > Debugging > Command Arguments.
- Programs run from the solution folder. Executables end up in `bin\Release\` or `bin\Debug\`.

## Build with CMake (alternative)
`CMakeLists.txt` is kept too, e.g. for VS "Open Folder" or other compilers.
With the OpenCV package above:
```
cmake -B build -DOpenCV_DIR=C:/opencv/build
cmake --build build --config Release
```
(then add `C:\opencv\build\x64\vc16\bin` to PATH, or copy `opencv_world*.dll` next to the exes).

## Web build (WebAssembly)

Needs [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (not Visual Studio):
```
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```
This writes `web/dist/seam.js`, `seam.wasm` and `seam.d.ts`.

## Run the visualiser

**With Node.js 22** (uses the committed `web/dist/`, so Emscripten isn't needed):
```
npm install
npm run dev
```
Then open the URL it prints. `npm run build` writes the static site to `site/`, and `npm run preview` serves it.

**With Docker** (builds everything from source):
```
docker build -t seam-carving .
docker run --rm -p 8080:80 seam-carving
```
Then open http://localhost:8080.

## GitHub Actions

On GitHub-hosted runners, `.github/workflows/build.yml` does the following:
- builds the WASM module and the visualiser on every push and pull request,
- builds the Docker image, and publishes it to `ghcr.io` except on pull requests,
- attaches `seam.js` / `seam.wasm` / `seam.d.ts` to a Release when you push a tag such as `v1.0.0`,
- publishes the visualiser to GitHub Pages from `main`. A repo admin must first set Settings > Pages > Source: GitHub Actions,
  then add the repository variable `PAGES_ENABLED` = `true`. On a private repo, Pages also needs a paid GitHub plan.

## seamviewer (step-by-step visualiser)
```
bin\Release\seamviewer.exe [image] [maxSize=800]
```
- No argument: a file picker opens. You can also drag an image onto `seamviewer.exe`.
- Images with a longest side above `maxSize` are downscaled so stepping stays interactive.
- Each seam takes two steps: **highlight** (seam drawn in red) then **remove**.
  Stepping back re-inserts the removed pixels exactly. Steps already computed are cached, so going back and forward again is instant.

| Key | Button | Action |
|---|---|---|
| Right / Left arrow | Next > / < Back | step forward / back |
| Space | Play / Pause | auto-play |
| A | Algo | DP -> Greedy -> Graph Cut |
| D | Dir | vertical / horizontal seams |
| E | View | image / energy map |
| R | Reset | back to the original |
| + / - | Faster / Slower | play speed |
| O | Open... | load another image |
| Esc / Q | | quit |

The status line shows each seam's cost and how long the finder took. Graph Cut is slow: about 1-2 s per seam at 400 px.

## seamcarve (batch runner)
```
bin\Release\seamcarve.exe <image> <colsToRemove> <rowsToRemove> [outDir=output] [algos=dp,greedy,graphcut]
```
Example: `bin\Release\seamcarve.exe images\photo.jpg 150 50 output dp,greedy` (put any test photo in `images\`)

Outputs in `outDir`:
- `energy.png`: energy map of the original
- `<algo>_result.png`: carved image
- `<algo>_seams_vertical.png` / `_horizontal.png`: every removed seam drawn in red on the original
- `timings.csv`: appended each run (find / energy / removal ms, average seam cost), ready for Part 5 tables

Graph Cut takes minutes for many seams on a big image. Use a small image or few seams for it.

## seambench (Part 5d)
```
bin\Release\seambench.exe <image> [seams=50] [outDir=bench]
```
Use a large photo (2000+ px) so all four test sizes are meaningful. It prints tables and writes `bench_dp.csv` + `bench_energy.csv`.
Every variant is first checked to give **identical** seams/results to the baseline.
**Run it on your own machine for the report:** my results below come from a 2-core cloud VM.

**1. Do we need the entire DP table? No.**
Row i of M only reads row i-1, so 2 rolling rows are enough: O(n) instead of O(mn).
Backtracking still needs one decision per pixel, but stored as an int8 offset {-1,0,+1} instead of an int column.
That's about 12x less memory, and somewhat faster thanks to better cache use. Time stays O(mn).

**2. Does multi-threading help? Only on big images.**
Rows are sequential (row i needs row i-1), but the cells within a row are independent, so each row is split across threads.
That costs one barrier per row, i.e. m synchronisations per seam with only O(n) work between them.
On small images the overhead outweighs the gain. Expect more threads to pay off more on wider images.

**3. The real bottleneck: recomputing the energy map.**
After removing a seam, a pixel's 3x3 neighbourhood only changes within about 1 pixel of the seam.
Each row then needs just columns [min(s) - 1, max(s)] recomputed (s = seam columns in rows i-1..i+1), usually ~3 pixels.
So the Sobel work drops from O(mn) to O(m) per seam. Shifting the map is still a cheap O(mn) copy, like removing the seam from the image.
The result is verified bit-identical to a full recompute after every seam.

Sample results (2-core VM, Broadway Tower photo):

| Size | Full table | Compact | Parallel, 1 thread | Parallel, 2 threads | Memory (full vs compact) |
|---|---|---|---|---|---|
| 256x174 | 0.158 ms | 0.146 ms | 0.185 ms | 0.189 ms (0.84x, slower) | 0.51 MB vs 0.05 MB |
| 512x347 | 0.981 ms | 0.774 ms | 0.972 ms | 0.734 ms (1.34x) | 2.03 MB vs 0.18 MB |
| 1024x694 | 3.88 ms | 3.34 ms | 4.64 ms | 2.88 ms (1.35x) | 8.13 MB vs 0.69 MB |
| 2048x1388 | 17.7 ms | 13.3 ms | 17.7 ms | 12.1 ms (1.47x) | 32.5 MB vs 2.74 MB |

| Carve 50 seams | Energy ms | Total ms |
|---|---|---|
| 512x347 full recompute | 59.8 | 112.3 |
| 512x347 incremental | 10.1 | 51.6 (2.2x faster) |
| 1024x694 full recompute | 184.6 | 338.9 |
| 1024x694 incremental | 40.5 | 207.0 (1.6x faster) |

## Graph Cut notes (Part 4)
- Graph as in the spec: INF source/sink edges, `e(i,j)` on (i,j)->(i,j+1), INF backward and diagonal edges.
- **One deviation:** the spec connects the last pixel column to T, which means pixels in the last column can never be removed. We add a dummy column after the last one and connect *that* to T. The set of possible seams is then identical to the DP's, so the costs are directly comparable.
- Capacities are 64-bit integers (Sobel energy of an 8-bit image is already an integer), so max-flow is exact.
- Max-flow uses Dinic's algorithm with an iterative DFS. The seam is read from the source side of the residual graph: the rightmost reachable pixel in each row.

## Sanity checks done
- DP vs brute-force enumeration of all seams, 2000 random small grids: always optimal.
- Graph Cut vs DP, 3000 random grids: seam cost **and** max-flow value always equal the DP optimum (max-flow min-cut theorem).
- Compact and parallel DP return the same seam as the full-table DP at every size tested.
- Incremental energy map equals a full recompute after each of 60 consecutive seams.
- Refactor to the OpenCV-free core: every output image (results, seam overlays, energy map) is bit-identical to the
  OpenCV-based version, for DP, Greedy and Graph Cut.
- WebAssembly vs native: 60 mixed steps (all three algorithms, both directions, undo) give identical seam paths and costs,
  and an identical final image.
- Viewer: 85 steps back after mixed vertical/horizontal, DP/Greedy/Graph Cut steps restores the original bit for bit.
- Sample single-seam timings at 590x400: DP ~2 ms, Graph Cut ~2100 ms.
- Sample batch run (590x400, remove 30 cols + 10 rows), total time spent finding seams: DP 41 ms, Greedy 1 ms, Graph Cut ~146 s. Average seam cost: DP 15187, Greedy 51028, Graph Cut 15179.
  DP and Graph Cut are both optimal at every step, but when two seams tie they can pick different ones, so later images (and costs) drift apart slightly.

