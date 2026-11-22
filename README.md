# ARBS — APEX Remote Bridge STACKON

> **Unofficial open-source project.** ARBS is not affiliated with, endorsed by, or sponsored by Flydigi.
> It is also not affiliated with the Moonlight, Sunshine, Apollo or Vibepollo projects.
> All product names and trademarks belong to their respective owners. This repository contains no Flydigi logos or images.

## What is this?

ARBS lets a **Flydigi APEX 6 Pro** controller connected to a game-streaming **client** feel the host game's DualSense
output — standard rumble, adaptive-trigger effects and PCM HD haptics — while streaming over the network.

ARBS consists of four repositories in the [ARBS-project](https://github.com/ARBS-project) organisation:

| Repository | Role |
|---|---|
| [arbs](https://github.com/ARBS-project/arbs) | Project overview, license notices, ARBS Core sources (this repository) |
| [arbs-h](https://github.com/ARBS-project/arbs-h) | Host (fork of Vibepollo 2.18.9) |
| [arbs-w](https://github.com/ARBS-project/arbs-w) | Windows client (fork of moonlight-qt) |
| [arbs-a](https://github.com/ARBS-project/arbs-a) | Android client (fork of moonlight-android) |

## Contents of this repository

- `core/` — **ARBS Core** (C++17): APEX protocol frames, rumble synthesis, trigger-effect bridge, PCM haptics pipeline,
  jitter buffer and scheduler, with unit and golden tests. It is built into ARBS-W
  (set `ARBS_CORE_DIR` to this `core` folder when building ARBS-W).
  - `core/run_tests.bat` builds the library and runs the tests with MSVC (Windows). Optional environment variables:
    `ARBS_VCVARS` (path of `vcvarsall.bat`) and `ARBS_TOOLCHAIN_DIR` (folder for dotnet/nuget caches); when they are
    empty, the previous defaults are used.
- `LICENSE`, `NOTICE.md`, `SECURITY.md`.
- Planned: user guide (`docs/`) and the build scripts / ENet patch used for the host and Windows client builds (`build/`),
  published before any binary release.

## Status

**Experimental.** There are no binary releases yet. This section will list only what has actually been confirmed on
real devices; until an item appears here, treat it as unverified.

## License

GPL-3.0 — see [LICENSE](LICENSE). ARBS Core source files are marked `GPL-3.0-or-later`.
Upstream works and third-party notices: [NOTICE.md](NOTICE.md). Security issues: [SECURITY.md](SECURITY.md).

---

## 한국어 요약

ARBS는 게임 스트리밍 **클라이언트**에 연결한 Flydigi APEX 6 Pro에서 호스트 게임의 DualSense 출력(일반 진동·트리거 효과·PCM HD 햅틱)을
느끼게 하는 프로젝트입니다. 구성: [arbs](https://github.com/ARBS-project/arbs)(소개·고지·ARBS Core 소스, 이 저장소),
[arbs-h](https://github.com/ARBS-project/arbs-h)(호스트, Vibepollo 2.18.9 포크), [arbs-w](https://github.com/ARBS-project/arbs-w)(Windows 클라이언트,
moonlight-qt 포크), [arbs-a](https://github.com/ARBS-project/arbs-a)(Android 클라이언트, moonlight-android 포크).
`core/`는 ARBS Core(C++17) 소스와 시험이며 ARBS-W 빌드에 쓰입니다(`ARBS_CORE_DIR`). 라이선스 GPL-3.0(Core 파일은 GPL-3.0-or-later).
상태: 실험적, 바이너리 배포 없음. 실기기에서 확인된 항목만 이 README에 적습니다.
**Flydigi와 관련 없는 비공식 오픈소스 프로젝트**이며 Moonlight·Sunshine·Apollo·Vibepollo 프로젝트와도 관련이 없습니다.
제품 이름과 상표는 각 소유자의 것입니다.
