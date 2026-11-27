# make-icons.mjs

Builds every icon file in `../` from `../source/*.svg`. The same input gives the same output (checked: two runs, same
`SHA256SUMS`).

- Renders SVG with an **installed** Chrome or Edge through Playwright (no browser download), then encodes PNG and ICO
  itself with Node `zlib` (no metadata chunks, no timestamps).
- ICO entries up to 64 px are 32-bit BMP (with an AND mask), larger ones PNG.
- Tested with Node 22.22.2, Playwright 1.55.0, Chrome 159.0.8154.34 (Windows 11).
- Does **not** use ImageMagick or any `convert` command. On Windows `convert` is the FAT-to-NTFS disk conversion tool:
  never run it for images.

```sh
PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH="C:\Program Files\Google\Chrome\Application\chrome.exe" \
ARBS_PLAYWRIGHT_MODULE="<folder with node_modules>/playwright/index.mjs" \
node tools/make-icons.mjs            # writes into branding/
node tools/make-icons.mjs --out DIR  # writes into DIR (sources are still read from branding/source)
```

The source drawings are edited by hand. Do not change the master's parts (arch, decks, gamepad, wave); the small-size
versions follow the same parts on the 16 / 20 / 24 pixel grid.
