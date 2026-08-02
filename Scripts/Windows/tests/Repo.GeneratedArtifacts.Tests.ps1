# Guards against generated artifacts sneaking into the git index.
#
# `docs/build/` was excluded by .gitignore (twice, in fact - a duplicate rule
# at both line 11 and line 85) yet 21 files under it plus a stray
# __pycache__/*.pyc were already tracked, because .gitignore only stops NEW
# files from being added - it does nothing once a path is already indexed.
# The result: git kept serving a two-month-stale docs/build/html/ that CI
# regenerates from scratch on every run.
#
# This is deliberately the general gate, not a docs/build-specific one: any
# future generated artifact that lands in the index while also being
# gitignored fails it too.
#
# NOTE: written for Pester 3.4.0 (the version installed here) - no BeforeAll
# outside Describe, and the dash-less assertion syntax.

Describe 'Repo generated artifacts' {

    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path

    It 'has no tracked file that is also gitignored' {
        Push-Location $repoRoot
        try {
            $tracked = @(& git ls-files -i -c --exclude-standard 2>$null)
        } finally {
            Pop-Location
        }

        if ($tracked.Count -gt 0) {
            Write-Host 'Tracked files that .gitignore also excludes (generated artifacts committed by mistake):'
            $tracked | ForEach-Object { Write-Host "  $_" }
            Write-Host 'Fix with: git rm --cached <path> for each file listed above - do not relax .gitignore.'
        }

        $tracked.Count | Should Be 0
    }
}
