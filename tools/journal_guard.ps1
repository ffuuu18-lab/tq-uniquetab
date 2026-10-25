# journal_guard.ps1 - the undeploy interlock (TQ port of GD's tools\journal_guard.ps1).
#
# The collection lives ONLY in the mod's journals: a deposited unique was removed from the game and
# exists nowhere else. Removing the mod while a journal holds rows leaves those items in a file
# nothing reads. This script exits 1 (REFUSE) when any journal of any save set holds at least one
# row, or cannot be read. Exit 0 = safe to uninstall. It reads files and writes nothing.
#
# WHERE IT LOOKS (GD's per-custom-game scan): the mod folder itself (the main campaign's
# tq-uniq-items.jsonl, tq-uniq-items-custom.jsonl, and a mod's journal an earlier version kept
# there), then EVERY folder under mods\ (mods\<mod>\tq-uniq-items-<mod>.jsonl), sorted by name.
# With journal_dir set in the mod folder's uniquetab.ini (an existing folder), the journals are
# there: that folder and its mods\ first, then the mod folder and its mods\ as above.
# Every set is checked even when one refuses, so one call lists everything at risk; the verdict
# is the worst.
#
# WHAT IT JUDGES: every file named tq-uniq-items*.jsonl in those folders, hidden ones included.
# A name the mod writes (tq-uniq-items.jsonl, tq-uniq-items-<leaf>.jsonl) is a journal; any other
# name with that start and end (a copy made by hand, tq-uniq-items123.jsonl) is judged by the
# same rules and named as such - never skipped. A copy of the collection is the collection.
#
# GD's three rules, kept:
#   1. It counts ROWS (format TQ-1: one row per stored copy). A row "pending":"out" (taken, no
#      character save seen yet) is counted too: the mod RESTORES it at the next load, so it is at
#      risk exactly like a stored row.
#   2. It IGNORES the aside copies - *.bad-* and *.tmp. Those are history, not a live journal.
#   3. A journal that exists but cannot be parsed (a header that is not TQ-1's, a newer format, a
#      line that is not JSON) or cannot be opened REFUSES. "I cannot read it" is never "there is
#      nothing in it".
#
#     powershell -File tools\journal_guard.ps1 -OutDir <the mod folder>      (exit 0 / 1)
#     powershell -File tools\journal_guard.ps1 -OutDir <the mod folder> -Dump
[CmdletBinding()]
param(
    [string]$OutDir = "",
    [switch]$Dump
)

$ErrorActionPreference = "Stop"

# -OutDir first (undeploy.bat passes the folder it is about to purge, so the guard reads THAT
# folder); %UNIQUETAB_OUT% only when no folder is given, as GD's guard orders them.
if (-not $OutDir -and $env:UNIQUETAB_OUT) { $OutDir = $env:UNIQUETAB_OUT }
if (-not $OutDir) {
    Write-Host "[guard] REFUSE: no folder given (-OutDir <the mod folder>)"
    exit 1
}
if (-not (Test-Path -LiteralPath $OutDir -PathType Container)) {
    Write-Host "[guard] no mod folder at `"$OutDir`" - nothing to protect"
    exit 0
}

# `journal_dir=` in uniquetab.ini moves the JOURNALS (and only the journals) out of the mod folder
# (GD's guard). The mod reads it once at start-up; a guard that did not read it would look in the
# wrong folder and report an empty collection. The ini itself always lives in the mod folder, so it
# is found first and then consulted. A journal still in the mod folder (journal_dir set later, the
# files not moved) is a collection too: the mod folder's sets are judged as well.
$JournalDir = $OutDir
$ini = Join-Path $OutDir "uniquetab.ini"
if (Test-Path -LiteralPath $ini -PathType Leaf) {
    $hit = Select-String -LiteralPath $ini -Pattern '^\s*journal_dir\s*=' | Select-Object -First 1
    if ($hit) {
        $v = ($hit.Line -replace '^\s*journal_dir\s*=', '') -replace ';.*$', ''
        $v = $v.Trim().Trim('"').TrimEnd('\', '/').Trim()
        if ($v -and (Test-Path -LiteralPath $v -PathType Container)) {
            $JournalDir = (Resolve-Path -LiteralPath $v).ProviderPath
            Write-Host "[guard] journal_dir=$v in $ini - looking for the journals THERE, and in $OutDir"
        } elseif ($v) {
            Write-Host "[guard] journal_dir=$v in $ini is not an existing folder - the mod keeps the journals in $OutDir"
        }
    }
}

# The names the mod writes: tq-uniq-items.jsonl and tq-uniq-items-<leaf>.jsonl, a leaf being the
# characters a mod's folder may carry (src\ut_folders.h).
$known = '^tq-uniq-items(-[A-Za-z0-9._+-]{1,63})?\.jsonl$'

# One journal: 1 = REFUSE, 0 = no rows.
function Test-Journal([System.IO.FileInfo]$j, [string]$name) {
    if ($j.Name -notmatch $known) {
        Write-Host "[guard] $name is not a name the mod writes (a copy?) - judged like a journal"
    }
    $lines = @()
    try {
        $lines = @(Get-Content -LiteralPath $j.FullName -Encoding UTF8 -ErrorAction Stop |
            Where-Object { $_.Trim() -ne "" })
    } catch {
        Write-Host "[guard] REFUSE: $name cannot be read ($($_.Exception.Message))"
        return 1
    }
    if ($lines.Count -eq 0) {
        Write-Host "[guard] REFUSE: $name is empty - the mod never writes an empty journal"
        return 1
    }
    $header = $null
    try { $header = $lines[0] | ConvertFrom-Json } catch { $header = $null }
    if (-not $header -or $header.journal -ne "titan quest uniquetab" -or ($header.format -as [int]) -ne 1) {
        Write-Host "[guard] REFUSE: $name line 1 is not a TQ-1 journal header"
        return 1
    }
    $rows = 0; $out = 0; $bad = 0
    for ($i = 1; $i -lt $lines.Count; $i++) {
        try {
            $r = $lines[$i] | ConvertFrom-Json
            if (-not $r.record) { $bad++; continue }
            $rows++
            if ($r.pending -eq "out") { $out++ }
            if ($Dump) { Write-Host ("  {0}  seed {1}{2}" -f $r.record, $r.seed, $(if ($r.pending) { "  pending " + $r.pending } else { "" })) }
        } catch { $bad++ }
    }
    $rc = 0
    if ($bad -gt 0) {
        Write-Host "[guard] REFUSE: $name has $bad line(s) that do not parse"
        $rc = 1
    }
    if ($rows -gt 0) {
        Write-Host "[guard] REFUSE: $name holds $rows row(s) ($out taken but not yet saved) - take them back in the game first"
        $rc = 1
    } elseif ($bad -eq 0) {
        Write-Host "[guard] ${name}: no rows"
    }
    return $rc
}

# The sets: the journals' folder, then every mods\<name>\ folder under it (hidden ones included);
# with journal_dir set to another folder, the mod folder and its mods\ after them.
$sets = @()
$bases = @(@{ Dir = $JournalDir; Label = "" })
if ($JournalDir -ne $OutDir -and (Resolve-Path -LiteralPath $OutDir).ProviderPath.TrimEnd('\') -ne $JournalDir.TrimEnd('\')) {
    $bases[0].Label = "journal_dir "
    $bases += @{ Dir = $OutDir; Label = "" }
}
foreach ($b in $bases) {
    $sets += @{ Dir = $b.Dir; Prefix = $b.Label }
    $modsDir = Join-Path $b.Dir "mods"
    if (Test-Path -LiteralPath $modsDir -PathType Container) {
        Get-ChildItem -LiteralPath $modsDir -Directory -Force | Sort-Object Name | ForEach-Object {
            $sets += @{ Dir = $_.FullName; Prefix = "$($b.Label)mods\$($_.Name)\" }
        }
    }
}

$verdict = 0
$seen = 0
foreach ($s in $sets) {
    $files = @()
    try {
        $files = @(Get-ChildItem -LiteralPath $s.Dir -File -Force -ErrorAction Stop |
            Where-Object { $_.Name -like "tq-uniq-items*.jsonl" -and
                $_.Name -notlike "*.bad-*" -and $_.Name -notlike "*.tmp" } | Sort-Object Name)
    } catch {
        Write-Host "[guard] REFUSE: the folder `"$($s.Dir)`" cannot be listed ($($_.Exception.Message))"
        $verdict = 1
        continue
    }
    foreach ($j in $files) {
        $seen++
        if ((Test-Journal $j ($s.Prefix + $j.Name)) -ne 0) { $verdict = 1 }
    }
}
if ($verdict -ne 0) { exit 1 }
if ($seen -eq 0) {
    if ($JournalDir -ne $OutDir) {
        Write-Host "[guard] no journal in `"$JournalDir`" (journal_dir), `"$OutDir`" or their mods\ folders - safe"
    } else {
        Write-Host "[guard] no journal in `"$OutDir`" or its mods\ folders - safe"
    }
    exit 0
}
Write-Host "[guard] every journal is empty ($seen checked, mods\ included) - safe"
exit 0
