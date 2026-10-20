@ECHO OFF
REM ===========================================================================
REM hdPhantasos - configure, build, and install the delegate.
REM
REM Usage:
REM   compile.bat              build the delegate (preset "dev")
REM   compile.bat core-only    build only the dependency-free core and its tests
REM   compile.bat clean        remove the build tree, then build "dev"
REM   compile.bat test         build "dev", then run every test
REM
REM The build itself is compile.py, the one driver on every platform; this
REM forwards to it. setup_usd_env.bat, which compile.py reads, already requires
REM Python on PATH.
REM ===========================================================================

python "%~dp0compile.py" %*
EXIT /B %ERRORLEVEL%
