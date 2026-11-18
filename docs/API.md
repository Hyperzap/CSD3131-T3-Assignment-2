# Seam Carving WebAssembly API

The seam carving engine (DP, Greedy, Graph Cut) is the same C++ code as the desktop programs, compiled to
WebAssembly. It runs entirely in the browser: no server, and images never leave the user's machine.

- Files: `web/dist/seam.js` + `web/dist/seam.wasm` (ES module), types in `web/seam.d.ts`
- Working reference app: `web/src/` + `web/worker.js`
- Version: `M.version()` → `"1.0.0"`

## Getting the files

- **Already built:** `web/dist/` is committed, and every push also builds it in CI (Actions → `build` → artifact `seam-wasm`).
- **Build it yourself** (needs [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)):
  ```
  emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
  cmake --build build-web
  ```
  This writes `web/dist/seam.js`, `seam.wasm` and `seam.d.ts`.
- **Run the demo:** `npm install`, then `npm run dev`, and open the URL it prints.

## Quick start

```js
import createSeamModule from './dist/seam.js';

const M = await createSeamModule();
const { data, width, height } = ctx.getImageData(0, 0, w, h);   // RGBA bytes from a canvas

const c = new M.Carver(width, height, data);

c.stepForward('dp', false);        // phase 1: seam #1 is found  -> c.pending()
const seam = c.pending();          // { path: Int32Array, cost, findMs, horizontal, algo, index }
c.stepForward('dp', false);        // phase 2: seam #1 is removed -> image is 1 px narrower
ctx.putImageData(new ImageData(c.image(), c.width(), c.height()), 0, 0);

c.delete();                        // REQUIRED: embind objects are not garbage-collected
```

## The step model

Each seam takes two steps, like the desktop viewer:

1. **find**: the next seam is computed and becomes *pending*. Draw it highlighted.
2. **remove**: the pending seam is removed. The image shrinks by one column (vertical) or one row (horizontal).

`stepBack()` undoes one phase: it un-highlights a pending seam, or re-inserts the last removed seam, which then becomes
pending again. Undo restores the original pixels exactly. Computed seams are cached, so stepping back and forward again
is instant. Asking for a different algorithm or direction drops the cached future automatically.

### Seam coordinates (important)

`path` is relative to the image **at that step**, after all earlier removals. That's the image you get from
`image()` while the seam is pending.

| Seam | `path.length` | Meaning |
|---|---|---|
| vertical (`horizontal: false`) | height | `path[y]` = x of the pixel removed in row y |
| horizontal (`horizontal: true`) | width | `path[x]` = y of the pixel removed in column x |

Drawing a pending seam in red on the current image:

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
| `new M.Carver(w, h, rgba)` | Carver | `rgba` = `Uint8Array` / `Uint8ClampedArray`, length `w*h*4`, else throws |
| `width()`, `height()` | number | current size |
| `originalWidth()`, `originalHeight()` | number | size at load |
| `findSeam(algo, horizontal)` | boolean | phase 1; false if the image is too small (≤ 2 px) |
| `removeSeam()` | boolean | phase 2; false if nothing is pending |
| `stepForward(algo, horizontal)` | boolean | remove if a matching seam is pending, else find |
| `stepBack()` | boolean | false at the original image |
| `carve(algo, cols, rows)` | number | removes `cols` vertical then `rows` horizontal seams; returns how many |
| `discardFuture()` | void | drop the pending seam and the redo cache |
| `reset()` | void | back to the original image |
| `hasPending()` | boolean | |
| `pending()` | SeamStep or null | the highlighted seam |
| `stepsRemoved()` | number | |
| `step(k)`, `steps()` | SeamStep / SeamStep[] | removed seams, oldest first (e.g. to draw all of them, or export) |
| `image()` | Uint8ClampedArray | current RGBA |
| `energyImage()` | Uint8ClampedArray | energy map as grayscale RGBA, for an "energy view" |
| `energy()` | Float64Array | raw energy values (`|dx| + |dy|` Sobel, summed over R, G, B) |
| `lastError()` | string | why the last call returned false |
| `delete()` | void | free WASM memory |

`algo` is `'dp'`, `'greedy'` or `'graphcut'`. An unknown name throws an `Error`.

Every array returned is a **copy**, so it's safe to keep, modify or transfer to another thread.

## Use a Web Worker

Graph Cut takes **seconds per seam** (it solves a max-flow problem each time; about 1000× slower than DP). On the
main thread that freezes the page. `web/worker.js` wraps the Carver in a worker with a small message protocol, and
`web/src/useCarver.js` shows how to use it:

```js
const worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
worker.postMessage({ id: 1, cmd: 'load', width, height, rgba }, [rgba.buffer]);
worker.postMessage({ id: 2, cmd: 'forward', algo: 'dp', horizontal: false });
worker.onmessage = (e) => {
  const { id, ok, error, state } = e.data;
  // state = { width, height, originalWidth, originalHeight, stepsRemoved, pending, rgba, energy, seams }
};
```

Commands: `load`, `forward`, `back`, `reset`, `discard`, `carve`, `state`. Add `withEnergy: true` to any command to also get
the energy image, or `withSeams: true` to get the original image with every seam drawn in red. See the comment at the top of `worker.js`.

## Limits and performance

| | Typical time per seam, 600×400, in the browser |
|---|---|
| DP | ~5–15 ms |
| Greedy | < 1 ms |
| Graph Cut | several seconds |

- Downscale large images before loading. The demo caps the longest side at 400 / 600 / 800 px.
- Graph Cut memory grows with the image (about 240k nodes and 2M edges at 600×400). The module grows its memory as needed.
- The WASM build is single-threaded. OpenMP threads need cross-origin isolation headers that static hosts like GitHub
  Pages can't send, so the parallel DP is a native-only experiment.

## Versioning

The JS API follows semver. Any change to method names, return shapes or the `path` convention bumps the major
version and is discussed in an issue first.
