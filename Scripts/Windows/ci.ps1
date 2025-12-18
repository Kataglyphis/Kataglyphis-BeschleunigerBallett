$ErrorActionPreference = 'Stop'

# Keep behavior close to the old workflow; image should already contain toolchains.
if (Test-Path 'C:\Program Files\LLVM\bin') {
  $env:Path = 'C:\Program Files\LLVM\bin;' + $env:Path
}

####################################################################################################
# MSVC Debug
cmake -B "$env:BUILD_DIR" --preset 'x64-MSVC-Windows-Debug' -Dmyproject_ENABLE_CPPCHECK='OFF' -DWINDOWS_CI='ON'
cmake --build "$env:BUILD_DIR"

Push-Location "$env:BUILD_DIR"
ctest
Pop-Location

Remove-Item -Path "$env:BUILD_DIR" -Recurse -Force

####################################################################################################
# MSVC Release
cmake -B "$env:BUILD_DIR" --preset 'x64-MSVC-Windows-Release' -Dmyproject_ENABLE_CPPCHECK='OFF'
cmake --build "$env:BUILD_DIR"

Remove-Item -Path "$env:BUILD_DIR" -Recurse -Force

####################################################################################################
# ClangCL Debug
cmake -B "$env:BUILD_DIR" --preset 'x64-ClangCL-Windows-Debug' -Dmyproject_ENABLE_CPPCHECK='OFF'
cmake --build "$env:BUILD_DIR" --preset 'x64-ClangCL-Windows-Debug'

Push-Location "$env:BUILD_DIR"
ctest
& 'llvm-profdata.exe' merge -sparse 'Test\compile\default.profraw' -o 'compileTestSuite.profdata'
& 'llvm-cov.exe' report 'compileTestSuite.exe' -instr-profile='compileTestSuite.profdata'
& 'llvm-cov.exe' export 'compileTestSuite.exe' -format=text -instr-profile='compileTestSuite.profdata' | Out-File -FilePath 'coverage.json' -Encoding UTF8
& 'llvm-cov.exe' show 'compileTestSuite.exe' -instr-profile='compileTestSuite.profdata'
Pop-Location

# Optional analyses: keep them non-blocking like before
try {
  $sourceFiles = Get-ChildItem -Path 'Src' -Recurse -Include '*.cpp', '*.cc' | ForEach-Object { $_.FullName }
  if ($null -ne $sourceFiles -and $sourceFiles.Count -gt 0) {
    clang++ --analyze -DUSE_RUST=1 -Xanalyzer -analyzer-output=html $sourceFiles
  }
} catch {
  Write-Host "Clang static analysis (HTML) failed (ignored): $($_.Exception.Message)"
}

try {
  New-Item -ItemType Directory -Path 'scan-build-reports' -Force | Out-Null
  scan-build --use-analyzer='C:\Program Files\LLVM\bin\clang-cl.exe' -o 'scan-build-reports' cmake --build "$env:BUILD_DIR" --preset 'x64-ClangCL-Windows-Debug'
} catch {
  Write-Host "Clang static analysis (scan-build) failed (ignored): $($_.Exception.Message)"
}

####################################################################################################
# Profiling build + benchmarks
clang --version
cmake -B "$env:BUILD_DIR_RELEASE" --preset "$env:CLANG_PROFILE_PRESET" -Dmyproject_ENABLE_CPPCHECK=OFF
cmake --build "$env:BUILD_DIR_RELEASE" --preset "$env:CLANG_PROFILE_PRESET"

Push-Location "$env:BUILD_DIR_RELEASE"
.\perfTestSuite.exe --benchmark_out=results.json --benchmark_out_format=json
Pop-Location

####################################################################################################
# Clang Release + package
Remove-Item -Path "$env:BUILD_DIR_RELEASE" -Recurse -Force
clang --version
cmake -B "$env:BUILD_DIR_RELEASE" --preset 'x64-ClangCL-Windows-Release' -Dmyproject_ENABLE_CPPCHECK='OFF' -DWINDOWS_CI=ON
$env:CMAKE_BUILD_PARALLEL_LEVEL = $env:NUMBER_OF_PROCESSORS
cmake --build "$env:BUILD_DIR_RELEASE" --preset 'x64-ClangCL-Windows-Release' -DWINDOWS_CI=ON
cmake --build "$env:BUILD_DIR_RELEASE" --target package --verbose
