@ECHO OFF
SETLOCAL

REM Open a stage in usdview with the hdPhantasos delegate selected.

CALL "%~dp0setup_usd_env.bat"
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

SET "SCENE_FILE=%~1"
IF "%SCENE_FILE%"=="" SET "SCENE_FILE=%~dp0examples\floor_two_boxes.usda"

IF NOT EXIST "%SCENE_FILE%" (
  ECHO [hdPhantasos] Stage not found: %SCENE_FILE%
  EXIT /B 1
)

ECHO [hdPhantasos] usdview: %SCENE_FILE%
CALL usdview "%SCENE_FILE%" --renderer Phantasos
EXIT /B %ERRORLEVEL%
