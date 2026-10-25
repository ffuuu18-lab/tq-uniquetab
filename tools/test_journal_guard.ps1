# test_journal_guard.ps1 - the uninstall guard (tools\journal_guard.ps1) and undeploy.bat's purge,
# offline. NO GAME NEEDED: every folder is made here, under build\test\journalguard\, and removed
# again at the next run. Nothing outside that folder is read or written.
#
#     powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_journal_guard.ps1   (exit 0 = pass)
#
# The guard cases: a journal with one row, a pending-out row, a line that is not a TQ-1 header, a
# file held open, a hidden file, each under mods\x\ -> REFUSE; a copy named tq-uniq-items123.jsonl
# beside the main journal with a row -> REFUSE; -OutDir wins over %UNIQUETAB_OUT%; every journal
# empty (the .bad- copies and other names ignored) -> allowed; no journal, no folder -> allowed.
# journal_dir in the mod folder's uniquetab.ini: a row in that folder's mods\x\, or one left in the
# mod folder -> REFUSE; both empty -> allowed; a folder that does not exist or a commented line ->
# the mod folder is judged; --purge with a row under journal_dir -> refused.
# The purge cases: a fake game folder (an empty TQ.exe, an empty uniquetab.asi) whose mod folder
# holds the generated set, the player's files and mods\x\: --purge deletes the generated files by
# name and leaves every player's file, every unknown file and mods\; refused, it deletes nothing
# in the mod folder.
$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$guard = Join-Path $repo "tools\journal_guard.ps1"
$undeploy = Join-Path $repo "undeploy.bat"
$base = Join-Path $repo "build\test\journalguard"
$env:UNIQUETAB_OUT = $null

$script:fails = 0
$script:cases = 0
function Check([bool]$ok, [string]$what) {
    $script:cases++
    if ($ok) { Write-Host "  ok   $what" } else { Write-Host "  FAIL $what"; $script:fails++ }
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
function Put([string]$path, [string]$text) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    [System.IO.File]::WriteAllText($path, $text, $utf8)
}
$header = '{"journal":"titan quest uniquetab","format":1,"set":"x","entries":0}'
$row = '{"record":"records/item/equipmentring/us_l_ringtest.dbr","seed":12345}'
$rowOut = '{"record":"records/item/equipmentring/us_l_ringtest.dbr","seed":777,"pending":"out"}'
function Journal([string]$path, [string[]]$rows) {
    $lines = @($header) + $rows
    Put $path (($lines -join "`n") + "`n")
}

# Runs the guard in its own process, as undeploy.bat does, and returns its exit code.
function Guard([string]$dir) {
    $o = & powershell -NoProfile -ExecutionPolicy Bypass -File $guard -OutDir $dir 2>&1
    $rc = $LASTEXITCODE
    $o | ForEach-Object { Write-Host "       | $_" }
    return $rc
}

# A fresh folder per case.
function Fresh([string]$name) {
    $d = Join-Path $base $name
    if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
    New-Item -ItemType Directory -Path $d -Force | Out-Null
    return $d
}
if (Test-Path -LiteralPath $base) { Remove-Item -LiteralPath $base -Recurse -Force }

# A clean mod folder: every journal holds its header only; aside copies with rows are history.
function CleanSet([string]$d) {
    Journal (Join-Path $d "tq-uniq-items.jsonl") @()
    Journal (Join-Path $d "tq-uniq-items-custom.jsonl") @()
    Journal (Join-Path $d "tq-uniq-items123.jsonl") @()
    Journal (Join-Path $d "mods\x\tq-uniq-items-x.jsonl") @()
    Journal (Join-Path $d "mods\y-1a2b3c4d\tq-uniq-items-y-1a2b3c4d.jsonl") @()
    Journal (Join-Path $d "tq-uniq-items.jsonl.bad-x") @($row)
    Journal (Join-Path $d "mods\x\tq-uniq-items-x.jsonl.bad-x") @($row)
    Journal (Join-Path $d "mods\x\tq-uniq-items-x.jsonl.tmp") @($row)
    Put (Join-Path $d "tq-uniq-items.csv") "record,seed`n"
}

Write-Host "[test] the guard"
$d = Fresh "clean"; CleanSet $d
Check ((Guard $d) -eq 0) "every journal empty, mods\ included, aside copies ignored -> allowed"

$d = Fresh "row"; CleanSet $d
Journal (Join-Path $d "mods\x\tq-uniq-items-x.jsonl") @($row)
Check ((Guard $d) -eq 1) "a mods\x\ journal with one row -> refused"

$d = Fresh "pending"; CleanSet $d
Journal (Join-Path $d "mods\y-1a2b3c4d\tq-uniq-items-y-1a2b3c4d.jsonl") @($rowOut)
Check ((Guard $d) -eq 1) "a mods\ journal with a pending-out row -> refused"

$d = Fresh "badheader"; CleanSet $d
Put (Join-Path $d "mods\x\tq-uniq-items-x.jsonl") "this is not a journal`n"
Check ((Guard $d) -eq 1) "a mods\x\ journal that is not TQ-1 -> refused"

$d = Fresh "badline"; CleanSet $d
Put (Join-Path $d "mods\x\tq-uniq-items-x.jsonl") ($header + "`n{broken`n")
Check ((Guard $d) -eq 1) "a mods\x\ journal with a line that does not parse -> refused"

$d = Fresh "locked"; CleanSet $d
$p = Join-Path $d "mods\x\tq-uniq-items-x.jsonl"
$h = [System.IO.File]::Open($p, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
try { Check ((Guard $d) -eq 1) "a mods\x\ journal held open by another program -> refused" } finally { $h.Close() }

$d = Fresh "hidden"; CleanSet $d
$p = Join-Path $d "mods\x\tq-uniq-items-x.jsonl"
Journal $p @($row)
(Get-Item -LiteralPath $p).Attributes = "Hidden"
Check ((Guard $d) -eq 1) "a hidden mods\x\ journal with a row -> refused"

$d = Fresh "stray"; CleanSet $d
Journal (Join-Path $d "tq-uniq-items123.jsonl") @($row, $row)
Check ((Guard $d) -eq 1) "tq-uniq-items123.jsonl beside the main journal with rows -> refused"

$d = Fresh "straymod"; CleanSet $d
Journal (Join-Path $d "mods\x\tq-uniq-items-x copy.jsonl") @($row)
Check ((Guard $d) -eq 1) "a copy named 'tq-uniq-items-x copy.jsonl' in mods\x\ with a row -> refused"

$d = Fresh "main"; CleanSet $d
Journal (Join-Path $d "tq-uniq-items.jsonl") @($row)
Check ((Guard $d) -eq 1) "the main journal with a row -> refused"

$d = Fresh "nojournal"
Put (Join-Path $d "uniquetab.ini") "[general]`n"
New-Item -ItemType Directory -Path (Join-Path $d "mods\x") -Force | Out-Null
Check ((Guard $d) -eq 0) "no journal anywhere -> allowed"
Check ((Guard (Join-Path $base "absent")) -eq 0) "no mod folder -> allowed"

$d = Fresh "envorder"; CleanSet $d
Journal (Join-Path $d "mods\x\tq-uniq-items-x.jsonl") @($row)
$env:UNIQUETAB_OUT = (Join-Path $base "clean")
Check ((Guard $d) -eq 1) "-OutDir wins over %UNIQUETAB_OUT% -> the folder given is the one judged"
$env:UNIQUETAB_OUT = $null

# journal_dir (GD's key): the ini in the mod folder names where the journals are
function JournalDirIni([string]$d, [string]$jd) {
    Put (Join-Path $d "uniquetab.ini") ("ini_version=23`r`n[general]`r`n[advanced]`r`n;mp_collect=1`t`t; mp`r`n" +
        "journal_dir=`"$jd\`"`t`t; where the collection file is kept`r`n")
}
$d = Fresh "jdrow"; CleanSet $d
$jd = Fresh "jdrow-journals"; CleanSet $jd
Journal (Join-Path $jd "mods\x\tq-uniq-items-x.jsonl") @($row)
JournalDirIni $d $jd
Check ((Guard $d) -eq 1) "journal_dir set: a row in <journal_dir>\mods\x\ -> refused"

$d = Fresh "jdclean"; CleanSet $d
$jd = Fresh "jdclean-journals"; CleanSet $jd
JournalDirIni $d $jd
Check ((Guard $d) -eq 0) "journal_dir set: every journal there and in the mod folder empty -> allowed"

$d = Fresh "jdleft"; CleanSet $d
$jd = Fresh "jdleft-journals"; CleanSet $jd
Journal (Join-Path $d "tq-uniq-items.jsonl") @($row)
JournalDirIni $d $jd
Check ((Guard $d) -eq 1) "journal_dir set: a row left in the mod folder's journal -> refused (both are judged)"

$d = Fresh "jdmissing"; CleanSet $d
JournalDirIni $d (Join-Path $base "no-such-folder")
Journal (Join-Path $d "mods\x\tq-uniq-items-x.jsonl") @($row)
Check ((Guard $d) -eq 1) "journal_dir naming no folder: the mod folder is judged (the mod keeps them there) -> refused"

$d = Fresh "jdcomment"; CleanSet $d
$jd = Fresh "jdcomment-journals"; CleanSet $jd
Journal (Join-Path $jd "tq-uniq-items.jsonl") @($row)
Put (Join-Path $d "uniquetab.ini") "ini_version=23`r`n[advanced]`r`n;journal_dir=$jd`t; commented`r`n"
Check ((Guard $d) -eq 0) "a commented ;journal_dir= line is not read -> the mod folder only, allowed"

# ---- the purge ----------------------------------------------------------------------------------
$generated = @("catalogue.bin", "catalogue.stamp", "uniq-records.txt", "uniq-groups.txt",
    "uniq-excluded.txt", "uniq-geometry.txt", "uniq-search.txt",
    "gray\ug0123456789abcdef.tex", "gray\gray.stamp",
    "plates\uniq_plate_32x32_10x20_L0badf00d.tex", "plates\uniq_plate_32x32_10x20_L0badf00d.src")
$players = @("uniquetab.ini", "uniquetab-buttons.ini", "uniquetab-groups.ini", "uniquetab-text.ini",
    "uniquetab.state", "uniquetab.log", "uniquetab-old.log", "tq-uniq-items.jsonl",
    "tq-uniq-items-custom.jsonl", "tq-uniq-items123.jsonl", "tq-uniq-items.csv",
    "tq-uniq-items.jsonl.bad-x", "notes.txt",
    "mods\x\tq-uniq-items-x.jsonl", "mods\x\uniquetab.ini", "mods\x\uniquetab-buttons.ini",
    "mods\x\catalogue.bin", "mods\x\uniq-groups.txt", "mods\x\gray\ug0123456789abcdef.tex")

function Game([string]$name) {
    $g = Fresh $name
    Put (Join-Path $g "TQ.exe") ""
    Put (Join-Path $g "scripts\uniquetab.asi") ""
    $m = Join-Path $g "scripts\uniquetab"
    foreach ($f in $generated) { Put (Join-Path $m $f) "generated`n" }
    foreach ($f in $players) { Put (Join-Path $m $f) "yours`n" }
    CleanSet $m
    return $g
}
# undeploy.bat in its own cmd, as a player runs it; its output is shown, its exit code returned.
function Undeploy([string]$g) {
    $si = New-Object System.Diagnostics.ProcessStartInfo
    $si.FileName = $env:ComSpec
    $si.Arguments = '/d /c ""' + $undeploy + '" "' + $g + '" --purge"'
    $si.UseShellExecute = $false
    $si.RedirectStandardOutput = $true
    $si.RedirectStandardError = $true
    $pr = [System.Diagnostics.Process]::Start($si)
    $text = $pr.StandardOutput.ReadToEnd() + $pr.StandardError.ReadToEnd()
    $pr.WaitForExit()
    $text -split "`r?`n" | Where-Object { $_ } | ForEach-Object { Write-Host "       | $_" }
    return $pr.ExitCode
}

Write-Host "[test] the purge"
$g = Game "purge"
$m = Join-Path $g "scripts\uniquetab"
Put (Join-Path $m "plates\mine.dds") "yours`n"
Check ((Undeploy $g) -eq 0) "--purge over empty journals -> exit 0"
Check (-not (Test-Path -LiteralPath (Join-Path $g "scripts\uniquetab.asi"))) "the .asi is removed"
$gone = @($generated | Where-Object { $_ -notlike "plates\*" } | Where-Object { Test-Path -LiteralPath (Join-Path $m $_) })
Check ($gone.Count -eq 0) "every generated file is deleted ($($gone -join ', '))"
Check (-not (Test-Path -LiteralPath (Join-Path $m "gray"))) "gray\ (only the mod's names in it) is removed"
Check (-not (Test-Path -LiteralPath (Join-Path $m "plates\uniq_plate_32x32_10x20_L0badf00d.tex"))) "the plates are deleted"
Check (Test-Path -LiteralPath (Join-Path $m "plates\mine.dds")) "a file the mod did not write in plates\ stays (and so does plates\)"
$lost = @($players | Where-Object { -not (Test-Path -LiteralPath (Join-Path $m $_)) })
Check ($lost.Count -eq 0) "every player's file, unknown file and mods\ file stays ($($lost -join ', '))"
Check (Test-Path -LiteralPath (Join-Path $m "mods\y-1a2b3c4d\tq-uniq-items-y-1a2b3c4d.jsonl")) "mods\ is never touched"

$g = Game "purgeemptyplates"
$m = Join-Path $g "scripts\uniquetab"
Check ((Undeploy $g) -eq 0) "--purge with plates\ holding only the mod's plates -> exit 0"
Check (-not (Test-Path -LiteralPath (Join-Path $m "plates"))) "plates\ is removed once empty"
Check (Test-Path -LiteralPath $m) "the mod folder stays while it holds the player's files"

$g = Game "purgerefused"
$m = Join-Path $g "scripts\uniquetab"
Journal (Join-Path $m "mods\x\tq-uniq-items-x.jsonl") @($row)
Check ((Undeploy $g) -eq 1) "--purge with a row in mods\x\ -> refused (exit 1)"
$gone = @(($generated + $players) | Where-Object { -not (Test-Path -LiteralPath (Join-Path $m $_)) })
Check ($gone.Count -eq 0) "refused: nothing in the mod folder is deleted ($($gone -join ', '))"

$g = Game "purgestray"
$m = Join-Path $g "scripts\uniquetab"
Journal (Join-Path $m "tq-uniq-items123.jsonl") @($row)
Check ((Undeploy $g) -eq 1) "--purge with rows in tq-uniq-items123.jsonl -> refused (exit 1)"
Check (Test-Path -LiteralPath (Join-Path $m "catalogue.bin")) "refused: the generated files stay too"

$g = Game "purgejournaldir"
$m = Join-Path $g "scripts\uniquetab"
$jd = Fresh "purgejournaldir-journals"; CleanSet $jd
Journal (Join-Path $jd "mods\x\tq-uniq-items-x.jsonl") @($row)
JournalDirIni $m $jd
Check ((Undeploy $g) -eq 1) "--purge with a row in journal_dir's mods\x\ -> refused (exit 1)"
Check (Test-Path -LiteralPath (Join-Path $m "catalogue.bin")) "refused: nothing in the mod folder is deleted"

Write-Host ""
if ($script:fails -ne 0) {
    Write-Host "[test] journal guard: $($script:fails) of $($script:cases) case(s) FAILED"
    exit 1
}
Write-Host "[test] journal guard: all $($script:cases) cases pass"
exit 0
