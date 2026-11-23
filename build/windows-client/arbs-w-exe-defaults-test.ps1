# SPDX-License-Identifier: GPL-3.0-or-later
# ARBS-W B4b F3 (D-B4b-1, docs/analysis/B4b.md 1-3): 빌드된 exe 기준 기본값 시험.
# 빌드 zip 을 -Scratch(새 임시 폴더)에 풀고 'ARBS-W.exe --arbs-dump-prefs' 를 세 경우로 실행한다.
#   A: 설정 파일 없음(작업 폴더 = 다른 폴더, 바로 가기 실행 흉내) -> gamepadmouse=false·detectnetblocking=false 출처=기본값,
#      실행 뒤 INI·ARBS-*.log·Moonlight-*.log 가 생기지 않음(SDL·HID·엔진 전 조기 종료, 저장 없음)
#   B: INI 에 [General] gamepadmouse=true -> true 출처=파일(덤프가 실제 포터블 설정 경로를 읽는다는 대조군), INI 바뀌지 않음
#   C: portable.dat 지움 -> 종료 코드 3, 덤프 없음(레지스트리를 읽지 않음)
# 장치 신호 없음. 설치·레지스트리·서비스 변경 없음. 설정은 -Scratch 아래만. 끝나면 -Scratch 삭제(-Keep 이면 남김).
# 사용: powershell -NoProfile -ExecutionPolicy Bypass -File arbs-w-exe-defaults-test.ps1 -Scratch <새 폴더> [-Zip <zip>] [-Edition b2h]
param(
    [Parameter(Mandatory = $true)][string]$Scratch,
    [string]$Zip = '',
    [string]$Edition = '0.1.0',
    [int]$TimeoutSec = 20,
    [switch]$Keep
)
$ErrorActionPreference = 'Stop'
if (-not $Zip) { $Zip = Join-Path (Split-Path -Parent $PSScriptRoot) 'out\arbs-w-b2h-portable.zip' }   # default: client-w\out\arbs-w-b2h-portable.zip
$script:pass = 0; $script:fail = 0
function Check([bool]$Cond, [string]$What) {
    if ($Cond) { $script:pass++; Write-Output ('[ OK ] ' + $What) }
    else { $script:fail++; Write-Output ('[FAIL] ' + $What) }
}

# 시간 제한 실행: 끝나지 않으면 그 프로세스만 종료하고 실패로 기록. 결과 = 종료 코드(시간 초과 = $null)
function Invoke-Dump([string]$Exe, [string]$WorkDir) {
    $p = Start-Process -FilePath $Exe -ArgumentList '--arbs-dump-prefs' -WorkingDirectory $WorkDir -PassThru
    $null = $p.Handle   # PS 5.1: ExitCode 를 받으려면 끝나기 전에 핸들을 잡아 둔다
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        try { $p.Kill() } catch { }
        [void]$p.WaitForExit(5000)
        return $null
    }
    return $p.ExitCode
}
function Read-Dump([string]$Path) {
    $m = @{}
    foreach ($l in [IO.File]::ReadAllLines($Path, [Text.Encoding]::UTF8)) {
        $i = $l.IndexOf('=')
        if ($i -gt 0) { $m[$l.Substring(0, $i)] = $l.Substring($i + 1) }
    }
    return $m
}
function Get-Leftover([string]$Exe) {
    return @(Get-Process -Name 'ARBS-W' -ErrorAction SilentlyContinue | Where-Object {
            $q = $null; try { $q = $_.Path } catch { }; $q -and ($q -ieq $Exe) })
}

if (Test-Path -LiteralPath $Scratch) { throw 'Scratch 는 아직 없는 새 폴더여야 함' }
if (-not (Test-Path -LiteralPath $Zip -PathType Leaf)) { throw ('zip 없음: ' + $Zip) }
New-Item -ItemType Directory -Path $Scratch | Out-Null
$S = (Resolve-Path -LiteralPath $Scratch).ProviderPath
Write-Output ('zip: ' + (Split-Path -Leaf $Zip) + ' sha256 ' + (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant())

$pkg = Join-Path $S 'pkg'
$other = Join-Path $S 'other-workdir'
New-Item -ItemType Directory -Path $other | Out-Null
Expand-Archive -LiteralPath $Zip -DestinationPath $pkg
$exe = Join-Path $pkg 'ARBS-W.exe'
$dump = Join-Path $pkg 'arbs-prefs-dump.txt'
# R1b (D-R1b-3): settings moved to ARBS\ARBS-W.ini; the old location is only read/copied.
$iniDir = Join-Path $pkg 'ARBS'
$ini = Join-Path $iniDir 'ARBS-W.ini'
$oldIniDir = Join-Path $pkg 'Moonlight Game Streaming Project'
$oldIni = Join-Path $oldIniDir 'Moonlight.ini'
Check ((Test-Path -LiteralPath $exe) -and (Test-Path -LiteralPath (Join-Path $pkg 'portable.dat'))) 'zip: ARBS-W.exe·portable.dat 있음'
Check (-not (Test-Path -LiteralPath $iniDir)) 'zip: 설정 폴더 없음(새 폴더 상태)'
Check (-not (Test-Path -LiteralPath (Join-Path $pkg 'Moonlight.exe'))) 'zip: 옛 실행 파일 이름 없음'
$before = @(Get-ChildItem -LiteralPath $pkg -Recurse -Force | ForEach-Object { $_.FullName })

# ---- 경우 A: 설정 파일 없음, 작업 폴더 = 다른 폴더(바로 가기 실행 흉내)
$rc = Invoke-Dump $exe $other
Check ($rc -eq 0) ('A: 종료 코드 0 (' + $rc + ')')
if (Test-Path -LiteralPath $dump) {
    $raw = [IO.File]::ReadAllBytes($dump)
    Check (-not ($raw.Length -ge 3 -and $raw[0] -eq 0xEF -and $raw[1] -eq 0xBB -and $raw[2] -eq 0xBF)) 'A: 덤프 UTF-8 BOM 없음'
    $d = Read-Dump $dump
    Write-Output ('A 덤프: ' + (([IO.File]::ReadAllLines($dump, [Text.Encoding]::UTF8)) -join ' | '))
    Check ($d['edition'] -eq $Edition) ('A: edition=' + $d['edition'])
    Check ($d['ini'] -eq '없음') ('A: ini=' + $d['ini'])
    Check ($d['gamepadmouse'] -eq 'false 출처=기본값') ('A: gamepadmouse=' + $d['gamepadmouse'])
    Check ($d['detectnetblocking'] -eq 'false 출처=기본값') ('A: detectnetblocking=' + $d['detectnetblocking'])
    Check ($d['arbssilence'] -eq '4 출처=기본값') ('A: arbssilence=' + $d['arbssilence'] + ' (D-B4c-1 W 기본값 모드4)')
} else {
    Check $false 'A: 덤프 파일 생김'
}
$after = @(Get-ChildItem -LiteralPath $pkg -Recurse -Force | ForEach-Object { $_.FullName })
$new = @($after | Where-Object { $before -notcontains $_ })
Check ($new.Count -eq 1 -and $new[0] -ieq $dump) ('A: 새로 생긴 것은 덤프 1개뿐(INI·로그·캐시 없음) - ' + (@($new | ForEach-Object { Split-Path -Leaf $_ }) -join ','))
Check (@(Get-ChildItem -LiteralPath $other -Force).Count -eq 0) 'A: 다른 작업 폴더에 아무것도 안 생김'
Check (@(Get-Leftover $exe).Count -eq 0) 'A: 남은 프로세스 없음'

# ---- 경우 B: INI 에 gamepadmouse=true (대조군)
Remove-Item -LiteralPath $dump
New-Item -ItemType Directory -Path $iniDir | Out-Null
[IO.File]::WriteAllText($ini, "[General]`r`ngamepadmouse=true`r`n", (New-Object Text.UTF8Encoding $false))
$iniHash = (Get-FileHash -LiteralPath $ini -Algorithm SHA256).Hash
$rc = Invoke-Dump $exe $pkg
Check ($rc -eq 0) ('B: 종료 코드 0 (' + $rc + ')')
if (Test-Path -LiteralPath $dump) {
    $d = Read-Dump $dump
    Write-Output ('B 덤프: ' + (([IO.File]::ReadAllLines($dump, [Text.Encoding]::UTF8)) -join ' | '))
    Check ($d['ini'] -eq '있음') ('B: ini=' + $d['ini'])
    Check ($d['gamepadmouse'] -eq 'true 출처=파일') ('B: gamepadmouse=' + $d['gamepadmouse'])
    Check ($d['detectnetblocking'] -eq 'false 출처=기본값') ('B: detectnetblocking=' + $d['detectnetblocking'])
} else {
    Check $false 'B: 덤프 파일 생김'
}
Check ((Get-FileHash -LiteralPath $ini -Algorithm SHA256).Hash -eq $iniHash) 'B: INI 바뀌지 않음(save 없음)'
Check (@(Get-ChildItem -LiteralPath $pkg -Filter '*.log' -Force).Count -eq 0) 'B: 로그 파일 없음'
Check (@(Get-Leftover $exe).Count -eq 0) 'B: 남은 프로세스 없음'

# ---- 경우 B2 (R1b D-R1b-5): 옛 위치 INI 만 있음 -> 덤프 모드는 옛 INI 를 읽기만, 새 폴더를 만들지 않음
Remove-Item -LiteralPath $dump
Remove-Item -LiteralPath $iniDir -Recurse
New-Item -ItemType Directory -Path $oldIniDir | Out-Null
[IO.File]::WriteAllText($oldIni, "[General]`r`ngamepadmouse=true`r`n", (New-Object Text.UTF8Encoding $false))
$oldHash = (Get-FileHash -LiteralPath $oldIni -Algorithm SHA256).Hash
$rc = Invoke-Dump $exe $pkg
Check ($rc -eq 0) ('B2: 종료 코드 0 (' + $rc + ')')
if (Test-Path -LiteralPath $dump) {
    $d = Read-Dump $dump
    Check ($d['ini'] -eq '있음') ('B2: ini=' + $d['ini'])
    Check ($d['gamepadmouse'] -eq 'true 출처=파일') ('B2: gamepadmouse=' + $d['gamepadmouse'] + ' (옛 위치를 읽음)')
} else {
    Check $false 'B2: 덤프 파일 생김'
}
Check (-not (Test-Path -LiteralPath $iniDir)) 'B2: 새 설정 폴더 안 만듦(덤프는 이전하지 않음)'
Check ((Get-FileHash -LiteralPath $oldIni -Algorithm SHA256).Hash -eq $oldHash) 'B2: 옛 INI 바뀌지 않음'
Check (@(Get-Leftover $exe).Count -eq 0) 'B2: 남은 프로세스 없음'

# ---- 경우 C: portable.dat 없음 -> 종료 코드 3, 아무것도 읽지·쓰지 않음
Remove-Item -LiteralPath $dump
Remove-Item -LiteralPath (Join-Path $pkg 'portable.dat')
$rc = Invoke-Dump $exe $pkg
Check ($rc -eq 3) ('C: 종료 코드 3 (' + $rc + ')')
Check (-not (Test-Path -LiteralPath $dump)) 'C: 덤프 없음'
Check (@(Get-ChildItem -LiteralPath $pkg -Filter '*.log' -Force).Count -eq 0) 'C: 로그 파일 없음'
Check (@(Get-Leftover $exe).Count -eq 0) 'C: 남은 프로세스 없음'

Write-Output ''
Write-Output ('exe 기본값 시험: ok=' + $script:pass + ' fail=' + $script:fail)
if (-not $Keep) { Remove-Item -LiteralPath $S -Recurse -Force }
exit $(if ($script:fail -eq 0) { 0 } else { 1 })
