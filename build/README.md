# ARBS build files

Patches and scripts used for the ARBS-H (host) and ARBS-W (Windows client) builds, published for GPL-3.0 source
availability. Paths inside the scripts are those of the original build PC (`D:\AI\ARBS-APEX`, `D:\AI\ARBS-APEX\toolchain`); adjust them.

| Folder | Contents | Original location |
|---|---|---|
| `patches/enet-c7353c05-arbs-precise-time.patch` | ENet patch applied during the ARBS-H build | `host/patches` |
| `patches/arbs-h-b1-*.diff` | Early ARBS-H changes, as plain diffs. Already part of the `arbs-h` history | |
| `host/` | ARBS-H environment, configure, build, test and packaging scripts (MSYS2 UCRT64, Inno Setup) | `host/scripts` |
| `windows-client/` | ARBS-W build wrapper, zip assembly, executable test | `client-w/scripts` |
| `windows-client/test-kit/` | ARBS-W install, update and log tools (shipped in the zip as `ARBS-W 도구`) | `client-w/test-kit` |

## ARBS-H

Check out `arbs-h` with submodules, then run `r1a_configure.sh` (applies the ENet patch through `b1_apply_patches.sh`),
`b1_build.sh`, `b1_ctest.sh` and `r1a_package.sh` in an MSYS2 UCRT64 shell (`b1_run.cmd` starts that shell).

## ARBS-W

`arbs-w` keeps the moonlight-common-c submodule at public commit `6f05ee89`. Its `patches/moonlight-common-c-6f05ee89-arbs.patch`
is applied at build time by `scripts/arbs-w-apply-patches.ps1`, which stops unless the patched tree is
`a7fc9110d1418bf75e569a9f04aeebe5fc08ca70`. ARBS Core comes from `core/` in this repository (`ARBS_CORE_DIR`).
Run `arbs-w-build.bat arbs-w-<version>-windows-portable.zip`.
