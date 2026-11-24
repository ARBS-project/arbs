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
- `build/` — patches and build scripts for the host and Windows client ([build/README.md](build/README.md)).

## Status

**Experimental.** Only items marked "confirmed" have been checked on real devices.

| Component | Status |
|---|---|
| ARBS-H 0.1.2 (installer) | Confirmed on real devices (local and external network) |
| ARBS-W 0.1.1 (portable) | Released (install tool fixes); not yet checked on real devices |
| ARBS-W 0.1.0 (portable) | Confirmed on real devices (local and external network) |
| ARBS-A | In preparation |

## Download

Download the latest release from the
[arbs-h Releases](https://github.com/ARBS-project/arbs-h/releases) and
[arbs-w Releases](https://github.com/ARBS-project/arbs-w/releases) pages. ARBS-A has no release yet.

## Install and update (summary)

- **ARBS-H (host):** download `arbs-h-<version>-setup.exe` and its `.sha256`, check the SHA-256, then run the installer.
  Running a newer installer updates the existing installation.
- **ARBS-W (Windows client):** put `arbs-w-<version>-windows-portable.zip` and its `.sha256` in the same folder, unzip, and
  run `ARBS-W 설치·업데이트.cmd` in the `ARBS-W 도구` folder. It installs to `D:\ARBS-W` (or `%LOCALAPPDATA%\ARBS-W` when
  there is no D: drive), adds an `ARBS-W` desktop shortcut and keeps settings and pairing on update.
- **ARBS-A (Android client):** when a release is available, download `arbs-a-<version>-release.apk` and its `.sha256` from the
  [arbs-a Releases](https://github.com/ARBS-project/arbs-a/releases) page, compare the SHA-256, open the APK on the device (allow
  installing from this source when Android asks), pair once with the PIN and connect the APEX dongle by USB. The application ID
  `io.github.arbs_project.arbsa` is new, so it is a separate app from earlier test builds.
  Release signing certificate SHA-256: _to be added after the device check_
- **Unsigned builds:** the ARBS-H and ARBS-W binaries are not code-signed, so Windows SmartScreen may warn. Compare the SHA-256 value first.
  See [Code signing policy](#code-signing-policy).

## Code signing policy

The current ARBS-H and ARBS-W releases are **not code-signed**. ARBS is applying for free code signing for open-source
projects from SignPath Foundation. This section is the policy for signed releases of ARBS-H and ARBS-W; it applies once
signing is enabled. ARBS-A (Android APK) is not covered by this policy.

- **What is signed:** only files built from ARBS sources, automatically by GitHub Actions on GitHub-hosted runners
  (`.github/workflows/build-windows.yml` in [arbs-h](https://github.com/ARBS-project/arbs-h) and
  [arbs-w](https://github.com/ARBS-project/arbs-w); build scripts in `build/` of this repository):
  - ARBS-H: the installer `arbs-h-<version>-setup.exe` and the ARBS-H executables and DLLs built from source inside it.
  - ARBS-W: `ARBS-W.exe` and `AntiHooking.dll` in the portable zip.
  - The exact file lists are in `.signpath/artifact-configurations/` of arbs-h and arbs-w.
- **Roles:** ARBS has one maintainer. Committers and reviewers: the maintainer. Approvers: the maintainer.
  All changes are written, reviewed and approved by the maintainer, and every signing request is approved manually by
  the maintainer.
- **Third-party components:** upstream and third-party binaries are **not re-signed** by ARBS. They are excluded from
  the signing request (the artifact configurations list only ARBS files by exact path) and are shipped byte-for-byte
  unchanged, so any original signature is kept (for example the Microsoft VC runtime and the driver catalog signatures).
  - ARBS-H: `nvngx_truehdr.dll` (NVIDIA RTX Video SDK runtime, proprietary, not open source, used only by the optional
    RTX HDR / TrueHDR feature, taken unchanged from the pinned upstream release), `vibeshine_truehdr.dll` (same upstream
    release), `zlib1.dll` (MSYS2), `msvcp140.dll` / `vcruntime140*.dll` (Microsoft VC runtime), `drivers/sudovda/*` and
    `drivers/usbip-win2/*` (upstream drivers).
  - ARBS-W: Qt6 DLLs and plugins (Qt), FFmpeg-family `av*` / `swscale-10.dll`, dav1d, libplacebo, opus, SDL2, SDL2_ttf,
    SDL3, libcrypto, libssl (moonlight-qt-deps), `msvcp140*` / `vcruntime140*` / `concrt140.dll` / `vccorlib140.dll` (Microsoft).
  - Known limitation: the Inno Setup uninstaller `unins000.exe` is generated inside the ARBS-H installer and is not signed.
- **Privacy:** ARBS-H installer builds check for a new version once a day by default, calling only the GitHub API
  (releases list of `arbs-project/arbs-h`). No personal data is sent (no account, host name, hardware or usage data;
  GitHub sees the connection's IP address as with any HTTPS request). It can be turned off with `update_check_interval = 0`
  (web UI: Host & diagnostics > Identity and local behavior > Update Check Interval). ARBS-W connects only to the user's
  own host chosen by the user; its update check is off by default. No other information is sent to other networked systems.
- **Attribution:** _placeholder — the SignPath attribution line will be added here only after the application is approved._

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
상태: 실험적. ARBS-H 0.1.2·ARBS-W 0.1.0 실기 확인(내부망·외부망), ARBS-W 0.1.1 배포(설치 도구 정리, 실기 미확인), ARBS-A 준비 중입니다.
다운로드: [arbs-h Releases](https://github.com/ARBS-project/arbs-h/releases), [arbs-w Releases](https://github.com/ARBS-project/arbs-w/releases). ARBS-A는 아직 배포하지 않습니다.
설치·업데이트: ARBS-H는 `arbs-h-<버전>-setup.exe`의 SHA-256을 확인한 뒤 실행(새 설치기로 덮어 업데이트). ARBS-W는 zip과 `.sha256`을 같은 폴더에 두고
압축을 푼 뒤 `ARBS-W 도구\ARBS-W 설치·업데이트.cmd`를 실행합니다(설정·페어링 유지). ARBS-H·ARBS-W는 코드 서명이 없어 SmartScreen 경고가 나올 수 있으니 SHA-256을 먼저 확인하세요.
코드 서명 정책([Code signing policy](#code-signing-policy)): 현재 릴리스는 서명되지 않았고 SignPath Foundation에 무료 오픈소스 서명을 신청하는 중입니다.
서명 대상은 GitHub 호스티드 러너의 GitHub Actions가 ARBS 소스로 만든 파일만(ARBS-H 설치 프로그램과 그 안의 ARBS-H 빌드 exe·dll, ARBS-W의 `ARBS-W.exe`·`AntiHooking.dll`)이며,
역할은 유지보수자 1인이 작성·검토·승인하고 서명 요청마다 수동 승인합니다. 업스트림·제3자 바이너리(`nvngx_truehdr.dll` 등)는 재서명하지 않고 서명 대상에서 제외해 바이트 그대로 넣으므로 원래 서명이 유지됩니다.
개인정보: ARBS-H 설치본은 기본값으로 하루 1회 GitHub API(`arbs-project/arbs-h` Releases 목록)만 조회해 새 버전을 확인한다. 보내는 개인정보는 없다(계정·호스트 이름·하드웨어·사용 정보 없음; 일반 HTTPS 연결처럼 GitHub에는 접속 IP가 보임). 설정 `update_check_interval = 0`(웹 UI: Host & diagnostics > Identity and local behavior > 업데이트 확인 간격(초))으로 끌 수 있다. ARBS-W는 사용자가 지정한 자신의 호스트에만 연결하며 업데이트 확인은 기본 꺼짐. 그 밖의 곳으로 정보를 보내지 않는다.
SignPath 표기 문구는 신청이 승인된 뒤에 위 절에 넣습니다.
ARBS-A는 릴리스가 올라오면 [arbs-a Releases](https://github.com/ARBS-project/arbs-a/releases)에서 `arbs-a-<버전>-release.apk`와 `.sha256`을 받아 SHA-256을 비교하고,
기기에서 APK를 열어(묻는 경우 이 출처의 앱 설치 허용) 설치한 뒤 PIN으로 한 번 페어링하고 APEX 동글을 USB로 연결합니다. 새 앱 ID(`io.github.arbs_project.arbsa`)라서 이전 시험판과 별개 앱입니다.
릴리스 서명 인증서 SHA-256: _실기 확인 뒤 기재_
**Flydigi와 관련 없는 비공식 오픈소스 프로젝트**이며 Moonlight·Sunshine·Apollo·Vibepollo 프로젝트와도 관련이 없습니다.
제품 이름과 상표는 각 소유자의 것입니다.
