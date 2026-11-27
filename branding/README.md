# ARBS icon

The ARBS icon: an orange (`#c2410c`) rounded square with a white arch (bridge) and its two side decks, a plain gamepad
outline inside the arch, and a soft wave inside the gamepad.

## Origin and license

- **Made by the ARBS project** (2026-11-27). The base vector `source/arbs-icon-master.svg` was drawn for ARBS by the
  project's planner from the user's chosen sketch; the sketch itself is not part of this folder or of any release.
- It does not use any company's controller shape, button symbols or logo.
- License: **GPL-3.0-or-later**, the same as this repository ([../LICENSE](../LICENSE)).

## Files

| Path | What |
|---|---|
| `source/arbs-icon-master.svg` | Base vector (64-unit grid). Kept as delivered, including its C2PA metadata block. |
| `source/arbs-icon-16.svg`, `-20.svg`, `-24.svg` | Small-size versions drawn on the pixel grid: same parts, 2 px arch and decks, thicker gamepad line, wave with 2 bumps. Used for 16 / 20 / 24 px instead of shrinking the master. |
| `png/arbs-<size>.png` | 16, 20, 24 (small-size versions), 32, 48, 64, 128, 256, 512, 1024 (master). |
| `ico/arbs.ico` | Windows app icon: 16, 20, 24, 32, 48, 64 (32-bit BMP entries) and 256 (PNG entry). |
| `ico/arbs-tray.ico`, `arbs-tray-playing.ico`, `arbs-tray-pausing.ico`, `arbs-tray-locked.ico` | ARBS-H tray states: 16, 20, 24, 32, 40, 48, 64, 128. Default = the app icon; streaming = green dot; paused = white dot with two bars; locked = dark dot (padlock from 32 px). Each badge has a white ring and its own shape, so colour is not the only difference. |
| `svg/arbs-tray-<state>-<size>.svg` | The tray badge drawings at 16 / 20 / 24 / 32 px (generated). |
| `svg/arbs-tray-base-32.svg` | Tray only, 32 px: the master with the two side-deck ends moved inward (3.5 -> 7, 60.5 -> 57) so they do not look cut at the tile edge; arch, gamepad and wave unchanged. The app icon keeps the master. |
| `check/margins.json` | Whole orange pixels between each side-deck end and the outside of the tile (16 / 20 / 24 / 32 / 48 app, 16-32 tray states). Small versions keep at least 1 px at 16 and 2 px at 20 / 24 / tray 32. |
| `svg/arbs-glyph.svg` | White drawing only, no background (generated). |
| `web/favicon.ico` (16, 32, 48), `web/icon.svg`, `web/arbs-logo.svg`, `web/apple-touch-icon.png` (180, full square), `web/arbs-logo-45.png`, `web/arbs-logo-90.png` | Web copies. The SVGs are the master without its metadata block (drawing unchanged). |
| `android/res/...`, `android/play-512.png` | Android adaptive icon (background `#c2410c`, white foreground and monochrome layer inside the 66 dp safe zone) and legacy launcher PNGs. Files only: ARBS-A uses them in a later update. |
| `check/*.png` | Enlarged (x8) checks of 16 / 20 / 24 / 32 px on light and dark backgrounds, tray states, adaptive icon masks. |
| `SHA256SUMS` | SHA-256 of every file above except `check/` and `tools/`. |
| `tools/make-icons.mjs` | Generator (see [tools/README.md](tools/README.md)). |

## Where the repositories use it

- **arbs-h** (ARBS-H 0.3.1): `branding/arbs.ico` (sunshine.exe, tool exes, installer and uninstaller), tray icons and web
  copies under `src_assets/common/assets/web-legacy/public/images/`, `web/public/images/`, `web-arbs/public/images/`.
  The older Vibepollo / Apollo icon files stay in that repository; only the references changed.
- **arbs-w**, **arbs-a**: in a later update of each (not yet).
