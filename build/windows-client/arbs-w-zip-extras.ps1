# SPDX-License-Identifier: GPL-3.0-or-later
# ARBS-W R1b (D-R1b-7, D-R1b-9): adds LICENSE.txt, NOTICE-ARBS.md, README-ARBS-W.txt and the user tools folder to a
# portable zip, then checks that every entry is really there. Reads the repository only; writes the zip and a work
# folder under client-w\build-arbs-w-zip. No install, service, registry or network use.
# Usage: arbs-w-zip-extras.ps1 -Zip <zip> -Version 0.1.0 -BaseVersion 6.2.0 [-SevenZip <7z.exe>]
param(
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$BaseVersion,
    [string]$SevenZip = '7z'
)
$ErrorActionPreference = 'Stop'

$clientW = Split-Path -Parent $PSScriptRoot
$repo = Join-Path $clientW 'arbs-w'
$kit = Join-Path $clientW 'test-kit'
$work = Join-Path $clientW 'build-arbs-w-zip'
$toolDir = 'ARBS-W ' + [string][char]0xB3C4 + [char]0xAD6C      # "ARBS-W 도구"
$installCmd = 'ARBS-W ' + [char]0xC124 + [char]0xCE58 + [char]0x00B7 + [char]0xC5C5 + [char]0xB370 + [char]0xC774 + [char]0xD2B8 + '.cmd'
$logsCmd = 'ARBS-W ' + [char]0xB85C + [char]0xADF8 + ' ' + [char]0xBCF4 + [char]0xB0B4 + [char]0xAE30 + '.cmd'
$manifestName = 'package-files.sha256'

if (-not (Test-Path -LiteralPath $Zip -PathType Leaf)) { throw ('zip not found: ' + $Zip) }
if ($Version -notmatch '^\d{1,6}\.\d{1,6}\.\d{1,6}$') { throw ('version must be x.y.z: ' + $Version) }
$commit = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw 'cannot read the arbs-w commit' }
$dirty = @(& git -C $repo status --porcelain --untracked-files=no --ignore-submodules=dirty -- . ':!build').Count -gt 0
$commitText = if ($dirty) { $commit + ' + uncommitted local changes (test build)' } else { $commit }

if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $work ($toolDir + '\lib')) -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $work 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $repo 'NOTICE-ARBS.md') -Destination (Join-Path $work 'NOTICE-ARBS.md')
Copy-Item -LiteralPath (Join-Path $kit ('stable\' + $installCmd)) -Destination (Join-Path $work ($toolDir + '\' + $installCmd))
Copy-Item -LiteralPath (Join-Path $kit ('stable\' + $logsCmd)) -Destination (Join-Path $work ($toolDir + '\' + $logsCmd))
Copy-Item -LiteralPath (Join-Path $kit 'lib\HomeKit.ps1') -Destination (Join-Path $work ($toolDir + '\lib\ArbsWKit.ps1'))

$readme = @(
    "ARBS-W $Version (based on Moonlight-qt $BaseVersion)",
    '',
    'Unofficial project. Not affiliated with or endorsed by Flydigi or the Moonlight project.',
    'License: GPL-3.0 (LICENSE.txt). Third-party and fork notices: NOTICE-ARBS.md.',
    'Source: https://github.com/arbs-project/arbs-w',
    "Commit: $commitText",
    'moonlight-common-c: public commit 6f05ee89 + patches/moonlight-common-c-6f05ee89-arbs.patch in the arbs-w repository.',
    '',
    'Install or update: unzip, then run the install tool in the "ARBS-W tools" folder (the Korean-named folder',
    'next to this file). It does not need administrator rights. If this zip and its .sha256 are nearby, they are checked too.',
    'This build is not code-signed, so Windows may show a warning. Check the SHA-256 value first.',
    'The tool does not change or remove an earlier test build (ARBS-W test) folder or shortcut (on the first install it only reads its settings).',
    '',
    '----',
    "ARBS-W $Version (Moonlight-qt $BaseVersion 기반)",
    '',
    '비공식 프로젝트이며 Flydigi·Moonlight 프로젝트와 관련이 없습니다.',
    '라이선스: GPL-3.0 (LICENSE.txt). 제3자·포크 고지: NOTICE-ARBS.md.',
    '소스: https://github.com/arbs-project/arbs-w',
    "커밋: $commitText",
    'moonlight-common-c: 공개 커밋 6f05ee89 + arbs-w 저장소의 patches/moonlight-common-c-6f05ee89-arbs.patch.',
    '',
    '설치·업데이트: 압축을 푼 뒤 ''ARBS-W 도구'' 폴더의 ''ARBS-W 설치·업데이트.cmd'' 를 더블클릭합니다(관리자 권한 불필요).',
    '이 zip 과 .sha256 이 가까이 있으면 함께 확인합니다.',
    '코드 서명이 없어 Windows 경고가 나올 수 있습니다. 먼저 SHA-256 값을 확인하세요.',
    '이전 시험판(ARBS-W 시험) 폴더·바로 가기는 이 도구가 바꾸거나 지우지 않습니다(첫 설치 때 설정만 읽어 옴).'
)
[IO.File]::WriteAllText((Join-Path $work 'README-ARBS-W.txt'), (($readme -join "`r`n") + "`r`n"), (New-Object Text.UTF8Encoding $true))

$zipFull = (Resolve-Path -LiteralPath $Zip).ProviderPath
Push-Location -LiteralPath $work
try {
    & $SevenZip a -tzip -mcu=on $zipFull '*' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw ('7z failed: ' + $LASTEXITCODE) }
} finally {
    Pop-Location
}

Add-Type -AssemblyName System.IO.Compression.FileSystem

# R1b-fix1 G2: file list with hashes inside the package, so the unpacked folder can be the install source.
function Get-ZipFileHashes([string]$Path) {
    $map = New-Object 'System.Collections.Generic.SortedDictionary[string,string]' ([StringComparer]::Ordinal)
    $a = [IO.Compression.ZipFile]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($e in $a.Entries) {
            $n = $e.FullName.Replace('\', '/')
            if ($n.EndsWith('/')) { continue }
            $s = $e.Open()
            try { $map[$n] = -join ($sha.ComputeHash($s) | ForEach-Object { $_.ToString('x2') }) } finally { $s.Dispose() }
        }
    } finally {
        $sha.Dispose()
        $a.Dispose()
    }
    return , $map
}
$manifestRel = $toolDir + '/' + $manifestName
$hashes = Get-ZipFileHashes $zipFull
if ($hashes.ContainsKey($manifestRel)) { throw ('input zip already has ' + $manifestRel) }
$manifestLines = New-Object System.Collections.Generic.List[string]
$manifestLines.Add('# arbs-w-package 1 ' + $Version)
foreach ($kv in $hashes.GetEnumerator()) { $manifestLines.Add($kv.Value + '  ' + $kv.Key) }
[IO.File]::WriteAllText((Join-Path $work ($toolDir + '\' + $manifestName)), (($manifestLines -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
Push-Location -LiteralPath $work
try {
    & $SevenZip a -tzip -mcu=on $zipFull ($toolDir + '\' + $manifestName) | Out-Null
    if ($LASTEXITCODE -ne 0) { throw ('7z failed: ' + $LASTEXITCODE) }
} finally {
    Pop-Location
}
$after = Get-ZipFileHashes $zipFull
if (-not $after.Remove($manifestRel)) { throw ('missing in zip: ' + $manifestRel) }
if ($after.Count -ne $hashes.Count) { throw ('package list count differs: ' + $hashes.Count + ' listed, ' + $after.Count + ' in zip') }
foreach ($kv in $hashes.GetEnumerator()) {
    if (-not $after.ContainsKey($kv.Key) -or $after[$kv.Key] -cne $kv.Value) { throw ('package list differs from zip: ' + $kv.Key) }
}

$names = @{}
$archive = [IO.Compression.ZipFile]::OpenRead($zipFull)
try {
    foreach ($e in $archive.Entries) { $names[$e.FullName.Replace('\', '/')] = $true }
} finally {
    $archive.Dispose()
}
$expected = @(
    'LICENSE.txt', 'NOTICE-ARBS.md', 'README-ARBS-W.txt',
    ($toolDir + '/' + $installCmd), ($toolDir + '/' + $logsCmd), ($toolDir + '/lib/ArbsWKit.ps1'), $manifestRel,
    'ARBS-W.exe', 'portable.dat'
)
$missing = @($expected | Where-Object { -not $names.ContainsKey($_) })
if ($missing.Count -gt 0) { throw ('missing in zip: ' + ($missing -join ', ')) }
if ($names.ContainsKey('Moonlight.exe')) { throw 'old executable name found in zip: Moonlight.exe' }
Write-Output ('[arbs-w-zip-extras] entries checked: ' + $expected.Count + ', total entries: ' + $names.Count + ', package list files: ' + $hashes.Count)
exit 0
