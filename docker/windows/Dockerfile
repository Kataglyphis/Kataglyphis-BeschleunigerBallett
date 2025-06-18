# escape=`

# Use the latest Windows Server Core 2022 image.
FROM mcr.microsoft.com/windows/servercore:ltsc2025

# Restore the default Windows shell for correct batch processing.
SHELL ["cmd", "/S", "/C"]

RUN `
    # Download the Build Tools bootstrapper.
    curl -SL --output vs_buildtools.exe https://aka.ms/vs/17/release/vs_buildtools.exe `
    `
    # Install Build Tools with the Microsoft.VisualStudio.Workload.AzureBuildTools workload, excluding workloads and components with known issues.
    && (start /w vs_buildtools.exe --quiet --wait --norestart --nocache `
    --installPath "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools" `
    --add Microsoft.VisualStudio.Workload.AzureBuildTools `
    --add Microsoft.Component.MSBuild `
    --add Microsoft.VisualStudio.Component.CoreBuildTools `
    --add Microsoft.VisualStudio.Component.Roslyn.Compiler `
    --add Microsoft.VisualStudio.Component.Roslyn.LanguageServices `
    --add Microsoft.VisualStudio.Component.TextTemplating `
    --add Microsoft.VisualStudio.Component.VC.ASAN `
    --add Microsoft.VisualStudio.Component.VC.ATL `
    --add Microsoft.VisualStudio.Component.VC.ATLMFC `
    --add Microsoft.VisualStudio.Component.VC.CLI.Support `
    --add Microsoft.VisualStudio.Component.VC.CMake.Project `
    --add Microsoft.VisualStudio.Component.VC.CoreBuildTools `
    --add Microsoft.VisualStudio.Component.VC.CoreIde `
    --add Microsoft.VisualStudio.Component.VC.Redist.14.Latest `
    --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    --add Microsoft.VisualStudio.Component.Windows10SDK `
    --add Microsoft.VisualStudio.Component.Windows11SDK.26100 `
    --add Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core `
    --add Microsoft.VisualStudio.Component.Llvm.ClangToolset `
    --add Microsoft.VisualStudio.Workload.MSBuildTools `
    --add Microsoft.VisualStudio.Workload.UniversalBuildTools `
    --add Microsoft.VisualStudio.Workload.VCTools `
    --add Microsoft.VisualStudio.Workload.ManagedDesktopBuildTools `
    --remove Microsoft.VisualStudio.Component.Windows10SDK.10240 `
    --remove Microsoft.VisualStudio.Component.Windows10SDK.10586 `
    --remove Microsoft.VisualStudio.Component.Windows10SDK.14393 `
    --remove Microsoft.VisualStudio.Component.Windows81SDK `
    || IF "%ERRORLEVEL%"=="3010" EXIT 0) `
    `
    # Cleanup
    && del /q vs_buildtools.exe


RUN `
    curl -L -o Git-64-bit.exe "https://github.com/git-for-windows/git/releases/download/v2.47.1.windows.2/Git-2.47.1.2-64-bit.exe" `
    && start /wait Git-64-bit.exe /SILENT /NORESTART `
    && del Git-64-bit.exe

# after your Git install…
# ---------------------------------------------------------------
# Install Scoop (admin mode) plus llvm & nano in one single RUN
# After your Git install…
# Temporarily switch to PowerShell so we can install Scoop cleanly
SHELL ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]
RUN `
    irm get.scoop.sh -outfile 'install.ps1' ; `
    .\install.ps1 -RunAsAdmin ; 
    # install llvm & nano
    scoop install llvm nano
# ---------------------------------------------------------------


# Define the entry point for the docker container.
# This entry point starts the developer command prompt and launches the PowerShell shell.
ENTRYPOINT ["C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\Common7\\Tools\\VsDevCmd.bat","-arch=amd64" , "&&", "powershell.exe", "-NoLogo", "-ExecutionPolicy", "Bypass"]
