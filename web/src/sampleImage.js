// Draws a demo picture so the visualiser has something to carve before an upload.
// The flat sky and water are low energy; the lighthouse and ridges are high energy,
// so seams visibly route around them.
export function createSampleImage(width = 800, height = 533) {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d');
  const horizon = height * 0.68;

  const sky = ctx.createLinearGradient(0, 0, 0, horizon);
  sky.addColorStop(0, '#27446b');
  sky.addColorStop(0.65, '#c9786a');
  sky.addColorStop(1, '#f3c07c');
  ctx.fillStyle = sky;
  ctx.fillRect(0, 0, width, horizon);

  ctx.fillStyle = '#ffe6ad';
  ctx.beginPath();
  ctx.arc(width * 0.27, horizon * 0.72, height * 0.075, 0, Math.PI * 2);
  ctx.fill();

  drawRidge(ctx, width, horizon, height * 0.17, 0.011, '#57506f');
  drawRidge(ctx, width, horizon, height * 0.1, 0.023, '#343a56');

  const sea = ctx.createLinearGradient(0, horizon, 0, height);
  sea.addColorStop(0, '#46708a');
  sea.addColorStop(1, '#14293f');
  ctx.fillStyle = sea;
  ctx.fillRect(0, horizon, width, height - horizon);

  // The sun's reflection, as short bright streaks.
  ctx.fillStyle = '#f7d79b';
  for (let i = 0; i < 16; i++) {
    const y = horizon + 7 + i * 10;
    const half = 34 - i * 1.4 + 12 * Math.sin(i * 1.7);
    ctx.fillRect(width * 0.27 - half, y, half * 2, 2);
  }

  drawLighthouse(ctx, width * 0.72, horizon + height * 0.08, height * 0.54);
  return canvas;
}

function drawRidge(ctx, width, base, amplitude, frequency, color) {
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.moveTo(0, base);
  for (let x = 0; x <= width; x += 4) {
    const wave = Math.sin(x * frequency) + 0.5 * Math.sin(x * frequency * 2.7 + 1) + 0.25 * Math.sin(x * frequency * 6.1);
    ctx.lineTo(x, base - amplitude * (0.55 + 0.32 * wave));
  }
  ctx.lineTo(width, base);
  ctx.closePath();
  ctx.fill();
}

function drawLighthouse(ctx, x, baseY, towerHeight) {
  const unit = towerHeight / 10;

  // Rocky island.
  ctx.fillStyle = '#20222d';
  ctx.beginPath();
  ctx.moveTo(x - unit * 4.2, baseY + unit * 0.9);
  ctx.lineTo(x - unit * 2.4, baseY - unit * 0.1);
  ctx.lineTo(x - unit * 0.6, baseY - unit * 0.45);
  ctx.lineTo(x + unit * 1.6, baseY - unit * 0.2);
  ctx.lineTo(x + unit * 3.6, baseY + unit * 0.9);
  ctx.closePath();
  ctx.fill();

  // Tapered tower with red and white bands.
  const top = baseY - towerHeight;
  const halfWidthAt = (y) => unit * (0.62 + (0.42 * (y - top)) / towerHeight);
  const bands = 6;
  const bandHeight = (towerHeight - unit * 0.3) / bands;
  for (let i = 0; i < bands; i++) {
    const y0 = top + i * bandHeight;
    const y1 = y0 + bandHeight;
    ctx.fillStyle = i % 2 ? '#f3efe6' : '#b73a35';
    ctx.beginPath();
    ctx.moveTo(x - halfWidthAt(y0), y0);
    ctx.lineTo(x + halfWidthAt(y0), y0);
    ctx.lineTo(x + halfWidthAt(y1), y1);
    ctx.lineTo(x - halfWidthAt(y1), y1);
    ctx.closePath();
    ctx.fill();
  }

  // Lantern room and roof.
  ctx.fillStyle = '#20222d';
  ctx.fillRect(x - unit * 0.85, top - unit * 0.12, unit * 1.7, unit * 0.16);
  ctx.fillStyle = '#ffe9a8';
  ctx.fillRect(x - unit * 0.48, top - unit * 0.72, unit * 0.96, unit * 0.6);
  ctx.fillStyle = '#20222d';
  ctx.beginPath();
  ctx.moveTo(x - unit * 0.62, top - unit * 0.72);
  ctx.lineTo(x, top - unit * 1.2);
  ctx.lineTo(x + unit * 0.62, top - unit * 0.72);
  ctx.closePath();
  ctx.fill();
}
