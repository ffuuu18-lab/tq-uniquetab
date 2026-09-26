# package.ps1 - builds the release zip from a built tree.
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-Version 1.0.0] [-Out out\]
# Stages exactly what a player installs and zips it: scripts\uniquetab.asi, the three documents and
# extras\ - the all-items collection file and its report (data\allitems\, the mod's own data: record
# paths and seeds written by tools\make_all_items.py, no text of the game's; README "Every item at once").
# The stage is UniqueCollectionTab-TQ-<version>\ and the zip is made from inside it, as GD's is, so
# the zip's root mirrors the folder that holds TQ.exe: unzip it there. Nothing generated or derived
# from the game ships. The plugin creates scripts\uniquetab\ itself and on the first launch
# generates catalogue.bin, the uniq-*.txt lists and the gray icons (gray\) there, and writes its
# own ini. data\oracle\ is the test harness's fixtures and never enters the zip.
param(
    [string]$Version = "",
    [string]$Out = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Version) {
    $hdr = Get-Content (Join-Path $root "src\ut_version.h") -Raw
    if ($hdr -match 'UT_VERSION\s+"([^"]+)"') { $Version = $Matches[1] } else { throw "UT_VERSION not found in src\ut_version.h" }
}
if (-not $Out) { $Out = Join-Path $root "out" }
if (-not (Test-Path $Out)) { New-Item -ItemType Directory -Force $Out | Out-Null }
$Out = (Resolve-Path $Out).Path
$asi = Join-Path $root "bin\uniquetab.asi"
if (-not (Test-Path $asi)) { throw "bin\uniquetab.asi is missing - run build.bat first" }

$name = "UniqueCollectionTab-TQ-$Version"
$stage = Join-Path $Out $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force "$stage\scripts" | Out-Null

Copy-Item $asi "$stage\scripts\uniquetab.asi"
foreach ($doc in "README.md", "LICENSE", "THIRD_PARTY.md") {
    $p = Join-Path $root $doc
    if (Test-Path $p) { Copy-Item $p "$stage\$doc" } else { throw "$doc is missing from the tree" }
}

New-Item -ItemType Directory -Force "$stage\extras" | Out-Null
$extras = @("tq-uniq-items-all.jsonl", "tq-uniq-items-all.report.txt")
foreach ($x in $extras) {
    $p = Join-Path $root "data\allitems\$x"
    if (-not (Test-Path $p)) { throw "data\allitems\$x is missing - run tools\regen_all_items.bat" }
    Copy-Item $p "$stage\extras\$x"
}

# What must never be in the zip: a generated output, a gray icon, a fixture, a journal, an ini -
# the two extras above aside, by name.
$bad = Get-ChildItem $stage -Recurse -File | Where-Object {
    ($_.FullName -notin ($extras | ForEach-Object { Join-Path "$stage\extras" $_ })) -and
    ($_.Extension -in ".tex", ".arz", ".arc", ".bin", ".txt", ".jsonl", ".csv", ".ini", ".stamp", ".log", ".pdb")
}
if ($bad) { throw ("derived files staged: " + (($bad | ForEach-Object { $_.Name }) -join ", ")) }

$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
# Written entry by entry instead of Compress-Archive, which (Windows PowerShell 5.1) stores
# "scripts\uniquetab.asi" with a backslash and stamps each entry with the file's LOCAL modification
# time. Here every name uses "/" as the zip format requires, and every entry carries the same fixed
# time (the zip epoch), so the package says nothing about when or in which time zone it was built.
Add-Type -AssemblyName System.IO.Compression
$epoch = New-Object DateTimeOffset(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
$fs = [IO.File]::Open($zip, [IO.FileMode]::CreateNew)
$za = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in (Get-ChildItem $stage -Recurse -File | Sort-Object FullName)) {
        $rel = $f.FullName.Substring($stage.Length + 1).Replace('\', '/')
        $e = $za.CreateEntry($rel, [IO.Compression.CompressionLevel]::Optimal)
        $e.LastWriteTime = $epoch
        $dst = $e.Open(); $src = [IO.File]::OpenRead($f.FullName)
        try { $src.CopyTo($dst) } finally { $src.Dispose(); $dst.Dispose() }
    }
} finally { $za.Dispose(); $fs.Dispose() }
$size = (Get-Item $zip).Length
Write-Host "[package] $zip ($size bytes)"
Get-ChildItem $stage -Recurse -File | ForEach-Object {
    Write-Host ("[package]   {0,-48} {1,9}" -f $_.FullName.Substring($stage.Length + 1), $_.Length)
}
