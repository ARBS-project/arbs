# NOTICE — ARBS project

ARBS is distributed under the **GNU General Public License v3.0** ([LICENSE](LICENSE)). ARBS Core source files in `core/`
carry `SPDX-License-Identifier: GPL-3.0-or-later`. Each ARBS fork keeps its upstream license files unchanged and has its own
`NOTICE-ARBS.md` with the exact base commit and the list of ARBS changes.

## Upstream works used by the ARBS repositories

| Work | Source | Used in | License |
|---|---|---|---|
| Vibepollo | [Nonary/Vibepollo](https://github.com/Nonary/Vibepollo), base commit `c286d0862` (2.18.9) | arbs-h | GPL-3.0 |
| Apollo (upstream of Vibepollo) | [ClassicOldSong/Apollo](https://github.com/ClassicOldSong/Apollo) | arbs-h | GPL-3.0 |
| Sunshine (upstream of Apollo) | [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine) | arbs-h | GPL-3.0 |
| Moonlight PC (moonlight-qt) | [moonlight-stream/moonlight-qt](https://github.com/moonlight-stream/moonlight-qt), base commit `28629dc1` | arbs-w | GPL-3.0 |
| Moonlight for Android | [moonlight-stream/moonlight-android](https://github.com/moonlight-stream/moonlight-android), base commit `b48494cb` | arbs-a | GPL-3.0 |
| moonlight-common-c (official) | [moonlight-stream/moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) | upstream of the Nonary fork | GPL-3.0 |
| moonlight-common-c (Nonary fork) | [Nonary/moonlight-common-c](https://github.com/Nonary/moonlight-common-c) | arbs-h (submodule); arbs-w (submodule at public commit `6f05ee89` + ARBS patch in arbs-w `patches/`); arbs-a (copied work tree, commit `6f05ee89`) | GPL-3.0 |
| ENet (Moonlight fork) | [cgutman/enet](https://github.com/cgutman/enet), inside moonlight-common-c | arbs-h, arbs-w, arbs-a | MIT |
| SudoVDA (bundled driver) | [SudoMaker/SudoVDA](https://github.com/SudoMaker/SudoVDA) | arbs-h installer | README: "MIT and CC0 or Public Domain" for the author's changes; no top-level LICENSE file |
| Windows-driver-samples (basis of SudoVDA) | [microsoft/Windows-driver-samples](https://github.com/microsoft/Windows-driver-samples) | arbs-h installer | MS-PL |
| Other bundled third-party code | as listed in each upstream repository (`third-party/`, `.gitmodules`, `libs/`, upstream `NOTICE`) | each fork | their own licenses |

The ARBS-H build applies a small patch to the bundled ENet (precise time). That patch and the build scripts are in
[`build/`](build/README.md). The ARBS-W build keeps moonlight-common-c at public commit `6f05ee89` and applies
`patches/moonlight-common-c-6f05ee89-arbs.patch` from the arbs-w repository.

## ApexSenseBridge

The ARBS project refers to [ApexSenseBridge](https://github.com/ReynArts/ApexSenseBridge) (commit `d831575`) **for protocol
facts only** (APEX HID report byte layouts, used in the clients and ARBS Core `core/include/arbs/protocol.h`).
**No ApexSenseBridge source code is copied** into the ARBS repositories.
License note: the ApexSenseBridge `LICENSE` file is GPL-3.0-or-later, while its README badge says MIT.
Either way it is compatible with this GPL-3.0 project.

## Trademarks

Flydigi, APEX, DualSense, PlayStation, Moonlight, Sunshine, Apollo, Vibepollo and other names are trademarks or names of
their respective owners. ARBS is an unofficial open-source project and is not affiliated with or endorsed by Flydigi,
Sony, Moonlight, Sunshine, Apollo or Vibepollo.

## Obligations when distributing (GPL-3.0)

Provide the complete corresponding source under the same license (the fork repositories **and** ARBS Core), keep
copyright and license notices, mark modifications, and add no further restrictions.
