#requires -Version 7.0
# Compare-PerfBaseline.ps1
#
# Diffs a fresh Google-Benchmark JSON run (perfTestSuite.exe --benchmark_out=...)
# against a checked-in per-machine baseline, flagging regressions. Turns the
# "Measured baseline" table in BACKLOG.md from prose someone has to eyeball
# into an executable check.
#
# Run (from the repo root, after a clangcl-profile build):
#   build-clangcl-profile\perfTestSuite.exe --benchmark_out=perf.json --benchmark_out_format=json
#   pwsh -ExecutionPolicy Bypass -File .\Scripts\Compare-PerfBaseline.ps1 -CandidatePath perf.json
#
# Baselines are per-machine (thread count, clock speed, thermal state all leak
# into wall-clock numbers - see "Regression tracking" under Performance testing
# in BACKLOG.md), so this script is NOT wired into CI. The checked-in baseline
# for this repo's Windows dev machine is
# Test/perf/baselines/win-9070xt-32core.json. There is deliberately no
# "capture" mode here: to refresh a baseline, run the suite, eyeball the
# result, and copy the JSON over by hand - a bad run should never be able to
# silently become the new baseline.
#
# Exit code: non-zero if any matched benchmark regressed beyond
# -ToleranceFraction. Benchmarks present in only one file are reported but
# never fatal - the suite grows over time and that alone should not fail a
# comparison.

[CmdletBinding()]
param(
    [string]$BaselinePath = (Join-Path $PSScriptRoot '..\Test\perf\baselines\win-9070xt-32core.json'),
    [Parameter(Mandatory)] [string]$CandidatePath,
    # Fraction over baseline real_time before a benchmark is flagged as a
    # regression. Default +25% - generous on purpose; wall-clock noise on a
    # desktop is real (see BACKLOG.md "Regression tracking").
    [double]$ToleranceFraction = 0.25
)

$ErrorActionPreference = 'Stop'
# Format numbers invariantly regardless of host locale (a comma-decimal
# culture would otherwise make "N1" output ambiguous next to the ns/% units).
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture

function ConvertTo-Nanoseconds {
    param([double]$Value, [string]$Unit)
    switch ($Unit) {
        'ns' { $Value }
        'us' { $Value * 1000.0 }
        'ms' { $Value * 1000000.0 }
        's' { $Value * 1000000000.0 }
        default { throw "Unknown time_unit '$Unit' - Google Benchmark only emits ns/us/ms/s." }
    }
}

function Format-Delta {
    param([double]$Delta)
    $pct = $Delta * 100.0
    $sign = if ($pct -ge 0) { '+' } else { '' }
    return "$sign$([math]::Round($pct, 1))%"
}

if (-not (Test-Path $BaselinePath)) { throw "Baseline not found at $BaselinePath" }
if (-not (Test-Path $CandidatePath)) { throw "Candidate not found at $CandidatePath" }

$baseline = Get-Content $BaselinePath -Raw | ConvertFrom-Json
$candidate = Get-Content $CandidatePath -Raw | ConvertFrom-Json

$baselineByName = @{}
foreach ($b in $baseline.benchmarks) {
    $baselineByName[$b.name] = ConvertTo-Nanoseconds -Value $b.real_time -Unit $b.time_unit
}
$candidateByName = @{}
foreach ($b in $candidate.benchmarks) {
    $candidateByName[$b.name] = ConvertTo-Nanoseconds -Value $b.real_time -Unit $b.time_unit
}

$names = [System.Collections.Generic.SortedSet[string]]::new()
foreach ($n in $baselineByName.Keys) { [void]$names.Add($n) }
foreach ($n in $candidateByName.Keys) { [void]$names.Add($n) }

$exitCode = 0
$regressions = @()
$onlyInBaseline = @()
$onlyInCandidate = @()

Write-Host "Baseline:  $BaselinePath" -ForegroundColor Cyan
Write-Host "Candidate: $CandidatePath" -ForegroundColor Cyan
Write-Host "Tolerance: +$([math]::Round($ToleranceFraction * 100, 1))%" -ForegroundColor Cyan
Write-Host ''
Write-Host ("{0,-42} {1,14} {2,14} {3,10}" -f 'Benchmark', 'baseline ns', 'candidate ns', 'delta')
Write-Host ('-' * 84)

foreach ($name in $names) {
    $hasBase = $baselineByName.ContainsKey($name)
    $hasCand = $candidateByName.ContainsKey($name)

    if ($hasBase -and $hasCand) {
        $b = $baselineByName[$name]
        $c = $candidateByName[$name]
        $delta = if ($b -ne 0) { ($c - $b) / $b } else { 0.0 }
        $isRegression = $delta -gt $ToleranceFraction
        $color = if ($isRegression) { 'Red' } else { 'Gray' }
        Write-Host ("{0,-42} {1,14:N1} {2,14:N1} {3,10}" -f $name, $b, $c, (Format-Delta $delta)) -ForegroundColor $color
        if ($isRegression) {
            $regressions += [pscustomobject]@{ Name = $name; Baseline = $b; Candidate = $c; Delta = $delta }
            $exitCode = 1
        }
    } elseif ($hasBase) {
        Write-Host ("{0,-42} {1,14:N1} {2,14} {3,10}" -f $name, $baselineByName[$name], '-', 'only-base') -ForegroundColor Yellow
        $onlyInBaseline += $name
    } else {
        Write-Host ("{0,-42} {1,14} {2,14:N1} {3,10}" -f $name, '-', $candidateByName[$name], 'only-cand') -ForegroundColor Yellow
        $onlyInCandidate += $name
    }
}

Write-Host ''
if ($onlyInBaseline) {
    Write-Host "Only in baseline (removed or renamed?): $($onlyInBaseline -join ', ')" -ForegroundColor Yellow
}
if ($onlyInCandidate) {
    Write-Host "Only in candidate (new benchmark, no baseline yet): $($onlyInCandidate -join ', ')" -ForegroundColor Yellow
}

if ($regressions) {
    Write-Host ''
    Write-Host "REGRESSIONS (beyond +$([math]::Round($ToleranceFraction * 100, 1))%):" -ForegroundColor Red
    foreach ($r in $regressions) {
        Write-Host ("  {0}: {1:N1} ns -> {2:N1} ns ({3})" -f $r.Name, $r.Baseline, $r.Candidate, (Format-Delta $r.Delta)) -ForegroundColor Red
    }
}

Write-Host ''
if ($exitCode -eq 0) {
    Write-Host '=== PERF BASELINE COMPARISON PASSED ===' -ForegroundColor Green
} else {
    Write-Host '=== PERF BASELINE COMPARISON FAILED (regression detected) ===' -ForegroundColor Red
}
exit $exitCode
