Describe 'WindowsClang.Common' {
  BeforeAll {
    . (Join-Path $PSScriptRoot '..\Resolve-BuildModule.ps1')
    $modulePath = Resolve-BuildModulePath -Name 'WindowsClang.Common'
    Import-Module $modulePath -Force
    $script:repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
  }

  Context 'Test-IsCxxModuleTranslationUnit' {
    It 'detects a module TU whose import is not on the first line' {
      $content = @'
module;
#include <optional>
#include "common/GuiModelTransform.hpp"

import kataglyphis.vulkan.device;
'@
      Test-IsCxxModuleTranslationUnit -Content $content | Should Be $true
    }

    It 'detects a module TU whose import is on the first line' {
      $content = "import kataglyphis.vulkan.app;`n`n#include <cstdint>"
      Test-IsCxxModuleTranslationUnit -Content $content | Should Be $true
    }

    It 'does not flag a plain #include-only TU' {
      $content = @'
#include <optional>
#include "common/Utilities.hpp"

void foo() {}
'@
      Test-IsCxxModuleTranslationUnit -Content $content | Should Be $false
    }

    It 'does not flag an import mention that is inside a comment' {
      $content = "// import kataglyphis.vulkan.app;`n#include <cstdint>"
      Test-IsCxxModuleTranslationUnit -Content $content | Should Be $false
    }

    It 'detects the real module; -first shape used across the engine' {
      $path = Join-Path $script:repoRoot 'Src\GraphicsEngineVulkan\renderer\VulkanRenderer.cpp'
      Test-IsCxxModuleTranslationUnit -Path $path | Should Be $true
    }
  }
}
