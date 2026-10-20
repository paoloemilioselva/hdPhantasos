@ECHO OFF
REM ===========================================================================
REM hdPhantasos - shared standalone OpenUSD runtime environment.
REM
REM Every USD-facing script in this repository calls this file. It is the one
REM place that knows where OpenUSD lives. The Linux counterpart is
REM setup_usd_env.sh; change both or neither.
REM
REM This script is CALLed, not run in a subshell, so it deliberately does not
REM SETLOCAL: the variables it sets must survive into the caller.
REM
REM Overrides, all optional, all honoured if already set:
REM   USDROOT                  OpenUSD install root
REM   USDEXTRA                 install prefix for hdPhantasos's own plugin
REM   HDPHANTASOS_SKIP_PYTHON  set to 1 to skip the Python conformance check
REM ===========================================================================

SET "HDPHANTASOS_ROOT=%~dp0"
IF "%HDPHANTASOS_ROOT:~-1%"=="\" SET "HDPHANTASOS_ROOT=%HDPHANTASOS_ROOT:~0,-1%"

REM ---------------------------------------------------------------------------
REM 1. Locate OpenUSD.
REM     An explicit USDROOT always wins. Otherwise probe known-good roots,
REM     newest first, and validate by the presence of pxrConfig.cmake.
REM ---------------------------------------------------------------------------
IF DEFINED USDROOT GOTO have_usdroot
FOR %%R IN (
  "C:\dev\usd-26.03"
  "C:\dev\usd-26.05"
  "C:\dev\usd-25.08"
) DO IF NOT DEFINED USDROOT IF EXIST "%%~R\pxrConfig.cmake" SET "USDROOT=%%~R"

:have_usdroot
IF NOT DEFINED USDROOT (
  ECHO [hdPhantasos] OpenUSD was not found.
  ECHO               Set USDROOT to an OpenUSD install root containing pxrConfig.cmake,
  ECHO               or install one under C:\dev. See docs\building.md.
  EXIT /B 1
)
IF NOT EXIST "%USDROOT%\pxrConfig.cmake" (
  ECHO [hdPhantasos] USDROOT does not look like an OpenUSD install: %USDROOT%
  ECHO               Expected "%USDROOT%\pxrConfig.cmake" to exist.
  EXIT /B 1
)

REM Install prefix for the plugin itself, beside the USD install rather than
REM inside it, and shared with hdClaude and hdGemini: plugin discovery is a
REM union, so each delegate is one more entry in the same tree.
IF NOT DEFINED USDEXTRA SET "USDEXTRA=%USERPROFILE%\Desktop\usd-26.03-extra"

REM ---------------------------------------------------------------------------
REM 2. Python conformance.
REM     OpenUSD's Python bindings are ABI-locked to the interpreter the
REM     distribution was built against, which pxrConfig.cmake records. usdview
REM     and usdrecord are Python programs, so the active Python must match.
REM     This script never selects, installs, or provisions an interpreter.
REM ---------------------------------------------------------------------------
IF "%HDPHANTASOS_SKIP_PYTHON%"=="1" GOTO python_done

SET "USD_PYTHON_RECORDED="
FOR /F "usebackq tokens=2 delims=[]" %%A IN (
  `FINDSTR /C:"set(Python3_EXECUTABLE [[" "%USDROOT%\pxrConfig.cmake"`
) DO IF NOT DEFINED USD_PYTHON_RECORDED SET "USD_PYTHON_RECORDED=%%A"

SET "USD_PYTHON_REQUIRED="
IF DEFINED USD_PYTHON_RECORDED IF EXIST "%USD_PYTHON_RECORDED%" (
  FOR /F "usebackq delims=" %%V IN (
    `"%USD_PYTHON_RECORDED%" -c "import sys;print('%%d.%%d'%%sys.version_info[:2])" 2^>NUL`
  ) DO SET "USD_PYTHON_REQUIRED=%%V"
)
REM Fall back to the version OpenUSD 26.03 ships against when the recorded
REM interpreter has been moved or removed.
IF NOT DEFINED USD_PYTHON_REQUIRED SET "USD_PYTHON_REQUIRED=3.12"

WHERE python.exe >NUL 2>NUL
IF ERRORLEVEL 1 (
  ECHO [hdPhantasos] Python was not found in the active environment.
  ECHO               OpenUSD at %USDROOT% requires Python %USD_PYTHON_REQUIRED%.
  EXIT /B 1
)

FOR /F "usebackq delims=" %%V IN (
  `python -c "import sys;print('%%d.%%d'%%sys.version_info[:2])" 2^>NUL`
) DO SET "USD_PYTHON_ACTIVE=%%V"

IF NOT "%USD_PYTHON_ACTIVE%"=="%USD_PYTHON_REQUIRED%" (
  ECHO [hdPhantasos] Python version mismatch.
  ECHO               Active:   %USD_PYTHON_ACTIVE%
  ECHO               Required: %USD_PYTHON_REQUIRED%  ^(the interpreter OpenUSD was built against^)
  IF DEFINED USD_PYTHON_RECORDED ECHO               Recorded: %USD_PYTHON_RECORDED%
  ECHO               Activate a matching environment, or set HDPHANTASOS_SKIP_PYTHON=1
  ECHO               for builds that do not use the Python bindings.
  EXIT /B 1
)

:python_done

REM ---------------------------------------------------------------------------
REM 3. OpenUSD runtime paths.
REM ---------------------------------------------------------------------------
SET "PXR_PLUGINPATH_NAME=%USDROOT%;%USDROOT%\plugin\usd;%USDEXTRA%\plugin\usd"
SET "PYTHONPATH=%USDROOT%\lib\python;%USDEXTRA%\lib\python;%PYTHONPATH%"
SET "PATH=%USDROOT%\bin;%USDEXTRA%\bin;%USDROOT%\lib;%USDEXTRA%\lib;%PATH%"

EXIT /B 0
