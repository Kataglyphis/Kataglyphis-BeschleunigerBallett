# Guards this repo's submodule pins against silent drift.
#
# The checks themselves are generic and were upstreamed on 2026-08-07 to
# ContainerHub's WindowsRepoHygiene.Common (with their own suite against real
# throwaway git repos). What stays here is the only project-specific part: this
# repo's root, and the fact that FUZZTEST in particular is the pin worth
# asserting is restorable.
#
# Why the drift check exists at all: BACKLOG.md carried an item claiming "an
# unidentified host process occasionally re-checks-out ExternalLib/FUZZTEST to
# the latest date tag". Investigated 2026-07-20 - the submodule's reflog held 14
# entries, all between 2026-07-15 and 2026-07-18, clustered into three working
# sessions, with nothing since. No hook, no CMake FetchContent, no script and no
# .gitmodules branch setting references those tags, and `submodule.<n>.branch`
# is unset so `--remote` cannot be the cause either. The evidence points at hand
# (or agent) experimentation rather than a daemon. So this does not chase a
# culprit; it detects the SYMPTOM whatever causes it.
#
# NOTE: written for Pester 3.4.0 (what the Windows lane pins) - no BeforeAll
# outside Describe, and the dash-less assertion syntax.

Describe 'Submodule pins' {

    . (Join-Path $PSScriptRoot '..\Resolve-BuildModule.ps1')
    Import-BuildModule 'WindowsRepoHygiene.Common'

    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path

    It 'reports a status line for every configured submodule' {
        # A vacuous pass guard: an empty result means the checkout omitted
        # submodules entirely, which would make the drift check below prove
        # nothing.
        @(Get-SubmoduleStatusLine -RepoRoot $repoRoot).Count | Should BeGreaterThan 0
    }

    It 'has no submodule checked out away from its recorded commit' {
        $drifted = @(Get-SubmodulePinDrift -RepoRoot $repoRoot)

        if ($drifted.Count -gt 0) {
            Write-Host 'Submodules checked out away from their recorded commit:'
            $drifted | ForEach-Object { Write-Host "  $_" }
            Write-Host 'Fix with: git submodule update --init --recursive'
        }

        $drifted.Count | Should Be 0
    }

    It 'keeps FUZZTEST at a commit reachable from its remote' {
        # A pin that is not reachable from any remote branch cannot be restored
        # by a fresh clone, which turns local-only drift into a build that only
        # works on this machine. The shallow-clone escalation this needs lives
        # in the upstream module.
        $result = Test-SubmoduleCommitReachable -SubmodulePath (Join-Path $repoRoot 'ExternalLib\FUZZTEST')

        if (-not $result.Reachable) {
            Write-Host "FUZZTEST HEAD $($result.Head) is on no remote branch - a fresh clone cannot restore this pin."
        } else {
            Write-Host "FUZZTEST pin $($result.Head) reachable via $($result.Method): $($result.ContainingRef -join ', ')"
        }

        $result.Head | Should Not BeNullOrEmpty
        $result.Reachable | Should Be $true
    }
}
