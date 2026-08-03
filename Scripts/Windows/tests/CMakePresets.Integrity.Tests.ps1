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

    function Get-EffectiveBinaryDir {
        param($PresetsByName, $Name)

        $preset = $PresetsByName[$Name]
        if ($null -eq $preset) {
            return $null
        }
        if ($preset.binaryDir) {
            return $preset.binaryDir
        }
        foreach ($parent in @($preset.inherits)) {
            $inherited = Get-EffectiveBinaryDir -PresetsByName $PresetsByName -Name $parent
            if ($inherited) {
                return $inherited
            }
        }
        return $null
    }

    $presetsByName = @{}
    foreach ($preset in @($presets.configurePresets)) {
        $presetsByName[$preset.name] = $preset
    }

    # Build-Windows.config.psd1 maps Build-Windows.ps1 -Configurations names to
    # a configure preset and a build directory; the two files must agree, or
    # `Build-Windows-Container.ps1` builds into a tree `ctest --preset` never
    # finds (or two configurations silently collide in one directory).
    $configPath = Join-Path $repoRoot 'Scripts\Windows\Build-Windows.config.psd1'
    $buildConfig = Import-PowerShellDataFile -Path $configPath
    $configurations = $buildConfig.Build.Configurations

    It 'points every Build-Windows.config.psd1 configuration at a real configure preset' {
        $missing = @()
        foreach ($name in $configurations.Keys) {
            $presetName = $configurations[$name].Preset
            if (-not $presetsByName.ContainsKey($presetName)) {
                $missing += "$name -> $presetName"
            }
        }
        ($missing -join '; ') | Should BeNullOrEmpty
    }

    It 'agrees with CMakePresets.json on each configuration''s build directory' {
        $mismatches = @()
        foreach ($name in $configurations.Keys) {
            $entry = $configurations[$name]
            $expected = "`${sourceDir}/$($entry.BuildDir)/"
            $actual = Get-EffectiveBinaryDir -PresetsByName $presetsByName -Name $entry.Preset
            if ($actual -ne $expected) {
                $mismatches += "$name ($($entry.Preset)): expected '$expected', got '$actual'"
            }
        }
        ($mismatches -join '; ') | Should BeNullOrEmpty
    }

    It 'gives no two Build-Windows.config.psd1 configurations the same BuildDir' {
        $seen = @{}
        $collisions = @()
        foreach ($name in $configurations.Keys) {
            $dir = $configurations[$name].BuildDir
            if ($seen.ContainsKey($dir)) {
                $collisions += "${dir}: $($seen[$dir]), $name"
            } else {
                $seen[$dir] = $name
            }
        }
        ($collisions -join '; ') | Should BeNullOrEmpty
    }

    It 'gives every Windows configure preset a unique binaryDir' {
        # x64-Clang-Windows-Release (the WiX package preset's configure preset)
        # and x64-ClangCL-Windows-RelWithDebInfo are the one deliberate
        # exception - both stay on build_release/. Name them explicitly so a
        # third preset sharing a directory still fails this test.
        $allowedSharedDir = "`${sourceDir}/build_release/"
        $allowedSharers = @('x64-Clang-Windows-Release', 'x64-ClangCL-Windows-RelWithDebInfo')

        $windowsPresets = @($presets.configurePresets | Where-Object {
            (-not $_.hidden) -and $_.condition -and ($_.condition.rhs -eq 'Windows')
        })

        $byDir = @{}
        foreach ($preset in $windowsPresets) {
            $dir = Get-EffectiveBinaryDir -PresetsByName $presetsByName -Name $preset.name
            if (-not $byDir.ContainsKey($dir)) {
                $byDir[$dir] = @()
            }
            $byDir[$dir] += $preset.name
        }

        $violations = @()
        foreach ($dir in $byDir.Keys) {
            $names = $byDir[$dir]
            if ($names.Count -le 1) {
                continue
            }
            if ($dir -eq $allowedSharedDir) {
                $unexpected = @($names | Where-Object { $allowedSharers -notcontains $_ })
                if ($unexpected.Count -gt 0) {
                    $violations += "$dir shared by $($names -join ', ') (unexpected: $($unexpected -join ', '))"
                }
            } else {
                $violations += "$dir shared by $($names -join ', ')"
            }
        }
        ($violations -join '; ') | Should BeNullOrEmpty
    }
}
