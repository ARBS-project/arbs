// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS icon set generator (H031). Input: ../source/*.svg. Output: png/, ico/, web/, svg/, android/, check/, SHA256SUMS.
// Renders SVG with an installed Chrome / Edge through Playwright (no browser download), encodes PNG and ICO itself
// (Node zlib), so the same input gives the same output. Never calls ImageMagick or "convert" (on Windows "convert" is
// the FAT-to-NTFS disk tool).
//
// Usage (from any folder):
//   PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH="C:\Program Files\Google\Chrome\Application\chrome.exe" \
//   ARBS_PLAYWRIGHT_MODULE=<path to playwright/index.mjs> node tools/make-icons.mjs [--out <dir>]
import { createHash } from 'node:crypto';
import { mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { deflateSync } from 'node:zlib';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');
const args = Object.fromEntries(
  process.argv.slice(2).reduce((pairs, value, i, all) => (value.startsWith('--') ? [...pairs, [value.slice(2), all[i + 1]]] : pairs), []),
);
const out = resolve(args.out ?? root);
const playwrightModule = process.env.ARBS_PLAYWRIGHT_MODULE;
const chrome = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH;
if (!playwrightModule || !chrome) {
  console.error('set ARBS_PLAYWRIGHT_MODULE (playwright/index.mjs) and PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH (installed Chrome / Edge)');
  process.exit(2);
}
const { chromium } = await import(pathToFileURL(playwrightModule).href);

const ORANGE = '#c2410c';
const GREEN = '#15803d';
const DARK = '#1f2937';
const read = (name) => readFileSync(join(root, 'source', name), 'utf8');
const MASTER = read('arbs-icon-master.svg');
const SMALL = { 16: read('arbs-icon-16.svg'), 20: read('arbs-icon-20.svg'), 24: read('arbs-icon-24.svg') };

// ---------------------------------------------------------------------------------------------
// SVG text helpers (the drawing itself is never changed)
// ---------------------------------------------------------------------------------------------
/** Deployed copy of the master: C2PA metadata block and its namespace removed (P-9 a), drawing unchanged. */
function stripMetadata(svg) {
  return svg
    .replace(/\s*<metadata[\s\S]*?<\/metadata>/g, '')
    .replace(/\s+xmlns:c2pa="[^"]*"/g, '')
    .replace(/\r\n/g, '\n')
    .replace(/<\/g><\/svg>\s*$/, '</g>\n</svg>\n');
}
const DEPLOY = stripMetadata(MASTER);
function withSize(svg, size) {
  return svg.replace(/<svg\b([^>]*?)\swidth="[^"]*"/, '<svg$1').replace(/<svg\b([^>]*?)\sheight="[^"]*"/, '<svg$1').replace('<svg', `<svg width="${size}" height="${size}"`);
}
function viewBoxOf(svg) {
  const m = /viewBox="([^"]+)"/.exec(svg);
  return m[1].split(/\s+/).map(Number);
}
function innerOf(svg) {
  return svg.replace(/^[\s\S]*?<svg\b[^>]*>/, '').replace(/<\/svg>\s*$/, '').replace(/<title>[\s\S]*?<\/title>/g, '');
}
/** White drawing only (no background): Android foreground / monochrome, tray badge base checks. */
function glyphOf(svg) {
  return stripMetadata(svg).replace(/\s*<rect\b[^>]*fill="#c2410c"[^>]*\/>/, '').replace('<title>ARBS</title>', '<title>ARBS glyph</title>');
}
/** Full-bleed square background (iOS / Play / launcher round the corners themselves). Drawing unchanged. */
function squareOf(svg) {
  return stripMetadata(svg).replace(/(<rect\b[^>]*?)\srx="[^"]*"/, '$1');
}
function baseFor(size) {
  return SMALL[size] ?? DEPLOY;
}

/** Tray state badge (docs/analysis/H031.md §5-2): bottom-right circle with a white ring; shape differs per state. */
function traySvg(state, size) {
  const base = baseFor(size);
  const [, , vw, vh] = viewBoxOf(base);
  const s = size / 16; // 1 px at 16
  // Whole pixels so the badge stays sharp: diameter 7 / 9 / 11 / 14 px at 16 / 20 / 24 / 32, ring 1 px up to 31 px.
  const d = Math.round(7 * s);
  const ring = Math.max(1, Math.floor(s));
  const cx = size - d / 2;
  const cy = size - d / 2;
  let badge = '';
  if (state === 'playing') {
    badge = `<circle cx="${cx}" cy="${cy}" r="${d / 2}" fill="#fff"/><circle cx="${cx}" cy="${cy}" r="${d / 2 - ring}" fill="${GREEN}"/>`;
  } else if (state === 'pausing') {
    const bw = Math.max(1, Math.round(s));
    const bh = Math.round(3 * s);
    const gap = bw;
    const x0 = Math.round(cx - (2 * bw + gap) / 2);
    const y0 = Math.round(cy - bh / 2);
    badge =
      `<circle cx="${cx}" cy="${cy}" r="${d / 2}" fill="#fff"/>` +
      `<rect x="${x0}" y="${y0}" width="${bw}" height="${bh}" fill="${DARK}"/>` +
      `<rect x="${x0 + bw + gap}" y="${y0}" width="${bw}" height="${bh}" fill="${DARK}"/>`;
  } else if (state === 'locked') {
    badge = `<circle cx="${cx}" cy="${cy}" r="${d / 2}" fill="#fff"/><circle cx="${cx}" cy="${cy}" r="${d / 2 - ring}" fill="${DARK}"/>`;
    if (size >= 32) {
      const u = (d - 2 * ring) / 10; // padlock on a 10-unit grid inside the dark circle
      const bx = cx - 2.5 * u;
      const by = cy - 0.5 * u;
      badge +=
        `<rect x="${bx}" y="${by}" width="${5 * u}" height="${3.5 * u}" rx="${0.5 * u}" fill="#fff"/>` +
        `<path d="M${cx - 1.6 * u} ${by}V${by - 1.2 * u}A${1.6 * u} ${1.6 * u} 0 0 1 ${cx + 1.6 * u} ${by - 1.2 * u}V${by}" fill="none" stroke="#fff" stroke-width="${0.9 * u}"/>`;
    }
  }
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${size} ${size}" width="${size}" height="${size}">\n` +
    `  <title>ARBS tray (${state || 'default'}, ${size} px)</title>\n` +
    `  <svg x="0" y="0" width="${size}" height="${size}" viewBox="0 0 ${vw} ${vh}">${innerOf(base)}</svg>\n` +
    (badge ? `  ${badge}\n` : '') +
    `</svg>\n`
  );
}

// ---------------------------------------------------------------------------------------------
// PNG and ICO encoders (deterministic, no metadata chunks)
// ---------------------------------------------------------------------------------------------
const CRC = new Int32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c;
});
function crc32(buf) {
  let c = -1;
  for (const b of buf) c = CRC[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ -1) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function png(rgba, w, h) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // RGBA
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}
/** 32-bit BMP entry (BITMAPINFOHEADER, bottom-up BGRA + AND mask). */
function bmpEntry(rgba, s) {
  const head = Buffer.alloc(40);
  const rowMask = Math.ceil(s / 32) * 4;
  head.writeUInt32LE(40, 0);
  head.writeInt32LE(s, 4);
  head.writeInt32LE(s * 2, 8);
  head.writeUInt16LE(1, 12);
  head.writeUInt16LE(32, 14);
  head.writeUInt32LE(0, 16);
  head.writeUInt32LE(s * s * 4 + rowMask * s, 20);
  const px = Buffer.alloc(s * s * 4);
  const mask = Buffer.alloc(rowMask * s);
  for (let y = 0; y < s; y++) {
    const src = y * s * 4;
    const dstRow = s - 1 - y;
    for (let x = 0; x < s; x++) {
      const i = src + x * 4;
      const o = (dstRow * s + x) * 4;
      px[o] = rgba[i + 2];
      px[o + 1] = rgba[i + 1];
      px[o + 2] = rgba[i];
      px[o + 3] = rgba[i + 3];
      if (rgba[i + 3] === 0) mask[dstRow * rowMask + (x >> 3)] |= 0x80 >> (x & 7);
    }
  }
  return Buffer.concat([head, px, mask]);
}
/** ICO: entries <= 64 px as 32-bit BMP (widest compatibility), larger as PNG. */
function ico(images) {
  const list = [...images].sort((a, b) => a.size - b.size);
  const datas = list.map((im) => (im.size > 64 ? png(im.rgba, im.size, im.size) : bmpEntry(im.rgba, im.size)));
  const header = Buffer.alloc(6 + 16 * list.length);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(list.length, 4);
  let offset = header.length;
  list.forEach((im, n) => {
    const e = 6 + 16 * n;
    header[e] = im.size >= 256 ? 0 : im.size;
    header[e + 1] = im.size >= 256 ? 0 : im.size;
    header[e + 2] = 0;
    header[e + 3] = 0;
    header.writeUInt16LE(1, e + 4);
    header.writeUInt16LE(32, e + 6);
    header.writeUInt32LE(datas[n].length, e + 8);
    header.writeUInt32LE(offset, e + 12);
    offset += datas[n].length;
  });
  return Buffer.concat([header, ...datas]);
}

// ---------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------
const browser = await chromium.launch({ executablePath: chrome, headless: true });
const page = await browser.newPage();
await page.setContent('<!doctype html><html><body></body></html>');

async function render(svg, size) {
  const sized = withSize(svg, size);
  const b64 = await page.evaluate(
    async ([text, s]) => {
      const img = new Image();
      img.src = `data:image/svg+xml;base64,${btoa(unescape(encodeURIComponent(text)))}`;
      await img.decode();
      const c = document.createElement('canvas');
      c.width = s;
      c.height = s;
      const ctx = c.getContext('2d');
      ctx.clearRect(0, 0, s, s);
      ctx.drawImage(img, 0, 0, s, s);
      const data = ctx.getImageData(0, 0, s, s).data;
      let bin = '';
      for (let i = 0; i < data.length; i += 0x8000) bin += String.fromCharCode.apply(null, data.subarray(i, i + 0x8000));
      return btoa(bin);
    },
    [sized, size],
  );
  return Buffer.from(b64, 'base64');
}

const written = [];
function put(path, data) {
  const file = join(out, path);
  mkdirSync(dirname(file), { recursive: true });
  writeFileSync(file, data);
  written.push(path);
}

try {
  // 1. PNG sizes: 16 / 20 / 24 from the small-size versions, 32 and up from the master.
  const PNG_SIZES = [16, 20, 24, 32, 48, 64, 128, 256, 512, 1024];
  const appImg = {};
  for (const s of [...PNG_SIZES, 40]) appImg[s] = await render(baseFor(s), s);
  for (const s of PNG_SIZES) put(`png/arbs-${s}.png`, png(appImg[s], s, s));

  // 2. Windows icons.
  put('ico/arbs.ico', ico([16, 20, 24, 32, 48, 64, 256].map((size) => ({ size, rgba: appImg[size] }))));
  const TRAY_SIZES = [16, 20, 24, 32, 40, 48, 64, 128];
  for (const state of ['', 'playing', 'pausing', 'locked']) {
    const images = [];
    for (const size of TRAY_SIZES) {
      const svg = traySvg(state, size);
      if (state && [16, 20, 24, 32].includes(size)) put(`svg/arbs-tray-${state}-${size}.svg`, svg);
      images.push({ size, rgba: state ? await render(svg, size) : appImg[size] ?? (await render(baseFor(size), size)) });
    }
    put(`ico/arbs-tray${state ? `-${state}` : ''}.ico`, ico(images));
  }

  // 3. Web.
  put('web/favicon.ico', ico([16, 32, 48].map((size) => ({ size, rgba: appImg[size] }))));
  put('web/icon.svg', DEPLOY);
  put('web/arbs-logo.svg', DEPLOY);
  const square = squareOf(MASTER);
  put('web/apple-touch-icon.png', png(await render(square, 180), 180, 180));
  put('web/arbs-logo-45.png', png(await render(DEPLOY, 45), 45, 45));
  put('web/arbs-logo-90.png', png(await render(DEPLOY, 90), 90, 90));

  // 4. White drawing only.
  const glyph = glyphOf(MASTER);
  put('svg/arbs-glyph.svg', glyph);

  // 5. Android adaptive icon (files only; applied in a later ARBS-A task). 108 dp canvas, drawing inside the 66 dp
  // safe zone: the 64-unit drawing (its strokes reach 31.8 units from the centre) at 1 dp per unit, moved by 22 dp, so it
  // stays inside the 66 dp safe circle with about 1 dp to spare.
  const strokes = [...glyph.matchAll(/<path d="([^"]+)" stroke-width="([^"]+)"\/>/g)].map((m) => ({ d: m[1], w: m[2] }));
  const padGroup = /<g transform="translate\(32 39\) scale\(\.78\) translate\(-32 -36\.75\)" stroke-width="([^"]+)">\s*<path d="([^"]+)"\/>/.exec(glyph);
  if (strokes.length !== 2 || !padGroup) throw new Error('master drawing layout not recognised (expected arch, wave and the gamepad group)');
  const vdPath = (d, w, color) =>
    `    <path android:pathData="${d}" android:strokeColor="${color}" android:strokeWidth="${w}" android:strokeLineCap="round" android:strokeLineJoin="round" android:fillColor="#00000000"/>`;
  const vector = (color) =>
    `<?xml version="1.0" encoding="utf-8"?>
<!-- ARBS adaptive icon layer (generated by branding/tools/make-icons.mjs from source/arbs-icon-master.svg). -->
<vector xmlns:android="http://schemas.android.com/apk/res/android"
    android:width="108dp" android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">
  <group android:translateX="22" android:translateY="22">
${vdPath(strokes[0].d, strokes[0].w, color)}
    <group android:translateX="32" android:translateY="39" android:scaleX="0.78" android:scaleY="0.78">
      <group android:translateX="-32" android:translateY="-36.75">
${vdPath(padGroup[2], padGroup[1], color).replace(/^/gm, '    ')}
      </group>
    </group>
${vdPath(strokes[1].d, strokes[1].w, color)}
  </group>
</vector>
`;
  put('android/res/drawable/ic_launcher_foreground.xml', vector('#FFFFFFFF'));
  put('android/res/drawable/ic_launcher_monochrome.xml', vector('#FFFFFFFF'));
  put('android/res/values/ic_launcher_background.xml', `<?xml version="1.0" encoding="utf-8"?>\n<resources>\n  <color name="ic_launcher_background">${ORANGE}</color>\n</resources>\n`);
  const adaptive = `<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
  <background android:drawable="@color/ic_launcher_background"/>
  <foreground android:drawable="@drawable/ic_launcher_foreground"/>
  <monochrome android:drawable="@drawable/ic_launcher_monochrome"/>
</adaptive-icon>
`;
  put('android/res/mipmap-anydpi-v26/ic_launcher.xml', adaptive);
  put('android/res/mipmap-anydpi-v26/ic_launcher_round.xml', adaptive);
  for (const [dpi, s] of Object.entries({ mdpi: 48, hdpi: 72, xhdpi: 96, xxhdpi: 144, xxxhdpi: 192 })) {
    put(`android/res/mipmap-${dpi}/ic_launcher.png`, png(await render(DEPLOY, s), s, s));
  }
  put('android/play-512.png', png(await render(square, 512), 512, 512));

  // 6. Check sheets: 16 / 20 / 24 / 32 at x8 (pixels kept), light and dark backgrounds; tray states the same way;
  // adaptive icon foreground inside a circle and a squircle mask.
  const fileUrl = (p) => `data:image/png;base64,${readFileSync(join(out, p)).toString('base64')}`;
  async function sheet(name, rows, bg, fg) {
    const html = `<!doctype html><html><body style="margin:0;background:${bg};color:${fg};font:14px sans-serif">
      <div style="padding:16px;display:grid;gap:12px">${rows
        .map(
          (r) => `<div style="display:flex;align-items:end;gap:20px"><span style="width:140px">${r.label}</span>${r.items
            .map(
              (it) => `<figure style="margin:0;text-align:center"><img src="${it.src}" width="${it.size * 8}" height="${it.size * 8}" style="image-rendering:pixelated;display:block"><img src="${it.src}" width="${it.size}" height="${it.size}" style="display:block;margin:6px auto 0"><figcaption>${it.size}px</figcaption></figure>`,
            )
            .join('')}</div>`,
        )
        .join('')}</div></body></html>`;
    await page.setViewportSize({ width: 1400, height: 400 });
    await page.setContent(html);
    await page.waitForTimeout(100);
    const file = join(out, 'check', `${name}.png`);
    mkdirSync(dirname(file), { recursive: true });
    await page.screenshot({ path: file, fullPage: true });
    written.push(`check/${name}.png`);
  }
  const sizes = [16, 20, 24, 32];
  const temp = {};
  for (const state of ['', 'playing', 'pausing', 'locked']) {
    temp[state] = [];
    for (const s of sizes) {
      const rgba = state ? await render(traySvg(state, s), s) : appImg[s];
      temp[state].push({ size: s, src: `data:image/png;base64,${png(rgba, s, s).toString('base64')}` });
    }
  }
  const appRow = { label: 'app icon', items: sizes.map((s) => ({ size: s, src: fileUrl(`png/arbs-${s}.png`) })) };
  for (const [bg, fg, tag] of [
    ['#f5f5f4', '#1c1917', 'light'],
    ['#1c1917', '#f5f5f4', 'dark'],
  ]) {
    await sheet(`small-x8-${tag}`, [appRow], bg, fg);
    await sheet(
      `tray-x8-${tag}`,
      ['', 'playing', 'pausing', 'locked'].map((st) => ({ label: `tray ${st || 'default'}`, items: temp[st] })),
      bg,
      fg,
    );
  }
  // tray at the notification size (2 x SM_CXICON = 64 at 100 %), shown at x2
  const big = [];
  for (const st of ['', 'playing', 'pausing', 'locked']) {
    const rgba = st ? await render(traySvg(st, 64), 64) : appImg[64];
    big.push({ size: 64, src: `data:image/png;base64,${png(rgba, 64, 64).toString('base64')}` });
  }
  await page.setContent(
    `<!doctype html><body style="margin:0;padding:16px;background:#f5f5f4;display:flex;gap:24px">${big
      .map((b, i) => `<figure style="margin:0;text-align:center"><img src="${b.src}" width="128" height="128" style="image-rendering:pixelated"><figcaption>${['default', 'playing', 'pausing', 'locked'][i]} 64px x2</figcaption></figure>`)
      .join('')}</body>`,
  );
  await page.screenshot({ path: join(out, 'check', 'tray-64-x2.png'), fullPage: true });
  written.push('check/tray-64-x2.png');
  // Android: foreground on the background, masked as a circle (66 dp safe zone ring drawn) and a squircle.
  await page.setContent(
    `<!doctype html><body style="margin:0;padding:16px;background:#e7e5e4;display:flex;gap:24px">
    ${['50%', '30%'].map((r) => `<div style="position:relative;width:216px;height:216px;border-radius:${r};overflow:hidden;background:${ORANGE}">
      <img src="data:image/svg+xml;base64,${Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 108 108" width="216" height="216"><g transform="translate(22 22)">${innerOf(glyph)}</g><circle cx="54" cy="54" r="33" fill="none" stroke="#000" stroke-opacity=".35" stroke-width=".5" stroke-dasharray="2 2"/></svg>`).toString('base64')}" width="216" height="216"></div>`).join('')}
    </body>`,
  );
  await page.screenshot({ path: join(out, 'check', 'android-adaptive-x2.png'), fullPage: true });
  written.push('check/android-adaptive-x2.png');
} finally {
  await browser.close();
}

// 7. Hash list of everything generated (and the sources).
const all = [];
function walk(dir) {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p);
    else all.push(p);
  }
}
walk(join(root, 'source'));
for (const d of ['png', 'ico', 'web', 'svg', 'android']) walk(join(out, d));
const lines = all
  .map((p) => [relative(out, p).replace(/\\/g, '/'), createHash('sha256').update(readFileSync(p)).digest('hex')])
  .sort((a, b) => a[0].localeCompare(b[0]))
  .map(([p, h]) => `${h}  ${p}`);
writeFileSync(join(out, 'SHA256SUMS'), lines.join('\n') + '\n');
console.log(`written ${written.length} files; SHA256SUMS ${lines.length} lines`);
