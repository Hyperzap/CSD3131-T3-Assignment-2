// Web Worker that owns the WASM seam carver, so slow work (graph cut takes
// seconds per seam) never blocks the page.
//
// Protocol: the page posts { id, cmd, ...args } and gets back
//   { id, ok, error?, state }
// where state = {
//   width, height, originalWidth, originalHeight, stepsRemoved,
//   pending: { index, horizontal, algo, cost, findMs, path: Int32Array } | null,
//   rgba: Uint8ClampedArray (current image, transferred),
//   energy: Uint8ClampedArray | null (grayscale RGBA, only when asked for)
// }
// Commands:
//   load    { width, height, rgba }      start over with a new image
//   forward { algo, horizontal }         find the next seam, or remove the pending one
//   back    {}                           undo one step
//   reset   {}                           back to the original image
//   discard {}                           drop the pending seam + cached redo steps
//   carve   { algo, cols, rows }         remove many seams at once
//   state   {}                           just report the state
// Every command accepts `withEnergy: true` to also return the energy map.

import createSeamModule from './dist/seam.js';

const ready = createSeamModule();
let M = null;
let carver = null;

function snapshot(withEnergy) {
  const rgba = carver.image();
  const energy = withEnergy ? carver.energyImage() : null;
  const state = {
    width: carver.width(),
    height: carver.height(),
    originalWidth: carver.originalWidth(),
    originalHeight: carver.originalHeight(),
    stepsRemoved: carver.stepsRemoved(),
    pending: carver.pending(),
    rgba,
    energy,
  };
  const transfer = [rgba.buffer];
  if (energy) transfer.push(energy.buffer);
  return { state, transfer };
}

self.onmessage = async (e) => {
  const { id, cmd, withEnergy = false, ...args } = e.data;
  try {
    M = M || (await ready);
    let ok = true;
    switch (cmd) {
      case 'load':
        if (carver) carver.delete(); // embind objects must be freed by hand
        carver = new M.Carver(args.width, args.height, args.rgba);
        break;
      case 'forward': ok = carver.stepForward(args.algo, args.horizontal); break;
      case 'back': ok = carver.stepBack(); break;
      case 'reset': carver.reset(); break;
      case 'discard': carver.discardFuture(); break;
      case 'carve': ok = carver.carve(args.algo, args.cols || 0, args.rows || 0) > 0; break;
      case 'state': break;
      default: throw new Error(`Unknown command: ${cmd}`);
    }
    const { state, transfer } = snapshot(withEnergy);
    self.postMessage({ id, ok, error: ok ? null : carver.lastError(), state }, transfer);
  } catch (err) {
    self.postMessage({ id, ok: false, error: String(err && err.message ? err.message : err), state: null });
  }
};
