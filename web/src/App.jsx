import { useCallback, useEffect, useRef, useState } from 'react';
import { createSampleImage } from './sampleImage.js';
import { useCarver } from './useCarver.js';

const ALGOS = [
  { id: 'dp', label: 'Dynamic programming' },
  { id: 'greedy', label: 'Greedy' },
  { id: 'graphcut', label: 'Graph Cut (slow)' },
];
// New images are scaled to fit this longest side; the size can then be edited.
const DEFAULT_MAX_SIDE = 400;
const MAX_DIM = 2000;
const VIEWS = [
  { id: 'image', label: 'Image' },
  { id: 'energy', label: 'Energy map' },
  { id: 'seams', label: 'All seams on original' },
];
const MAX_DELAY = 500;

const clampDim = (n) => Math.min(MAX_DIM, Math.max(1, Math.round(n)));

export default function App() {
  const [state, setState] = useState(null);
  const [error, setError] = useState(null);
  const [algo, setAlgo] = useState('dp');
  const [horizontal, setHorizontal] = useState(false);
  const [view, setView] = useState('image');
  const [sizeW, setSizeW] = useState(0);
  const [sizeH, setSizeH] = useState(0);
  const [keepAspect, setKeepAspect] = useState(true);
  const [playing, setPlaying] = useState(false);
  const [delay, setDelay] = useState(100);
  const [cols, setCols] = useState(50);
  const [rows, setRows] = useState(0);
  const [dragging, setDragging] = useState(false);

  const canvasRef = useRef(null);
  const fileInputRef = useRef(null);
  const sourceRef = useRef(null);

  // Current settings for the play loop and async handlers, without restarting them.
  const live = useRef({});
  live.current = { algo, horizontal, view, delay };

  const { send, busy } = useCarver(() => loadSource(createSampleImage()));

  // Runs one worker command and shows the result. Resolves to true if it succeeded.
  const run = useCallback(
    async (cmd, args = {}, transfer = []) => {
      const reply = await send(cmd, { ...args, withEnergy: live.current.view === 'energy', withSeams: live.current.view === 'seams' }, transfer);
      if (reply.state) setState(reply.state);
      setError(reply.ok ? null : reply.error);
      return reply.ok;
    },
    [send],
  );

  // Resizes a bitmap or canvas to `size` (default: fit DEFAULT_MAX_SIDE) and hands its pixels to the worker.
  function loadSource(source, size) {
    sourceRef.current = source;
    let width;
    let height;
    if (size) {
      width = clampDim(size.width);
      height = clampDim(size.height);
    } else {
      const scale = Math.min(1, DEFAULT_MAX_SIDE / Math.max(source.width, source.height));
      width = clampDim(source.width * scale);
      height = clampDim(source.height * scale);
    }
    setSizeW(width);
    setSizeH(height);
    const scratch = document.createElement('canvas');
    scratch.width = width;
    scratch.height = height;
    const ctx = scratch.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(source, 0, 0, width, height);
    const rgba = ctx.getImageData(0, 0, width, height).data;
    setPlaying(false);
    return run('load', { width, height, rgba }, [rgba.buffer]);
  }

  async function openFile(file) {
    if (!file) return;
    try {
      await loadSource(await createImageBitmap(file));
    } catch {
      setError(`Could not read "${file.name}" as an image.`);
    }
  }

  const stepForward = useCallback(
    () => run('forward', { algo: live.current.algo, horizontal: live.current.horizontal }),
    [run],
  );

  const controlsLocked = busy || playing || !state;

  // Autoplay: wait for each step to finish, then pause before starting the next one.
  useEffect(() => {
    if (!playing) return undefined;
    let cancelled = false;
    let timer;
    const tick = async () => {
      const ok = await stepForward();
      if (cancelled) return;
      if (ok) timer = setTimeout(tick, live.current.delay);
      else setPlaying(false);
    };
    tick();
    return () => {
      cancelled = true;
      clearTimeout(timer);
    };
  }, [playing, stepForward]);

  // A seam found with the old settings is no longer the next step, so drop it.
  function changeAlgo(next) {
    setAlgo(next);
    run('discard');
  }

  function changeDirection(next) {
    setHorizontal(next);
    run('discard');
  }

  function changeView(next) {
    live.current.view = next;
    setView(next);
    run('state');
  }

  // Editing one side updates the other when proportions are kept.
  function changeWidth(value) {
    setSizeW(value);
    const src = sourceRef.current;
    if (keepAspect && src && value > 0) setSizeH(clampDim((value * src.height) / src.width));
  }

  function changeHeight(value) {
    setSizeH(value);
    const src = sourceRef.current;
    if (keepAspect && src && value > 0) setSizeW(clampDim((value * src.width) / src.height));
  }

  function applySize() {
    if (!(sizeW > 0 && sizeH > 0)) {
      setError('Width and height must be at least 1 pixel.');
      return;
    }
    loadSource(sourceRef.current, { width: sizeW, height: sizeH });
  }

  // The image's own size, scaled down proportionally if it is over MAX_DIM.
  function originalSize() {
    const src = sourceRef.current;
    const scale = Math.min(1, MAX_DIM / Math.max(src.width, src.height));
    loadSource(src, { width: src.width * scale, height: src.height * scale });
  }

  function reset() {
    setPlaying(false);
    run('reset');
  }

  // Keyboard shortcuts (listed in the footer).
  useEffect(() => {
    const onKey = (e) => {
      if (e.ctrlKey || e.metaKey || e.altKey) return;
      if (e.target.closest('input, select')) return;
      const key = e.key.toLowerCase();
      if (key === ' ' && e.target.closest('button')) return;

      const actions = {
        arrowright: () => !controlsLocked && stepForward(),
        arrowleft: () => !controlsLocked && run('back'),
        ' ': () => state && setPlaying((p) => !p),
        a: () => !controlsLocked && changeAlgo(ALGOS[(ALGOS.findIndex((x) => x.id === algo) + 1) % ALGOS.length].id),
        d: () => !controlsLocked && changeDirection(!horizontal),
        e: () => state && changeView(VIEWS[(VIEWS.findIndex((x) => x.id === view) + 1) % VIEWS.length].id),
        r: () => state && !busy && reset(),
        '+': () => setDelay((d) => Math.max(0, d - 50)),
        '=': () => setDelay((d) => Math.max(0, d - 50)),
        '-': () => setDelay((d) => Math.min(MAX_DELAY, d + 50)),
        o: () => !controlsLocked && fileInputRef.current.click(),
      };
      if (actions[key]) {
        e.preventDefault();
        actions[key]();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  });

  // Draw the chosen view. The image and energy map show any pending seam in red; the
  // seam overlay already includes it and stays at the original size.
  useEffect(() => {
    if (!state) return;
    const canvas = canvasRef.current;
    if (view === 'seams' && state.seams) {
      canvas.width = state.originalWidth;
      canvas.height = state.originalHeight;
      canvas.getContext('2d').putImageData(new ImageData(state.seams, canvas.width, canvas.height), 0, 0);
      return;
    }
    const source = view === 'energy' && state.energy ? state.energy : state.rgba;
    const pixels = new Uint8ClampedArray(source);
    const seam = state.pending;
    if (seam) {
      seam.path.forEach((value, i) => {
        const [x, y] = seam.horizontal ? [i, value] : [value, i];
        const offset = (y * state.width + x) * 4;
        pixels[offset] = 255;
        pixels[offset + 1] = 30;
        pixels[offset + 2] = 40;
        pixels[offset + 3] = 255;
      });
    }
    canvas.width = state.width;
    canvas.height = state.height;
    canvas.getContext('2d').putImageData(new ImageData(pixels, state.width, state.height), 0, 0);
  }, [state, view]);

  const seam = state?.pending ?? null;
  const original = state ? `${state.originalWidth} × ${state.originalHeight}` : '–';
  const current = state ? `${state.width} × ${state.height}` : '–';

  return (
    <main className="app">
      <header className="masthead">
        <div>
          <p className="eyebrow">CSD3131 Assignment 2</p>
          <h1>Seam Carving Visualiser</h1>
        </div>
        <p className="lede">
          Watch an image shrink one seam at a time. It runs entirely in your browser, using the C++ engine compiled to
          WebAssembly, and your images never leave your machine.
        </p>
      </header>

      <div className="workspace">
        <section
          className={`stage${dragging ? ' stage-drop' : ''}`}
          aria-label="Image being carved"
          onDragOver={(e) => {
            e.preventDefault();
            setDragging(true);
          }}
          onDragLeave={() => setDragging(false)}
          onDrop={(e) => {
            e.preventDefault();
            setDragging(false);
            if (!controlsLocked) openFile(e.dataTransfer.files[0]);
          }}
        >
          <div
            className="frame"
            style={
              state
                ? {
                    aspectRatio: `${state.originalWidth} / ${state.originalHeight}`,
                    // Fit tall images on screen without changing their proportions.
                    width: `min(100%, calc(72vh * ${state.originalWidth / state.originalHeight}))`,
                  }
                : undefined
            }
          >
            <canvas
              ref={canvasRef}
              role="img"
              aria-label={`${VIEWS.find((v) => v.id === view).label}, image currently ${current} pixels`}
              style={
                state && view !== 'seams'
                  ? {
                      width: `${(state.width / state.originalWidth) * 100}%`,
                      height: `${(state.height / state.originalHeight) * 100}%`,
                    }
                  : undefined
              }
            />
          </div>

          <dl className="readout" aria-live="polite">
            <div>
              <dt>Original</dt>
              <dd>{original}</dd>
            </div>
            <div>
              <dt>Current</dt>
              <dd>{current}</dd>
            </div>
            <div>
              <dt>Seams removed</dt>
              <dd>{state?.stepsRemoved ?? 0}</dd>
            </div>
            <div>
              <dt>Seam cost</dt>
              <dd>{seam ? Math.round(seam.cost).toLocaleString() : '–'}</dd>
            </div>
            <div>
              <dt>Find time</dt>
              <dd>{seam ? `${seam.findMs.toFixed(seam.findMs < 10 ? 2 : 0)} ms` : '–'}</dd>
            </div>
          </dl>

          {error && (
            <p className="error" role="alert">
              {error}
            </p>
          )}
        </section>

        <aside className="panel" aria-label="Controls">
          <fieldset>
            <legend>Step through</legend>
            <div className="row row-step">
              <button type="button" onClick={() => run('back')} disabled={controlsLocked}>
                ← Back
              </button>
              <button type="button" className="primary" onClick={stepForward} disabled={controlsLocked}>
                {seam ? 'Remove seam →' : 'Find seam →'}
              </button>
            </div>
            <div className="row">
              <button type="button" onClick={() => setPlaying((p) => !p)} disabled={!state}>
                {playing ? 'Pause' : 'Play'}
              </button>
              <button type="button" onClick={reset} disabled={!state || (busy && !playing)}>
                Reset
              </button>
            </div>
            <label>
              <span className="label-row">
                Delay between steps <output>{delay} ms</output>
              </span>
              <input
                type="range"
                min="0"
                max={MAX_DELAY}
                step="10"
                value={delay}
                onChange={(e) => setDelay(Number(e.target.value))}
              />
            </label>
          </fieldset>

          <fieldset>
            <legend>Settings</legend>
            <label>
              Algorithm
              <select value={algo} onChange={(e) => changeAlgo(e.target.value)} disabled={controlsLocked}>
                {ALGOS.map((a) => (
                  <option key={a.id} value={a.id}>
                    {a.label}
                  </option>
                ))}
              </select>
            </label>
            <label>
              Seam direction
              <select
                value={horizontal ? 'horizontal' : 'vertical'}
                onChange={(e) => changeDirection(e.target.value === 'horizontal')}
                disabled={controlsLocked}
              >
                <option value="vertical">Vertical (narrower)</option>
                <option value="horizontal">Horizontal (shorter)</option>
              </select>
            </label>
            <label>
              View
              <select value={view} onChange={(e) => changeView(e.target.value)} disabled={!state}>
                {VIEWS.map((v) => (
                  <option key={v.id} value={v.id}>
                    {v.label}
                  </option>
                ))}
              </select>
            </label>
            {algo === 'graphcut' && <p className="note">Graph Cut can take seconds per seam. Keep the image small.</p>}
          </fieldset>

          <fieldset>
            <legend>Remove many at once</legend>
            <div className="row">
              <label>
                Columns
                <input type="number" min="0" value={cols} onChange={(e) => setCols(Number(e.target.value))} />
              </label>
              <label>
                Rows
                <input type="number" min="0" value={rows} onChange={(e) => setRows(Number(e.target.value))} />
              </label>
            </div>
            <button type="button" onClick={() => run('carve', { algo, cols, rows })} disabled={controlsLocked}>
              Carve
            </button>
          </fieldset>

          <fieldset>
            <legend>Image</legend>
            <input
              ref={fileInputRef}
              className="visually-hidden"
              type="file"
              accept="image/*"
              tabIndex={-1}
              aria-label="Choose an image"
              onChange={(e) => {
                openFile(e.target.files[0]);
                e.target.value = '';
              }}
            />
            <button type="button" onClick={() => fileInputRef.current.click()} disabled={controlsLocked}>
              Open image…
            </button>
            <button type="button" onClick={() => loadSource(createSampleImage())} disabled={controlsLocked}>
              Use sample
            </button>
            <div className="row">
              <label>
                Width (px)
                <input
                  type="number"
                  min="1"
                  max={MAX_DIM}
                  value={sizeW || ''}
                  onChange={(e) => changeWidth(Number(e.target.value))}
                  onKeyDown={(e) => e.key === 'Enter' && !controlsLocked && applySize()}
                  disabled={controlsLocked}
                />
              </label>
              <label>
                Height (px)
                <input
                  type="number"
                  min="1"
                  max={MAX_DIM}
                  value={sizeH || ''}
                  onChange={(e) => changeHeight(Number(e.target.value))}
                  onKeyDown={(e) => e.key === 'Enter' && !controlsLocked && applySize()}
                  disabled={controlsLocked}
                />
              </label>
            </div>
            <label className="check">
              <input type="checkbox" checked={keepAspect} onChange={(e) => setKeepAspect(e.target.checked)} />
              Keep proportions
            </label>
            <div className="row">
              <button type="button" onClick={applySize} disabled={controlsLocked}>
                Apply size
              </button>
              <button type="button" onClick={originalSize} disabled={controlsLocked}>
                Original size
              </button>
            </div>
            <p className="note">
              Resizing reloads the image and starts carving over. Up to {MAX_DIM} px per side; large images are slow.
            </p>
            <p className="note">You can also drop an image onto the picture.</p>
          </fieldset>

          <p className="status" role="status">
            {busy ? 'Working…' : playing ? 'Playing' : 'Ready'}
          </p>
        </aside>
      </div>

      <footer className="keys">
        <span><kbd>→</kbd> <kbd>←</kbd> step</span>
        <span><kbd>Space</kbd> play</span>
        <span><kbd>A</kbd> algorithm</span>
        <span><kbd>D</kbd> direction</span>
        <span><kbd>E</kbd> view</span>
        <span><kbd>R</kbd> reset</span>
        <span><kbd>+</kbd> <kbd>-</kbd> speed</span>
        <span><kbd>O</kbd> open</span>
      </footer>
    </main>
  );
}
