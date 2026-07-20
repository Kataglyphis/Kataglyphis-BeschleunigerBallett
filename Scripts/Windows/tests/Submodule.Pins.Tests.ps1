# Guards the submodule pins against silent drift.
#
# BACKLOG.md carried an item claiming "an unidentified host process
# occasionally re-checks-out ExternalLib/FUZZTEST to the latest date tag".
# Investigated 2026-07-20: the submodule's reflog holds 14 entries, all
# between 2026-07-15 and 2026-07-18, clustered into three working sessions,
# with nothing since. No hook, no CMake FetchContent, no script and no
# .gitmodules branch setting references those tags, and `submodule.<n>.branch`
# is unset so `--remote` cannot be the cause either. The evidence points at
# hand (or agent) experimentation during those sessions rather than a daemon.
#
# So this does not try to identify a culprit. It detects the SYMPTOM whatever
# causes it: a submodule checked out somewhere other than the commit this
# repository records. `git submodule status` marks those with a leading '+',
# which is easy to miss in a wall of output and means the build is compiling
# something other than what is committed.
#
# NOTE: written for Pester 3.4.0 (the version installed here) - no BeforeAll
# outside Describe, and the dash-less assertion syntax.

Describe 'Submodule pins' {

    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path

    It 'reports a status line for every configured submodule' {
        Push-Location $repoRoot
        try {
            $status = @(& git submodule status 2>$null)
        } finally {
            Pop-Location
        }
        $status.Count | Should BeGreaterThan 0
    }

    It 'has no submodule checked out away from its recorded commit' {
        Push-Location $repoRoot
        try {
            $status = @(& git submodule status 2>$null)
        } finally {
            Pop-Location
        }

        # '+' = checked out at a different commit than the superproject records.
        # '-' = not initialised, which is a different (and usually harmless)
        #       state and is deliberately not failed here.
        $drifted = @($status | Where-Object { $_ -match '^\+' })

        if ($drifted.Count -gt 0) {
            Write-Host 'Submodules checked out away from their recorded commit:'
            $drifted | ForEach-Object { Write-Host "  $_" }
            Write-Host 'Fix with: git submodule update --init --recursive'
        }

        $drifted.Count | Should Be 0
    }

    It 'keeps FUZZTEST at a commit reachable from its remote' {
        # The specific submodule the backlog worried about. A pin that is not
        # an ancestor of any remote branch cannot be restored by a fresh clone,
        # which turns local-only drift into a build that only works here.
        Push-Location (Join-Path $repoRoot 'ExternalLib\FUZZTEST')
        try {
            $head = (& git rev-parse HEAD 2>$null)
            $describes = @(& git branch -r --contains $head 2>$null)
        } finally {
            Pop-Location
        }

        $head | Should Not BeNullOrEmpty
        $describes.Count | Should BeGreaterThan 0
    }
}
