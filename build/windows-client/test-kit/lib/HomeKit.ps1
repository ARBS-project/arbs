# SPDX-License-Identifier: GPL-3.0-or-later
# ARBS-W 집 PC 시험 도구 (CC_TASK_B4b 5-2, docs/analysis/B4b.md 3-4). 더블클릭용 .cmd 가 이 파일을 부른다.
#   -Action Install  : 최신 arbs-w-*-portable.zip(+ .sha256) 확인 -> -Target 프로그램 파일만 교체
#                      (-Target 없으면 Resolve-KitTarget: 기존 설치 D:\arbs-w-test -> %LOCALAPPDATA%\ARBS-W-test
#                       -> 새 기기는 D:(고정 디스크)면 D:\arbs-w-test, 아니면 %LOCALAPPDATA%\ARBS-W-test. B4c D-B4c-5)
#                      -> 바탕화면 바로 가기 'ARBS-W 시험' 만들기/갱신. -FreshSettings(메뉴 2) = 설정 폴더 이름만 바꿔 보관.
#   -Action SendLogs : 오늘 ARBS-*.log 만 바탕화면\ARBS-보낼로그 로 복사(Moonlight 로그 제외) -> 탐색기로 열기.
#   -Action None     : 함수만 정의(시험용 dot-source).
# 바꾸는 것: -Target 폴더의 프로그램 파일, 바탕화면 바로 가기 1개, (새 설정으로) 설정 폴더 이름, 결과 txt.
# 바꾸지 않는 것: 설치·서비스·드라이버·레지스트리·방화벽, 다른 포터블 폴더, 설정 폴더·로그 삭제(하지 않음).
# 관리자 권한으로 실행하지 않는다(관리자면 중단). IP·PIN·설정 파일 내용은 화면·파일에 쓰지 않는다.
# R1b (D-R1b-7): -Channel Test(기본, 위 동작 그대로) / Stable(정식: D:\ARBS-W 또는 %LOCALAPPDATA%\ARBS-W, 바로 가기 'ARBS-W',
#   zip 은 arbs-w-<x.y.z>-windows-portable.zip 만 버전 높은 순, 시험 폴더는 읽기만). 배포 zip 에는 이 파일이 'ARBS-W 도구\lib\ArbsWKit.ps1' 로 들어간다.
[CmdletBinding()]
param(
    [ValidateSet('Install', 'SendLogs', 'None')][string]$Action = 'None',
    [ValidateSet('Test', 'Stable')][string]$Channel = 'Test',
    [string[]]$ImportFrom,  # 정식 첫 설치 때 설정을 읽어 올 시험 폴더(기본: D:\arbs-w-test, %LOCALAPPDATA%\ARBS-W-test)
    [string]$Target = '',   # '' = Resolve-KitTarget 로 자동(D-B4c-5)
    [string[]]$SearchDirs,
    [string]$DesktopDir,
    [string]$StagingRoot = $env:TEMP,
    [switch]$FreshSettings,
    [switch]$NoMenu,
    [switch]$DryRun,
    [switch]$NoPause
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

$script:SettingsDirName = 'Moonlight Game Streaming Project'   # b2* 까지의 설정 폴더
$script:NewSettingsDirName = 'ARBS'                            # 0.1.0 부터의 설정 폴더 (D-R1b-3)
$script:ToolDirName = 'ARBS-W 도구'
$script:AppExeName = 'ARBS-W.exe'                          # 0.1.0 부터 (R-02)
$script:LegacyExeName = 'Moonlight.exe'                       # b2* 까지
$script:SendDirName = 'ARBS-보낼로그'
$script:ResultName = '설치-결과.txt'
$script:Steps = New-Object System.Collections.ArrayList

# 채널별 이름·위치 (D-R1b-7)
function Set-KitChannel([string]$Name) {
    $script:KitChannel = $Name
    if ($Name -eq 'Stable') {
        $script:ShortcutName = 'ARBS-W.lnk'
        $script:ShortcutLabel = 'ARBS-W'
        $script:KitTitle = 'ARBS-W 설치·업데이트'
        $script:DefaultDTarget = 'D:\ARBS-W'
        $script:LocalLeaf = 'ARBS-W'
    } else {
        $script:ShortcutName = 'ARBS-W 시험.lnk'
        $script:ShortcutLabel = 'ARBS-W 시험'
        $script:KitTitle = 'ARBS-W 시험 설치'
        $script:DefaultDTarget = 'D:\arbs-w-test'
        $script:LocalLeaf = 'ARBS-W-test'
    }
}
Set-KitChannel $Channel

# ------------------------------------------------------------------------------------------------ 순수 함수
function Test-IsAdmin {
    $p = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# .sha256 한 줄: W 형식 "해시  이름", 호스트 형식 "해시 *이름", 해시만. 잘못되면 $null.
function Read-Sha256Text([string]$Text) {
    if ($null -eq $Text) { return $null }
    $line = ($Text -split "`r?`n" | Where-Object { $_.Trim() -ne '' } | Select-Object -First 1)
    if ($null -eq $line) { return $null }
    $m = [regex]::Match($line.Trim(), '^([0-9A-Fa-f]{64})(?:\s+\*?(.+))?$')
    if (-not $m.Success) { return $null }
    $name = $null
    if ($m.Groups[2].Success) { $name = $m.Groups[2].Value.Trim() }
    return [pscustomobject]@{ Hash = $m.Groups[1].Value.ToLowerInvariant(); Name = $name }
}

# 후보 폴더(없는 폴더는 건너뜀)에서 arbs-w-*-portable.zip 중 수정 시각이 가장 늦은 것. 없으면 $null.
function Select-LatestZip([string[]]$Dirs) {
    $all = @()
    foreach ($d in $Dirs) {
        if ([string]::IsNullOrWhiteSpace($d)) { continue }
        if (-not (Test-Path -LiteralPath $d -PathType Container)) { continue }
        $all += @(Get-ChildItem -LiteralPath $d -File -Filter 'arbs-w-*-portable.zip' -ErrorAction SilentlyContinue)
    }
    if ($all.Count -eq 0) { return $null }
    return ($all | Sort-Object LastWriteTimeUtc, Name -Descending | Select-Object -First 1)
}

# 정식 zip 이름의 버전. 규칙에 맞지 않으면(b2 시험판 포함) $null
function Get-StableZipVersion([string]$Name) {
    $m = [regex]::Match($Name, '^arbs-w-(\d{1,6})\.(\d{1,6})\.(\d{1,6})-windows-portable\.zip$')
    if (-not $m.Success) { return $null }
    return New-Object Version ([int]$m.Groups[1].Value), ([int]$m.Groups[2].Value), ([int]$m.Groups[3].Value)
}

# 정식 채널: 규칙에 맞는 zip 중 버전이 가장 높은 것(같으면 수정 시각이 늦은 것). 없으면 $null
function Select-StableZip([string[]]$Dirs) {
    $all = @()
    foreach ($d in $Dirs) {
        if ([string]::IsNullOrWhiteSpace($d)) { continue }
        if (-not (Test-Path -LiteralPath $d -PathType Container)) { continue }
        foreach ($f in @(Get-ChildItem -LiteralPath $d -File -Filter 'arbs-w-*-windows-portable.zip' -ErrorAction SilentlyContinue)) {
            $v = Get-StableZipVersion $f.Name
            if ($null -ne $v) { $all += [pscustomobject]@{ File = $f; Version = $v; Time = $f.LastWriteTimeUtc } }
        }
    }
    if ($all.Count -eq 0) { return $null }
    return ($all | Sort-Object Version, Time -Descending | Select-Object -First 1).File
}

# zip 과 같은 폴더의 <zip>.sha256 과 비교. 결과 {Ok; Msg}
function Test-ZipHash([string]$ZipPath) {
    $shaPath = $ZipPath + '.sha256'
    if (-not (Test-Path -LiteralPath $shaPath -PathType Leaf)) {
        return [pscustomobject]@{ Ok = $false; Msg = ('.sha256 파일 없음: ' + (Split-Path -Leaf $shaPath)) }
    }
    $exp = Read-Sha256Text ([IO.File]::ReadAllText($shaPath))
    if ($null -eq $exp) { return [pscustomobject]@{ Ok = $false; Msg = '.sha256 형식이 맞지 않음' } }
    $leaf = Split-Path -Leaf $ZipPath
    if ($null -ne $exp.Name -and $exp.Name -ne $leaf) {
        return [pscustomobject]@{ Ok = $false; Msg = ('.sha256 안의 이름(' + $exp.Name + ')이 zip 이름과 다름') }
    }
    $act = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($act -ne $exp.Hash) {
        return [pscustomobject]@{ Ok = $false; Msg = ('해시 불일치(파일이 덜 옮겨졌거나 바뀜) 기대 ' + $exp.Hash.Substring(0, 12) + '… 실제 ' + $act.Substring(0, 12) + '…') }
    }
    return [pscustomobject]@{ Ok = $true; Msg = ('해시 일치 ' + $act.Substring(0, 12) + '…') }
}

# 교체 대상에서 빼는 상대 경로: 설정 폴더(페어링), 로그, INI, 결과·덤프 파일, 캐시 폴더.
function Test-ProtectedRelPath([string]$Rel) {
    $r = $Rel.Replace('/', '\').TrimStart('\')
    $first = ($r -split '\\')[0]
    foreach ($n in @($script:SettingsDirName, $script:NewSettingsDirName)) {
        if ($first -ieq $n) { return $true }
        if ($first -like ($n + '.bak-*')) { return $true }
    }
    # 도구 폴더는 복사하지 않는다(실행 중인 .cmd 를 덮어쓰지 않도록)
    if ($first -ieq $script:ToolDirName) { return $true }
    if (@('boxart', 'cache', 'qmlcache') -icontains $first) { return $true }
    if ($r -notmatch '\\') {
        if ($r -like 'ARBS-*.log' -or $r -like 'Moonlight-*.log' -or $r -like '*.ini') { return $true }
        if ($r -ieq 'arbs-prefs-dump.txt' -or $r -ieq $script:ResultName) { return $true }
    }
    return $false
}

# 폴더 아래 파일의 상대 경로 목록
function Get-RelFileList([string]$Root) {
    $rootFull = (Resolve-Path -LiteralPath $Root).ProviderPath.TrimEnd('\')
    return @(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($rootFull.Length + 1) })
}

# 폴더의 실행 파일 이름: ARBS-W.exe 우선, 없으면 옛 Moonlight.exe, 둘 다 없으면 $null
function Get-AppExeName([string]$Dir) {
    foreach ($n in @($script:AppExeName, $script:LegacyExeName)) {
        if (Test-Path -LiteralPath (Join-Path $Dir $n) -PathType Leaf) { return $n }
    }
    return $null
}

# 풀린 폴더에서 실행 파일·portable.dat 가 있는 뿌리(바로 아래 또는 최상위 폴더 1개). 없으면 $null.
function Find-PackageRoot([string]$Staging) {
    if ((Get-AppExeName $Staging) -and (Test-Path -LiteralPath (Join-Path $Staging 'portable.dat'))) { return $Staging }
    $dirs = @(Get-ChildItem -LiteralPath $Staging -Directory)
    if ($dirs.Count -eq 1) {
        $d = $dirs[0].FullName
        if ((Get-AppExeName $d) -and (Test-Path -LiteralPath (Join-Path $d 'portable.dat'))) { return $d }
    }
    return $null
}

# 오늘(수정 날짜) ARBS-*.log (Target 바로 아래만)
function Get-TodayArbsLogs([string]$Dir, [datetime]$Today) {
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { return @() }
    return @(Get-ChildItem -LiteralPath $Dir -File -Filter 'ARBS-*.log' | Where-Object { $_.LastWriteTime.Date -eq $Today.Date } | Sort-Object Name)
}

function Get-DefaultSearchDirs([string]$KitDir) {
    $dl = Join-Path $env:USERPROFILE 'Downloads'
    return @($KitDir, $dl, (Join-Path $dl 'to-home'))
}

# 정식: zip 을 푼 폴더의 도구에서 실행하므로 도구 폴더의 위 두 단계(푼 폴더, zip 이 있는 폴더)도 찾는다
function Get-StableSearchDirs([string]$KitDir) {
    $unpacked = Split-Path -Parent $KitDir
    $beside = if ($unpacked) { Split-Path -Parent $unpacked } else { $null }
    return @(@($KitDir, $unpacked, $beside) + @(Get-DefaultSearchDirs $KitDir) | Where-Object { $_ } | Select-Object -Unique)
}

# 그 폴더가 실제로 쓰는 설정 파일: 새 위치가 있으면 그것, 없으면 옛 위치. 없으면 $null
function Get-ActiveSettingsIni([string]$Dir) {
    $new = Join-Path $Dir ($script:NewSettingsDirName + '\ARBS-W.ini')
    if (Test-Path -LiteralPath $new -PathType Leaf) { return [pscustomobject]@{ Path = $new; Rel = ($script:NewSettingsDirName + '\ARBS-W.ini') } }
    $old = Join-Path $Dir ($script:SettingsDirName + '\Moonlight.ini')
    if (Test-Path -LiteralPath $old -PathType Leaf) { return [pscustomobject]@{ Path = $old; Rel = ($script:SettingsDirName + '\Moonlight.ini') } }
    return $null
}

# 시험 폴더들 중 설정 파일 수정 시각이 가장 늦은 것. 대상 폴더 자신은 제외. 없으면 $null
function Select-ImportSource([string[]]$Dirs, [string]$Target) {
    $best = $null; $bestTime = [datetime]::MinValue
    foreach ($d in $Dirs) {
        if ([string]::IsNullOrWhiteSpace($d)) { continue }
        if ($Target -and ($d.TrimEnd('\') -ieq $Target.TrimEnd('\'))) { continue }
        if (-not (Test-Path -LiteralPath $d -PathType Container)) { continue }
        $ini = Get-ActiveSettingsIni $d
        if ($null -eq $ini) { continue }
        $t = (Get-Item -LiteralPath $ini.Path).LastWriteTimeUtc
        if ($null -eq $best -or $t -gt $bestTime) { $best = $ini; $bestTime = $t }
    }
    return $best
}

# 드라이브가 고정 디스크이고 준비됨(USB·광학·네트워크·없음 = $false)
function Test-FixedDriveReady([string]$Path) {
    try {
        $root = [IO.Path]::GetPathRoot($Path)
        if ([string]::IsNullOrEmpty($root)) { return $false }
        $di = New-Object IO.DriveInfo($root)
        return ($di.DriveType -eq [IO.DriveType]::Fixed -and $di.IsReady)
    } catch {
        return $false
    }
}

# B4c D-B4c-5: 설치·로그 보내기·바로 가기가 쓰는 폴더 하나를 고른다(순수 - 경로·드라이브 검사를 인자로 받음).
#   ① -Explicit 지정 -> 그대로  ② DTarget 에 실행 파일(ARBS-W.exe 또는 옛 Moonlight.exe) 있음 -> DTarget  ③ LocalTarget 에 있음 -> LocalTarget
#   ④ 설치 없음: -ExistingOnly(로그 보내기) -> Path=$null / 아니면 DTarget 드라이브가 고정 디스크면 DTarget, 아니면 LocalTarget
# 결과 {Path; Shown(화면용, 사용자 이름 없이 %LOCALAPPDATA%); Reason}
function Resolve-KitTarget {
    param(
        [string]$Explicit,
        [string]$LocalAppData,
        [string]$DTarget = 'D:\arbs-w-test',
        [string]$LocalLeaf = 'ARBS-W-test',
        [scriptblock]$IsFixedDrive = { param($p) Test-FixedDriveReady $p },
        [switch]$ExistingOnly
    )
    $localTarget = $null
    $localShown = '%LOCALAPPDATA%\' + $LocalLeaf
    if (-not [string]::IsNullOrWhiteSpace($LocalAppData)) { $localTarget = Join-Path $LocalAppData $LocalLeaf }
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        return [pscustomobject]@{ Path = $Explicit; Shown = $Explicit; Reason = '-Target 지정' }
    }
    if ($null -ne (Get-AppExeName $DTarget)) {
        return [pscustomobject]@{ Path = $DTarget; Shown = $DTarget; Reason = '기존 설치' }
    }
    if ($localTarget -and $null -ne (Get-AppExeName $localTarget)) {
        return [pscustomobject]@{ Path = $localTarget; Shown = $localShown; Reason = '기존 설치' }
    }
    if ($ExistingOnly) {
        return [pscustomobject]@{ Path = $null; Shown = ''; Reason = ('설치 폴더 없음(두 위치 확인: ' + $DTarget + ', ' + $localShown + ')') }
    }
    $dRoot = [IO.Path]::GetPathRoot($DTarget)
    if ([bool](& $IsFixedDrive $DTarget)) {
        return [pscustomobject]@{ Path = $DTarget; Shown = $DTarget; Reason = ('새 설치, ' + $dRoot.TrimEnd('\') + ' 고정 디스크') }
    }
    if (-not $localTarget) {
        return [pscustomobject]@{ Path = $null; Shown = ''; Reason = ($dRoot.TrimEnd('\') + ' 없음, LOCALAPPDATA 도 없음 - -Target 으로 지정하세요') }
    }
    return [pscustomobject]@{ Path = $localTarget; Shown = $localShown; Reason = ('새 설치, ' + $dRoot.TrimEnd('\') + ' 없음(또는 고정 디스크 아님) -> 자동') }
}

# ------------------------------------------------------------------------------------------------ 단계 기록
function Add-StepResult([string]$Name, [bool]$Ok, [string]$Msg) {
    [void]$script:Steps.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Msg = $Msg })
    $mark = if ($Ok) { 'O' } else { 'X' }
    $color = if ($Ok) { 'Green' } else { 'Red' }
    Write-Host ('[' + $mark + '] ' + $Name + ' - ' + $Msg) -ForegroundColor $color
}

# 스크립트 블록이 문자열(메시지)을 돌려주면 O, 예외면 X. 반환값 = 성공 여부
function Invoke-Step([string]$Name, [scriptblock]$Script) {
    try {
        $msg = & $Script
        Add-StepResult $Name $true ([string]($msg -join ' '))
        return $true
    } catch {
        Add-StepResult $Name $false $_.Exception.Message
        return $false
    }
}

function Format-Report([string]$Title) {
    $lines = @('==== ' + $Title + ' ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ====')
    foreach ($s in $script:Steps) {
        $mark = if ($s.Ok) { 'O' } else { 'X' }
        $lines += ('[' + $mark + '] ' + $s.Name + ' - ' + $s.Msg)
    }
    $bad = @($script:Steps | Where-Object { -not $_.Ok }).Count
    $lines += $(if ($bad -eq 0) { '결과: 모두 O' } else { '결과: X ' + $bad + '개 - 위 메시지를 확인' })
    return $lines
}

# ------------------------------------------------------------------------------------------------ 부작용 함수(얇게)
function Expand-ToStaging([string]$Zip, [string]$StagingDir) {
    if (Test-Path -LiteralPath $StagingDir) { Remove-Item -LiteralPath $StagingDir -Recurse -Force }
    New-Item -ItemType Directory -Path $StagingDir | Out-Null
    Expand-Archive -LiteralPath $Zip -DestinationPath $StagingDir -Force
}

# 풀린 파일을 Target 에 덮어쓰기(보호 경로 제외). 결과 {Copied; Skipped; Extra}
function Copy-ProgramFiles([string]$SrcRoot, [string]$Dest) {
    if (-not (Test-Path -LiteralPath $Dest)) { New-Item -ItemType Directory -Path $Dest | Out-Null }
    $src = @(Get-RelFileList $SrcRoot)
    $copied = 0; $skipped = 0
    foreach ($rel in $src) {
        if (Test-ProtectedRelPath $rel) { $skipped++; continue }
        $to = Join-Path $Dest $rel
        $toDir = Split-Path -Parent $to
        if (-not (Test-Path -LiteralPath $toDir)) { New-Item -ItemType Directory -Path $toDir | Out-Null }
        Copy-Item -LiteralPath (Join-Path $SrcRoot $rel) -Destination $to -Force
        $copied++
    }
    $srcSet = @{}
    foreach ($rel in $src) { $srcSet[$rel.ToLowerInvariant()] = $true }
    $extra = @(Get-RelFileList $Dest | Where-Object { -not (Test-ProtectedRelPath $_) -and -not $srcSet.ContainsKey($_.ToLowerInvariant()) })
    return [pscustomobject]@{ Copied = $copied; Skipped = $skipped; Extra = $extra }
}

# 설정 폴더 -> '<이름>.bak-yyyyMMdd-HHmmss' (이름만 바꿈, 삭제 안 함). 없으면 $null
function Move-SettingsAside([string]$Dir, [string]$Stamp, [string]$Name = $script:SettingsDirName) {
    $cur = Join-Path $Dir $Name
    if (-not (Test-Path -LiteralPath $cur -PathType Container)) { return $null }
    $newName = $Name + '.bak-' + $Stamp
    if (Test-Path -LiteralPath (Join-Path $Dir $newName)) { throw ('보관 이름이 이미 있음: ' + $newName) }
    Rename-Item -LiteralPath $cur -NewName $newName
    return $newName
}

# 옛·새 설정 폴더 둘 다 보관(하나만 옮기면 다음 실행에 남은 쪽이 되살아남). 먼저 이름 충돌을 확인한다. 결과 = 보관한 이름 목록
function Move-AllSettingsAside([string]$Dir, [string]$Stamp) {
    $names = @($script:SettingsDirName, $script:NewSettingsDirName)
    foreach ($n in $names) {
        if ((Test-Path -LiteralPath (Join-Path $Dir $n)) -and (Test-Path -LiteralPath (Join-Path $Dir ($n + '.bak-' + $Stamp)))) {
            throw ('보관 이름이 이미 있음: ' + $n + '.bak-' + $Stamp)
        }
    }
    $moved = @()
    foreach ($n in $names) {
        $r = Move-SettingsAside $Dir $Stamp $n
        if ($null -ne $r) { $moved += $r }
    }
    return $moved
}

# 정식 첫 설치: 시험 폴더의 설정 파일을 대상 폴더의 같은 상대 위치로 복사(시험 폴더는 읽기만, 덮어쓰기 없음).
# 옛 위치로 복사된 경우 앱 첫 실행 때 새 위치로 옮겨진다. 결과 = 화면 문구
function Import-TestSettings([string]$Target, [string[]]$Dirs) {
    if ($null -ne (Get-ActiveSettingsIni $Target)) { return '대상 폴더에 설정이 이미 있음(그대로 유지)' }
    if (Test-Path -LiteralPath $Target -PathType Container) {
        foreach ($n in @($script:SettingsDirName, $script:NewSettingsDirName)) {
            if (@(Get-ChildItem -LiteralPath $Target -Directory -Filter ($n + '.bak-*')).Count -gt 0) { return '새 설정으로 시작한 기록이 있어 가져오지 않음' }
        }
    }
    $src = Select-ImportSource $Dirs $Target
    if ($null -eq $src) { return '가져올 시험 설정 없음(새로 시작, PIN 페어링 필요)' }
    $to = Join-Path $Target $src.Rel
    $toDir = Split-Path -Parent $to
    if (-not (Test-Path -LiteralPath $toDir)) { New-Item -ItemType Directory -Path $toDir -Force | Out-Null }
    $tmp = $to + '.importing'
    try {
        [IO.File]::Copy($src.Path, $tmp, $true)
        if ((Get-FileHash -LiteralPath $src.Path -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $tmp -Algorithm SHA256).Hash) { throw '복사본 내용이 원본과 다름' }
        [IO.File]::Move($tmp, $to)
    } finally {
        if ([IO.File]::Exists($tmp)) { [IO.File]::Delete($tmp) }
    }
    return '시험 폴더의 설정·페어링을 복사함(시험 폴더는 그대로)'
}

function New-TestShortcut([string]$Desktop, [string]$Dir, [string]$ExeName = '') {
    if (-not (Test-Path -LiteralPath $Desktop -PathType Container)) { throw ('바탕화면 폴더 없음') }
    if (-not $ExeName) { $ExeName = Get-AppExeName $Dir }
    if (-not $ExeName) { $ExeName = $script:AppExeName }
    $lnk = Join-Path $Desktop $script:ShortcutName
    $sh = New-Object -ComObject WScript.Shell
    try {
        $s = $sh.CreateShortcut($lnk)
        $s.TargetPath = (Join-Path $Dir $ExeName)
        $s.WorkingDirectory = $Dir
        $s.Description = ($script:ShortcutLabel + ' (포터블)')
        $s.Save()
    } finally {
        [void][Runtime.InteropServices.Marshal]::ReleaseComObject($sh)
    }
    return $lnk
}

function Get-TargetMoonlightProcess([string]$Dir) {
    $exes = @((Join-Path $Dir $script:AppExeName), (Join-Path $Dir $script:LegacyExeName))
    return @(Get-Process -Name 'ARBS-W', 'Moonlight' -ErrorAction SilentlyContinue | Where-Object {
            $p = $null
            try { $p = $_.Path } catch { }
            $p -and ($exes -icontains $p)
        })
}

# R-02: 같은 폴더 업데이트 뒤 그 폴더의 옛 Moonlight.exe 한 파일만 지운다(하위 폴더·설정·페어링·로그는 건드리지 않음).
# 새 패키지가 ARBS-W.exe 이고, 새 파일이 실제로 있고, 업데이트 전에 포터블 설치(portable.dat)였고, 실행 중이 아닐 때만.
function Remove-LegacyExe([string]$Dir, [string]$PackageExe, [bool]$WasPortable) {
    if ($PackageExe -ne $script:AppExeName) { return '해당 없음(옛 이름 패키지)' }
    $old = Join-Path $Dir $script:LegacyExeName
    if (-not (Test-Path -LiteralPath $old -PathType Leaf)) { return '옛 Moonlight.exe 없음' }
    if (-not $WasPortable) { return 'portable.dat 이 없던 폴더라 옛 Moonlight.exe 를 지우지 않음' }
    if (-not (Test-Path -LiteralPath (Join-Path $Dir $script:AppExeName) -PathType Leaf)) { throw 'ARBS-W.exe 가 없어 옛 Moonlight.exe 를 지우지 않음' }
    if (@(Get-TargetMoonlightProcess $Dir).Count -gt 0) { throw '대상 폴더의 프로그램이 실행 중 - 옛 Moonlight.exe 를 지우지 않음' }
    [IO.File]::Delete($old)
    if (Test-Path -LiteralPath $old) { throw '옛 Moonlight.exe 를 지우지 못함' }
    return '옛 Moonlight.exe 지움(설정·페어링 유지)'
}

function Open-Folder([string]$Dir) {
    if ($DryRun) { return 'DRY: 탐색기 열기 생략' }
    Start-Process -FilePath 'explorer.exe' -ArgumentList ('"' + $Dir + '"')
    return '탐색기로 열었음'
}

function Write-ResultFile([string]$Path, [string[]]$Lines) {
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) { return }
    Add-Content -LiteralPath $Path -Value ($Lines + '') -Encoding UTF8
}

function Wait-Close {
    if ($NoPause -or $DryRun) { return }
    Write-Host ''
    [void](Read-Host '창을 닫으려면 Enter')
}

# ------------------------------------------------------------------------------------------------ 동작
# '설치 위치' 결과 줄(D-B4c-5). 폴더를 못 고르면 X
function Get-TargetStepMsg {
    if (-not $script:TargetInfo -or -not $script:TargetInfo.Path) { throw ([string]$script:TargetInfo.Reason) }
    ($script:TargetInfo.Shown + ' (' + $script:TargetInfo.Reason + ')')
}

function Invoke-Install {
    $kitDir = Split-Path -Parent $PSScriptRoot
    $dirs = if ($SearchDirs) { $SearchDirs } elseif ($script:KitChannel -eq 'Stable') { Get-StableSearchDirs $kitDir } else { Get-DefaultSearchDirs $kitDir }
    $desk = if ($DesktopDir) { $DesktopDir } else { [Environment]::GetFolderPath('Desktop') }
    $fresh = [bool]$FreshSettings
    Write-Host ($script:KitTitle + ' - 대상 폴더 ' + $script:TargetShown) -ForegroundColor Cyan

    if (-not $fresh -and -not $NoMenu -and -not $DryRun) {
        Write-Host '  1 = 설치 (설정·페어링 유지)  <- Enter'
        Write-Host '  2 = 새 설정으로 설치 (기존 설정 폴더는 날짜 이름으로 보관, 호스트와 PIN 페어링을 다시 해야 함)'
        $c = Read-Host '번호'
        if ($c -eq '2') {
            $y = Read-Host '설정 폴더를 보관하고 새로 시작합니다. 계속할까요? (Y/N)'
            if ($y -match '^[Yy]') { $fresh = $true } else { Write-Host '1번(설정 유지)으로 진행합니다.' }
        }
    }

    $ok = Invoke-Step '관리자 아님 확인' {
        if (Test-IsAdmin) { throw '관리자 권한으로 실행됨 - 창을 닫고 파일을 그냥 더블클릭하세요' }
        '일반 권한'
    }
    if ($ok) { $ok = Invoke-Step '설치 위치' { Get-TargetStepMsg } }
    $zip = $null
    if ($ok) {
        $ok = Invoke-Step 'zip 찾기' {
            if ($script:KitChannel -eq 'Stable') {
                $script:zipFile = Select-StableZip $dirs
                if ($null -eq $script:zipFile) { throw 'arbs-w-<버전>-windows-portable.zip 을 찾지 못함(압축을 푼 폴더 옆 또는 다운로드 폴더에 두세요)' }
            } else {
                $script:zipFile = Select-LatestZip $dirs
                if ($null -eq $script:zipFile) { throw 'arbs-w-*-portable.zip 을 찾지 못함(이 폴더 또는 다운로드 폴더에 두세요)' }
            }
            $script:zipFile.Name
        }
        if ($ok) { $zip = $script:zipFile.FullName }
    }
    if ($ok) {
        $ok = Invoke-Step 'SHA-256 확인' {
            $r = Test-ZipHash $zip
            if (-not $r.Ok) { throw $r.Msg }
            $r.Msg
        }
    }
    if ($ok) {
        $ok = Invoke-Step ($script:ShortcutLabel + ' 창 닫힘 확인') {
            if (@(Get-TargetMoonlightProcess $Target).Count -gt 0) { throw '대상 폴더의 ARBS-W(또는 옛 Moonlight.exe)가 실행 중 - 닫고 다시 실행하세요' }
            '실행 중 아님'
        }
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $staging = Join-Path $StagingRoot ('arbs-w-staging-' + $stamp)
    $pkgRoot = $null
    if ($ok) {
        $ok = Invoke-Step 'zip 풀기(임시 폴더)' {
            Expand-ToStaging $zip $staging
            $script:pkgRoot = Find-PackageRoot $staging
            if ($null -eq $script:pkgRoot) { throw 'zip 안에 실행 파일·portable.dat 가 없음' }
            $script:pkgExe = Get-AppExeName $script:pkgRoot
            if ($script:KitChannel -eq 'Stable' -and $script:pkgExe -ne $script:AppExeName) { throw '정식 zip 안에 ARBS-W.exe 가 없음' }
            ($script:pkgExe + '·portable.dat 확인')
        }
        if ($ok) { $pkgRoot = $script:pkgRoot }
    }
    if ($ok -and $fresh) {
        $ok = Invoke-Step '새 설정으로(설정 폴더 보관)' {
            $n = @(Move-AllSettingsAside $Target $stamp)
            if ($n.Count -eq 0) { '설정 폴더가 원래 없음(새로 시작)' } else { ('보관: ' + ($n -join ', ') + ' (삭제 안 함, PIN 페어링 다시 필요)') }
        }
    }
    # 프로그램 교체 전에: 실패하면 멈춘다(그냥 진행하면 새 신원이 생겨 페어링을 옮길 기회를 잃음)
    if ($ok -and -not $fresh -and $script:KitChannel -eq 'Stable') {
        $ok = Invoke-Step '시험판 설정 가져오기' {
            $from = if ($PSBoundParameters.ContainsKey('ImportFrom') -or $ImportFrom) { $ImportFrom } else { @('D:\arbs-w-test', $(if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'ARBS-W-test' })) }
            Import-TestSettings $Target $from
        }
    }
    $wasPortable = [bool]($Target -and (Test-Path -LiteralPath (Join-Path $Target 'portable.dat') -PathType Leaf))
    if ($ok) {
        $ok = Invoke-Step '프로그램 파일 교체' {
            $r = Copy-ProgramFiles $pkgRoot $Target
            $extra = @($r.Extra | Where-Object { -not ($script:pkgExe -eq $script:AppExeName -and $_ -ieq $script:LegacyExeName) })
            $m = ('파일 ' + $r.Copied + '개 복사, 설정·로그 유지')
            if ($extra.Count -gt 0) { $m += (', zip 에 없는 옛 파일 ' + $extra.Count + '개(그대로 둠)') }
            $m
        }
    }
    if ($ok) {
        # 실패해도 새 실행 파일은 이미 들어갔으므로 바로 가기는 만들고, 결과에 X 로 남긴다
        [void](Invoke-Step '옛 실행 파일 정리' { Remove-LegacyExe $Target $script:pkgExe $wasPortable })
    }
    if ($ok) {
        [void](Invoke-Step ("바탕화면 바로 가기 '" + $script:ShortcutLabel + "'") {
                [void](New-TestShortcut $desk $Target $script:pkgExe)
                '만들기/갱신'
            })
    }
    if (Test-Path -LiteralPath $staging) {
        try { Remove-Item -LiteralPath $staging -Recurse -Force } catch { Write-Host ('임시 폴더 정리 실패: ' + $staging) }
    }
    $lines = Format-Report ($script:KitTitle + $(if ($DryRun) { ' (DRY)' } else { '' }))
    # B5 D-B5-6: 설치 위치를 결과 맨 끝에 한 번 더(화면·결과 파일 둘 다). Format-Report 는 로그 보내기와 공용이라 그대로
    if ($script:TargetInfo -and $script:TargetInfo.Path) { $lines = @($lines) + ('설치 위치: ' + $script:TargetShown) }
    Write-Host ''
    $lines | ForEach-Object { Write-Host $_ }
    if ($Target) { Write-ResultFile (Join-Path $Target $script:ResultName) $lines }
}

function Invoke-SendLogs {
    $desk = if ($DesktopDir) { $DesktopDir } else { [Environment]::GetFolderPath('Desktop') }
    $dest = Join-Path $desk $script:SendDirName
    Write-Host ('ARBS-W 로그 보내기 - ' + $script:TargetShown + ' 의 오늘 ARBS-*.log') -ForegroundColor Cyan
    $files = @()
    $ok = Invoke-Step '설치 위치' { Get-TargetStepMsg }
    if ($ok) { $ok = Invoke-Step '오늘 ARBS 로그 찾기' {
        $script:logFiles = @(Get-TodayArbsLogs $Target (Get-Date))
        if ($script:logFiles.Count -eq 0) { throw ('오늘 날짜 ARBS-*.log 없음(' + $script:TargetShown + ')') }
        ('' + $script:logFiles.Count + '개')
    } }
    if ($ok) { $files = $script:logFiles }
    if ($ok) {
        $ok = Invoke-Step "바탕화면 '$($script:SendDirName)' 로 복사" {
            if (-not (Test-Path -LiteralPath $dest)) { New-Item -ItemType Directory -Path $dest | Out-Null }
            foreach ($f in $files) { Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $dest $f.Name) -Force }
            ('' + $files.Count + '개 복사(Moonlight 로그 제외)')
        }
    }
    if ($ok) { [void](Invoke-Step '폴더 열기' { Open-Folder $dest }) }
    Write-Host ''
    Format-Report 'ARBS-W 로그 보내기' | ForEach-Object { Write-Host $_ }
}

# ------------------------------------------------------------------------------------------------ 진입점
if ($Action -eq 'None') { return }
# -DryRun = 시험용: 실제 집 PC 경로(D:\arbs-w-test, 바탕화면, 다운로드)를 쓰지 않도록 세 경로를 모두 직접 받아야 함
if ($DryRun -and -not ($PSBoundParameters.ContainsKey('Target') -and $PSBoundParameters.ContainsKey('DesktopDir') -and
        ($Action -eq 'SendLogs' -or ($PSBoundParameters.ContainsKey('SearchDirs') -and $PSBoundParameters.ContainsKey('StagingRoot'))))) {
    Write-Host '-DryRun 에는 -Target, -DesktopDir (설치는 + -SearchDirs, -StagingRoot) 를 모두 지정해야 함' -ForegroundColor Red
    exit 2
}
if ($DryRun -and $Channel -eq 'Stable' -and $Action -eq 'Install' -and -not $PSBoundParameters.ContainsKey('ImportFrom')) {
    Write-Host '-DryRun 정식 설치에는 -ImportFrom 도 지정해야 함' -ForegroundColor Red
    exit 2
}
# D-B4c-5: 설치·로그 보내기·바로 가기가 같은 폴더를 쓴다(로그 보내기는 기존 설치만)
$script:TargetInfo = Resolve-KitTarget -Explicit $Target -LocalAppData $env:LOCALAPPDATA -DTarget $script:DefaultDTarget -LocalLeaf $script:LocalLeaf -ExistingOnly:($Action -eq 'SendLogs')
$Target = [string]$script:TargetInfo.Path
$script:TargetShown = $(if ($script:TargetInfo.Path) { $script:TargetInfo.Shown } else { '(설치 폴더 없음)' })
$code = 0
try {
    if ($Action -eq 'Install') { Invoke-Install } else { Invoke-SendLogs }
    if (@($script:Steps | Where-Object { -not $_.Ok }).Count -gt 0) { $code = 1 }
} catch {
    Write-Host ('예상하지 못한 오류: ' + $_.Exception.Message) -ForegroundColor Red
    $code = 2
}
if (-not ($NoPause -or $DryRun)) {
    Wait-Close
    exit 0   # 사용자가 결과를 본 뒤 -> .cmd 가 다시 멈추지 않게 0
}
exit $code
