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
        #
        # `git branch -r --contains` alone answers this ONLY in a full clone.
        # actions/checkout defaults to fetch-depth 1 and hands that depth to
        # `git submodule update`, so on the runner FUZZTEST is a shallow,
        # single-branch clone whose only remote-tracking ref is origin/main -
        # while the pin (704efb34, the 2026-06-29 release) lives on
        # origin/release_2026-06-29. `--contains` then finds nothing and the
        # test failed for two weeks over a pin that is perfectly reachable.
        # Reproduced 2026-08-05 with `git clone --depth 1` + `git submodule
        # update --init --depth 1`: `git branch -r` lists origin/main only.
        #
        # So ask progressively, cheapest first, and only fail when the remote
        # itself cannot account for the commit:
        #   1. local remote-tracking branches (free, conclusive in a full clone)
        #   2. `git ls-remote --heads` (one network round-trip, no objects -
        #      conclusive whenever the pin sits at a branch TIP, which is the
        #      normal case for a release pin)
        #   3. fetch every remote head commit-only (--filter=blob:none, plus
        #      --unshallow when shallow) and re-ask (1) - the mid-branch case
        Push-Location (Join-Path $repoRoot 'ExternalLib\FUZZTEST')
        try {
            $head = (& git rev-parse HEAD 2>$null)
            $containing = @(& git branch -r --contains $head 2>$null)

            if ($containing.Count -eq 0 -and $head) {
                $tips = @(& git ls-remote --heads origin 2>$null |
                    Where-Object { $_ -match ('^{0}\s' -f [regex]::Escape($head)) })
                if ($tips.Count -gt 0) {
                    Write-Host "FUZZTEST pin $head is the tip of: $($tips -join ', ')"
                    $containing = $tips
                } else {
                    Write-Host "FUZZTEST pin $head is not a remote branch tip; fetching remote heads to test ancestry."
                    $isShallow = (& git rev-parse --is-shallow-repository 2>$null)
                    $unshallow = @()
                    if ("$isShallow".Trim() -eq 'true') {
                        $unshallow = @('--unshallow')
                    }
                    & git fetch --no-tags --filter=blob:none @unshallow origin '+refs/heads/*:refs/remotes/origin/*' 2>&1 |
                        ForEach-Object { Write-Host "  git fetch: $_" }
                    $containing = @(& git branch -r --contains $head 2>$null)
                }
            }
        } finally {
            Pop-Location
        }

        if ($containing.Count -eq 0) {
            Write-Host "FUZZTEST HEAD $head is on no remote branch - a fresh clone cannot restore this pin."
        }

        $head | Should Not BeNullOrEmpty
        $containing.Count | Should BeGreaterThan 0
    }
}
