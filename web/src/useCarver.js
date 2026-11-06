import { useCallback, useEffect, useRef, useState } from 'react';

// Owns the worker from web/worker.js and turns its id-based messages into promises.
// send() resolves with the worker's reply: { ok, error, state }.
export function useCarver(onReady) {
  const workerRef = useRef(null);
  const repliesRef = useRef(new Map());
  const nextIdRef = useRef(1);
  const onReadyRef = useRef(onReady);
  const [inFlight, setInFlight] = useState(0);

  const send = useCallback((cmd, args = {}, transfer = []) => {
    const id = nextIdRef.current++;
    setInFlight((n) => n + 1);
    return new Promise((resolve) => {
      repliesRef.current.set(id, resolve);
      workerRef.current.postMessage({ id, cmd, ...args }, transfer);
    });
  }, []);

  useEffect(() => {
    const replies = repliesRef.current;
    const worker = new Worker(new URL('../worker.js', import.meta.url), { type: 'module' });
    workerRef.current = worker;

    worker.onmessage = (e) => {
      const resolve = replies.get(e.data.id);
      replies.delete(e.data.id);
      setInFlight((n) => n - 1);
      resolve?.(e.data);
    };
    // Raised if the worker script or the WASM module fails to load.
    worker.onerror = (e) => {
      const failure = { ok: false, error: e.message || 'The seam carving worker failed to load.', state: null };
      replies.forEach((resolve) => resolve(failure));
      setInFlight((n) => n - replies.size);
      replies.clear();
    };

    onReadyRef.current();

    return () => {
      worker.terminate();
      setInFlight((n) => n - replies.size);
      replies.clear();
    };
  }, []);

  return { send, busy: inFlight > 0 };
}
