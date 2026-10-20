@ECHO OFF
SETLOCAL

REM Render with the hdPhantasos delegate. Accepts normal usdrecord arguments.
REM
REM The render purpose is always requested: a production asset keeps its real
REM geometry under purpose="render" and a stand-in under "proxy", and
REM usdrecord's default purpose would draw the stand-in.
REM
REM Colour correction is sRGB: the colour AOV is linear, made from an sRGB
REM palette, so the sRGB transform gives back exactly the proxy's colours --
REM the same picture as proxy.png in the manifest directory.

CALL "%~dp0setup_usd_env.bat"
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

IF "%~1"=="" (
  ECHO Usage: render_phantasos.bat [usdrecord options] ^<scene.usd^> ^<output.png^>
  ECHO.
  ECHO Settings are taken from the environment. These are the names the
  ECHO delegate reads; see src/hydra/render_delegate.cpp.
  ECHO   HDPHANTASOS_OUTLINE_WIDTH   default 1     outline pixels at 480 rows, 0 for none
  ECHO   HDPHANTASOS_PROXY_SHADING   default 0     1 lights the colours from above left of the camera
  ECHO   HDPHANTASOS_PROXY_CLAY      default 0     1 draws everything in grey clay, lit the same way
  ECHO   HDPHANTASOS_CHECKER_SIZE    default 1     ground checker cell, world units
  ECHO   HDPHANTASOS_MANIFEST        a directory: each frame writes manifest.json
  ECHO                               ^(the subjects and what is seen of them^) and
  ECHO                               proxy.png there
  ECHO   HDPHANTASOS_TRACE           1 prints what is synced and drawn, with timings
  EXIT /B 2
)

CALL usdrecord --renderer Phantasos --purposes render --colorCorrectionMode sRGB %*
EXIT /B %ERRORLEVEL%
