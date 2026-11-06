// TypeScript declarations for seam.js / seam.wasm (Emscripten embind build).
// See docs/API.md for the full guide.

export type Algo = 'dp' | 'greedy' | 'graphcut';

/** One seam: found (pending) or already removed. */
export interface SeamStep {
  /** 0-based position in the removal order. */
  index: number;
  /** false: vertical seam (path[row] = column). true: horizontal seam (path[column] = row). */
  horizontal: boolean;
  algo: Algo;
  /** Sum of the energies along the seam. */
  cost: number;
  /** Time spent in the seam finder, in milliseconds. */
  findMs: number;
  /**
   * Seam coordinates in the image AS IT WAS AT THIS STEP (after all earlier removals).
   * Vertical: length = height, path[y] = x.  Horizontal: length = width, path[x] = y.
   */
  path: Int32Array;
}

/** Stateful step-by-step carver. Must be freed with delete(). */
export interface Carver {
  /** Current size (shrinks as seams are removed). */
  width(): number;
  height(): number;
  originalWidth(): number;
  originalHeight(): number;

  /** Phase 1: compute the next seam and make it pending. False if the image is too small. */
  findSeam(algo: Algo, horizontal: boolean): boolean;
  /** Phase 2: remove the pending seam. False if none is pending. */
  removeSeam(): boolean;
  /** removeSeam() if a seam with these settings is pending, otherwise findSeam(). */
  stepForward(algo: Algo, horizontal: boolean): boolean;
  /** Undo one phase (un-highlight, or re-insert the last removed seam). False at the original. */
  stepBack(): boolean;
  /** Drop the pending seam and cached redo steps. */
  discardFuture(): void;
  /** Back to the original image, clearing all history. */
  reset(): void;
  /** Remove `cols` vertical then `rows` horizontal seams. Returns how many were removed. */
  carve(algo: Algo, cols: number, rows: number): number;

  hasPending(): boolean;
  /** The highlighted (found, not yet removed) seam, or null. */
  pending(): SeamStep | null;
  /** Number of seams removed so far. */
  stepsRemoved(): number;
  /** Removed seam k (0 = first), or null. */
  step(k: number): SeamStep | null;
  /** All removed seams, oldest first. */
  steps(): SeamStep[];

  /** Current image, RGBA, width() * height() * 4 bytes (a copy). */
  image(): Uint8ClampedArray;
  /** Energy map of the current image as a grayscale RGBA image (a copy). */
  energyImage(): Uint8ClampedArray;
  /** Raw energy values, width() * height() doubles, row-major (a copy). */
  energy(): Float64Array;

  /** Why the last call returned false ('' if it didn't). */
  lastError(): string;
  /** Frees the WASM memory. Required: embind objects are not garbage-collected. */
  delete(): void;
}

export interface SeamModule {
  /** rgba: e.g. ImageData.data. Throws if rgba.length !== width * height * 4. */
  Carver: new (width: number, height: number, rgba: Uint8Array | Uint8ClampedArray) => Carver;
  version(): string;
}

declare function createSeamModule(options?: Record<string, unknown>): Promise<SeamModule>;
export default createSeamModule;
