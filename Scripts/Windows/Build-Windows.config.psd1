@{
  Build = @{
    WorkspaceRootEnv = 'WORKSPACE_PATH'
    LogDir = 'logs/windows'

    BuildDirMsvc = 'build-msvc-debug'
    BuildDirClangCl = 'build-clangcl-debug'
    BuildDirClangClTsan = 'build-clangcl-tsan'
    BuildDirProfile = 'build-clangcl-profile'
    BuildDirRelease = 'build-clangcl-release'

    Presets = @{
      MsvcDebug = 'x64-MSVC-Windows-Debug'
      MsvcRelease = 'x64-MSVC-Windows-Release'
      ClangClDebug = 'x64-ClangCL-Windows-Debug'
      ClangClDebugTsan = 'x64-ClangCL-Windows-Debug-TSan'
      ClangClProfile = 'x64-ClangCL-Windows-Profile'
      ClangClRelease = 'x64-ClangCL-Windows-Release'
    }
  }

  Msix = @{
    PackageNameDefault = 'GraphicsEngine'
    Publisher = 'CN=Jonas Heinle'
    Version = '1.5.0.0'
    MinVersion = '10.0.17763.0'
    ManifestTemplate = 'Scripts/Windows/AppxManifest.xml.template'
  }

}
