# Seam Carving (CSD3131 Assignment 2)

A seam carving engine (content-aware image resizing) compiled to WebAssembly. It offers three seam finders
(**DP**, **Greedy**, **Graph Cut**), step-by-step find/remove with undo, and the energy map.
It runs entirely in the browser: no server needed, and images never leave the user's machine.

Version **1.0.0**.

## What's in here

| Path | What |
|---|---|
| `web/dist/seam.js` + `seam.wasm` | The built engine (ES module, default export `createSeamModule`). Ready to use, no build needed |
| `web/dist/seam.d.ts` | TypeScript types for everything below |
| `web/worker.js` | Optional: runs the engine in a Web Worker (recommended, see below) |
| `src/` | The C++ engine: energy map, DP / Greedy / Graph Cut seam finders, step engine (`carver.h`) |
| `web/bindings.cpp` | Exposes the C++ `Carver` to JavaScript (Emscripten embind) |
| `CMakeLists.txt` | Builds `web/dist/` from the C++ source (see "Building from source") |
| `ci/build.yml` or `.github/workflows/build.yml` | GitHub Actions: rebuilds the module on every push |
| `package.json` | So you can `npm install` this repo and import it by name |

## Setup

**Option A: static files (simplest).** Copy `web/dist/` and `web/worker.js` into your app's static/public directory,
keeping `worker.js` next to the `dist` folder:
```
public/seam/worker.js
public/seam/dist/seam.js, seam.wasm, seam.d.ts
```
```js
import createSeamModule from '/seam/dist/seam.js';
```

**Option B: as a package** (Vite, webpack, ...):
```
npm install ../path/to/this-repo        (or: npm install github:<user>/<repo>)
```
```ts
import createSeamModule from 'seam-api';   // types come from web/dist/seam.d.ts
```
`seam.js` finds `seam.wasm` with `new URL('seam.wasm', import.meta.url)`, which modern bundlers handle automatically.
For the worker with a bundler, copy `web/worker.js` into your source tree and change its first import to `'seam-api'`.

The page must be served over HTTP (e.g. `npx vite`, or `python -m http.server`). Browsers won't load WASM from `file://`.

## Quick start

```js
import createSeamModule from '/seam/dist/seam.js';

const M = await createSeamModule();
const { data, width, height } = ctx.getImageData(0, 0, w, h);   // RGBA bytes from a canvas

const c = new M.Carver(width, height, data);

c.stepForward('dp', false);        // step 1: seam #1 is found  -> c.pending()
const seam = c.pending();          // { path: Int32Array, cost, findMs, horizontal, algo, index }
c.stepForward('dp', false);        // step 2: seam #1 is removed -> image is 1 px narrower
ctx.putImageData(new ImageData(c.image(), c.width(), c.height()), 0, 0);

c.delete();                        // REQUIRED when done: these objects are not garbage-collected
```

## How stepping works

Each seam takes two steps:

1. **find**: the next seam is computed and becomes *pending*. Draw it highlighted, e.g. in red.
2. **remove**: the pending seam is removed. The image shrinks by one column (vertical seam) or one row (horizontal seam).

`stepBack()` undoes one step: it un-highlights a pending seam, or puts the last removed seam back (restoring the
original pixels exactly), which then becomes pending again. Computed seams are cached, so stepping back and forward again
is instant. Asking for a different algorithm or direction drops the cached steps automatically.

### Seam coordinates (important)

`path` is relative to the image **at that step**, after all earlier removals. That's the image `image()` returns
while the seam is pending.

| Seam | `path.length` | Meaning |
|---|---|---|
| vertical (`horizontal: false`) | height | `path[y]` = x of the pixel removed in row y |
| horizontal (`horizontal: true`) | width | `path[x]` = y of the pixel removed in column x |

Drawing the pending seam in red:

```js
const px = c.image();   // a copy, safe to modify
const p = c.pending();
p.path.forEach((v, i) => {
  const [x, y] = p.horizontal ? [i, v] : [v, i];
  const o = (y * c.width() + x) * 4;
  px[o] = 255; px[o + 1] = 0; px[o + 2] = 0; px[o + 3] = 255;
});
ctx.putImageData(new ImageData(px, c.width(), c.height()), 0, 0);
```

## Carver reference

| Method | Returns | Notes |
|---|---|---|
| `new M.Carver(w, h, rgba)` | Carver | `rgba` = `Uint8Array` / `Uint8ClampedArray` of length `w*h*4` (e.g. `ImageData.data`), else throws |
| `width()`, `height()` | number | current size |
| `originalWidth()`, `originalHeight()` | number | size at load |
| `findSeam(algo, horizontal)` | boolean | step 1; false if the image is too small (≤ 2 px) |
| `removeSeam()` | boolean | step 2; false if nothing is pending |
| `stepForward(algo, horizontal)` | boolean | removes a matching pending seam, otherwise finds the next one |
| `stepBack()` | boolean | false at the original image |
| `carve(algo, cols, rows)` | number | removes `cols` vertical then `rows` horizontal seams at once; returns how many |
| `discardFuture()` | void | drops the pending seam and the redo cache |
| `reset()` | void | back to the original image |
| `hasPending()` | boolean | |
| `pending()` | SeamStep or null | the highlighted seam |
| `stepsRemoved()` | number | |
| `step(k)`, `steps()` | SeamStep / SeamStep[] | removed seams, oldest first |
| `image()` | Uint8ClampedArray | current image, RGBA |
| `energyImage()` | Uint8ClampedArray | energy map as a grayscale RGBA image, for an "energy view" |
| `energy()` | Float64Array | raw energy values, row-major |
| `lastError()` | string | why the last call returned false |
| `delete()` | void | frees the engine's memory |

`M.version()` returns `"1.0.0"`.

- **Algorithms:** `algo` is `'dp'`, `'greedy'` or `'graphcut'`. An unknown name throws an `Error`.
- **Returned arrays are copies,** so they're safe to keep, modify or transfer to a worker.
- **DP and Graph Cut find the optimal (cheapest) seam;** Greedy is fast but not optimal.

## Use a Web Worker (recommended)

Graph Cut takes **seconds per seam**. On the main thread that freezes the page, so `worker.js` runs the engine in a
worker instead:

```js
const worker = new Worker(new URL('/seam/worker.js', import.meta.url), { type: 'module' });

worker.postMessage({ id: 1, cmd: 'load', width, height, rgba }, [rgba.buffer]);
worker.postMessage({ id: 2, cmd: 'forward', algo: 'dp', horizontal: false });

worker.onmessage = (e) => {
  const { id, ok, error, state } = e.data;
  // state = { width, height, originalWidth, originalHeight, stepsRemoved, pending, rgba, energy }
};
```

| Command | Extra fields | Does |
|---|---|---|
| `load` | `width, height, rgba` | starts over with a new image |
| `forward` | `algo, horizontal` | finds the next seam, or removes the pending one |
| `back` | none | undoes one step |
| `reset` | none | back to the original image |
| `discard` | none | drops the pending seam and the redo cache |
| `carve` | `algo, cols, rows` | removes many seams at once |
| `state` | none | just reports the current state |

Add `withEnergy: true` to any command to also get `state.energy` (grayscale RGBA).

Each reply carries the same `id` as its command. The worker handles one command at a time, in order.

## Performance and limits

Measured per seam on a 590×400 photo in Chromium:

| Algorithm | Time per seam |
|---|---|
| DP | ~2 ms |
| Greedy | ~0.04 ms |
| Graph Cut | several seconds (~10 s on a slow machine) |

- **Downscale large images before loading,** e.g. to 600–800 px on the longest side. Graph Cut gets much slower as the image grows.
- **Single-threaded:** the engine runs one computation at a time and doesn't use multiple cores.

## Run the whole app with Docker

To run the visualiser locally without installing Node or Emscripten, use Docker. It builds everything from source,
including the C++ engine:
```
docker compose up --build
```
Then open http://localhost:8080. Stop it with `docker compose down`.

## Building from source

You only need this to change the engine; `web/dist/` is already built.

1. Install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (emsdk).
2. Build:
   ```
   emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
   cmake --build build-web
   ```
3. That rewrites `web/dist/seam.js`, `seam.wasm` and `seam.d.ts`. Commit all three together.

CI does the same on GitHub: every push builds the module (Actions → `build` → artifact `seam-wasm`).

The C++ engine in brief:

| File | Contents |
|---|---|
| `src/image.h` | RGBA image type (same layout as canvas `ImageData`) |
| `src/energy.cpp` | Sobel energy map, plus the incremental update after a seam is removed |
| `src/seam_dp.cpp`, `seam_dp_variants.cpp` | Dynamic programming seam finder (full table and memory-compact versions) |
| `src/seam_greedy.cpp` | Greedy seam finder |
| `src/seam_graphcut.cpp` | Min-cut / max-flow seam finder (Dinic's algorithm) |
| `src/carve.cpp` | Removing and re-inserting seams |
| `src/carver.cpp` | `Carver`: the step engine (find/remove, undo/redo) that the JS API wraps |

## Versions

The API follows semantic versioning (`M.version()`, and `version` in `package.json`). A change to method names, return
shapes or the `path` convention bumps the major version.
