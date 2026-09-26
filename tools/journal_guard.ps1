# journal_guard.ps1 - the undeploy interlock (TQ port of GD's tools\journal_guard.ps1).
#
# The collection lives ONLY in the mod's journals: a deposited unique was removed from the game and
# exists nowhere else. Purging the mod folder while a journal holds rows would throw those items
# away. This script exits 1 (REFUSE) when any journal of any save set in the folder -
# tq-uniq-items.jsonl, tq-uniq-items-custom.jsonl, tq-uniq-items-<mod>.jsonl - holds at least one
# row, or cannot be read. Exit 0 = safe to purge. It reads files and writes nothing.
#
# GD's three rules, kept:
#   1. It counts ROWS (format TQ-1: one row per stored copy). A row "pending":"out" (taken, no
#      character save seen yet) is counted too: the mod RESTORES it at the next load, so it is at
#      risk exactly like a stored row.
#   2. It IGNORES the aside copies - *.bad-* and *.tmp. Those are history, not a live journal.
#   3. A journal that exists but cannot be parsed (a header that is not TQ-1's, a newer format, a
#      line that is not JSON) REFUSES. "I cannot read it" is never "there is nothing in it".
#
#     powershell -File tools\journal_guard.ps1 -OutDir <the mod folder>      (exit 0 / 1)
#     powershell -File tools\journal_guard.ps1 -OutDir <the mod folder> -Dump
[CmdletBinding()]
param(
    [string]$OutDir = "",
    [switch]$Dump
)

$ErrorActionPreference = "Stop"

# %UNIQUETAB_OUT% first, exactly as the DLL resolves it (the offline harnesses); else -OutDir.
if ($env:UNIQUETAB_OUT) { $OutDir = $env:UNIQUETAB_OUT }
if (-not $OutDir) {
    Write-Host "[guard] REFUSE: no folder given (-OutDir <the mod folder>)"
    exit 1
}
if (-not (Test-Path -LiteralPath $OutDir -PathType Container)) {
    Write-Host "[guard] no mod folder at `"$OutDir`" - nothing to protect"
    exit 0
}

$journals = @(Get-ChildItem -LiteralPath $OutDir -File -Filter "tq-uniq-items*.jsonl" |
    Where-Object { $_.Name -match '^tq-uniq-items(-[a-z0-9_-]+)?\.jsonl$' })
if ($journals.Count -eq 0) {
    Write-Host "[guard] no journal in `"$OutDir`" - safe"
    exit 0
}

$refuse = $false
foreach ($j in $journals) {
    $lines = @()
    try {
        $lines = @(Get-Content -LiteralPath $j.FullName -Encoding UTF8 -ErrorAction Stop |
            Where-Object { $_.Trim() -ne "" })
    } catch {
        Write-Host "[guard] REFUSE: $($j.Name) cannot be read ($($_.Exception.Message))"
        $refuse = $true
        continue
    }
    if ($lines.Count -eq 0) {
        Write-Host "[guard] REFUSE: $($j.Name) is empty - the mod never writes an empty journal"
        $refuse = $true
        continue
    }
    $header = $null
    try { $header = $lines[0] | ConvertFrom-Json } catch { $header = $null }
    if (-not $header -or $header.journal -ne "titan quest uniquetab" -or [int]$header.format -ne 1) {
        Write-Host "[guard] REFUSE: $($j.Name) line 1 is not a TQ-1 journal header"
        $refuse = $true
        continue
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
    if ($bad -gt 0) {
        Write-Host "[guard] REFUSE: $($j.Name) has $bad line(s) that do not parse"
        $refuse = $true
    }
    if ($rows -gt 0) {
        Write-Host "[guard] REFUSE: $($j.Name) holds $rows row(s) ($out taken but not yet saved) - take them back in the game first"
        $refuse = $true
    } elseif ($bad -eq 0) {
        Write-Host "[guard] $($j.Name): no rows"
    }
}
if ($refuse) { exit 1 }
Write-Host "[guard] every journal is empty - safe to purge"
exit 0
