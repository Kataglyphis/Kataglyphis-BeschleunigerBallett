# Guards CMakePresets.json against dangling references.
#
# Removing a configure preset that a build/test/package preset still points at
# makes CMake reject the ENTIRE file ("Invalid configurePreset"), so every
# preset breaks at once - not just the one that was removed. That happened in
# July 2026 when a package preset (windows-clang-release-wix) turned out to be
# the only consumer of a configure preset that looked unused, because a grep
# for references had excluded CMakePresets.json itself.
#
# NOTE: written for Pester 3.4.0 (the version installed here) - no BeforeAll
# outside Describe, and the dash-less assertion syntax.

Describe 'CMakePresets.json integrity' {

    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
    $presets = Get-Content (Join-Path $repoRoot 'CMakePresets.json') -Raw | ConvertFrom-Json
    $configureNames = @($presets.configurePresets | ForEach-Object { $_.name })

    It 'parses as JSON and defines configure presets' {
        $configureNames.Count | Should BeGreaterThan 0
    }

    It 'has no build/test/package preset pointing at a missing configure preset' {
        $dangling = @()
        foreach ($group in @('buildPresets', 'testPresets', 'packagePresets')) {
            foreach ($preset in @($presets.$group)) {
                $target = $preset.configurePreset
                if ($target -and ($configureNames -notcontains $target)) {
                    $dangling += "$group/$($preset.name) -> $target"
                }
            }
        }
        ($dangling -join '; ') | Should BeNullOrEmpty
    }

    It 'has no preset inheriting from a missing preset' {
        $broken = @()
        foreach ($group in @('configurePresets', 'buildPresets', 'testPresets')) {
            $names = @($presets.$group | ForEach-Object { $_.name })
            foreach ($preset in @($presets.$group)) {
                foreach ($parent in @($preset.inherits)) {
                    if ($parent -and ($names -notcontains $parent)) {
                        $broken += "$group/$($preset.name) inherits $parent"
                    }
                }
            }
        }
        ($broken -join '; ') | Should BeNullOrEmpty
    }

    It 'keeps configurations testable via test presets' {
        # Not every preset needs one, but the ratio matters: the file once had
        # 26 configure presets and exactly ONE test preset, so most
        # configurations could be built but never `ctest --preset`ed.
        $testTargets = @($presets.testPresets | ForEach-Object { $_.configurePreset })
        $testTargets.Count | Should BeGreaterThan 3
    }
}
