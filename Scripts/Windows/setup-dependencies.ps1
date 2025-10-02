Param(
    [string]$VulkanVersion = '1.4.321.1',
    [string]$ClangVersion  = '21.1.1',
    [string]$VulkanSdkPath = 'C:\VulkanSDK'
)

Write-Host "=== Installing build dependencies on Windows ==="

# Enable verbose logging
$ErrorActionPreference = 'Stop'

# Install LLVM/Clang via Chocolatey
# 
Write-Host "Installing LLVM/Clang $ClangVersion..."
winget install --accept-source-agreements --accept-package-agreements --id=LLVM.LLVM -v $ClangVersion -e
# choco install llvm --version="$ClangVersion" --params '/AddToPath' -y

# Install sccache
Write-Host "Installing Ccache..."
winget install --accept-source-agreements --accept-package-agreements --id=Ccache.Ccache -e 
# choco install sccache -y

# install scoop (if not present)
iwr -useb get.scoop.sh | iex
# install sccache
scoop install sccache
# verify
sccache --version
sccache -s   # show stats

# Install CMake, Cppcheck, NSIS via WinGet
Write-Host "Installing CMake, Cppcheck and NSIS via winget..."
winget install --accept-source-agreements --accept-package-agreements cmake cppcheck nsis
# also get wix
winget install --accept-source-agreements --accept-package-agreements --id WiXToolset.WiXToolset -e
# get ninja
Write-Host "Installing Ninja via winget..."
winget install --accept-source-agreements --accept-package-agreements --id=Ninja-build.Ninja  -e

# Install VulkanSDK via WinGet
Write-Host "Installing Vulkan SDK $VulkanVersion..."
winget install --accept-source-agreements --accept-package-agreements --id KhronosGroup.VulkanSDK -v $VulkanVersion -e

# Add Vulkan SDK paths to GITHUB_PATH
Write-Host "Adding Vulkan SDK Bin, Lib and Include to PATH"
$binPath    = "${VulkanSdkPath}\${VulkanVersion}\Bin"
$libPath    = "${VulkanSdkPath}\${VulkanVersion}\Lib"
$includePath= "${VulkanSdkPath}\${VulkanVersion}\Include"

foreach ($path in @($binPath, $libPath, $includePath)) {
    if (Test-Path $path) {
        Write-Host "Appending $path to GITHUB_PATH"
        $path | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
    } else {
        Write-Warning "Path not found: $path"
    }
}

# Add NSIS to PATH (in case it's under Program Files (x86))
$nsisPath = 'C:\Program Files (x86)\NSIS'
if (Test-Path $nsisPath) {
    Write-Host "Adding NSIS path to GITHUB_PATH: $nsisPath"
    $nsisPath | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
} else {
    Write-Warning "NSIS installation path not found at $nsisPath"
}

Write-Host "=== Dependency installation completed ==="
