@ECHO OFF
SETLOCAL

REM Set up the model worker ahead of time: the venv (Python 3.12, PyTorch,
REM the libraries) and every model, downloaded and checked, so the first
REM picture in usdview does not wait for them. Safe to run again after an
REM interruption: everything resumes or is checked and fixed.
REM
REM   prepare_phantasos.bat                    everything (about 42 GB)
REM   prepare_phantasos.bat --models klein4b   one model: klein4b, matrixgame
REM   prepare_phantasos.bat --quick            check files by size, not SHA-256
REM   prepare_phantasos.bat --check            download nothing; report what is missing
REM
REM Where: HDPHANTASOS_CACHE, else %LOCALAPPDATA%\hdPhantasos.
REM Needs uv (https://docs.astral.sh/uv/) and git on PATH.

WHERE uv >NUL 2>&1
IF ERRORLEVEL 1 (
  ECHO [hdPhantasos] uv is not on PATH. Install it:
  ECHO   powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
  EXIT /B 1
)

SET "MODELS=--models all"
ECHO %* | FINDSTR /C:"--models" >NUL && SET "MODELS="
uv run --no-project --python 3.12 "%~dp0worker\setup_worker.py" %MODELS% %*
EXIT /B %ERRORLEVEL%
