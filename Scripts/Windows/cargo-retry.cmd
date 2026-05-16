@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "MAX_RETRIES=4"
set "RETRY_DELAY_SECONDS=2"
set "ATTEMPT=1"
set "LOG_FILE=%TEMP%\cargo-retry-%RANDOM%%RANDOM%.log"

:retry
cargo.exe %* >"%LOG_FILE%" 2>&1
set "EXIT_CODE=%ERRORLEVEL%"
type "%LOG_FILE%"

if "%EXIT_CODE%"=="0" goto done

findstr /C:"failed to remove temporary directory" /C:"(os error 32)" "%LOG_FILE%" >nul
if errorlevel 1 goto done

if !ATTEMPT! GEQ %MAX_RETRIES% goto done

echo cargo-retry.cmd: detected transient Windows file lock during cargo build, retrying attempt !ATTEMPT! of %MAX_RETRIES% after %RETRY_DELAY_SECONDS%s.
timeout /t %RETRY_DELAY_SECONDS% /nobreak >nul
set /A ATTEMPT+=1
goto retry

:done
del "%LOG_FILE%" >nul 2>&1
exit /b %EXIT_CODE%
