# Building

The delegate needs a C++20 compiler, CMake 3.25+, Ninja and an OpenUSD
install. There is nothing to fetch.

| | Windows | Linux |
|---|---|---|
| Compiler | MSVC 2022 (compile.py enters VsDevCmd when it is not on PATH) | GCC or Clang |
| OpenUSD | probed in `C:\dev\usd-26.03`, `-26.05`, `-25.08`, or `USDROOT` | probed in `~/dev/usd-*`, `/opt/usd-*`, or `USDROOT` |
| Install prefix | `USDEXTRA`, default `%USERPROFILE%\Desktop\usd-26.03-extra` | `USDEXTRA`, default `~/usd-26.03-extra` |
| Environment | `setup_usd_env.bat` | `. ./setup_usd_env.sh` |

The install prefix is shared with hdClaude and hdGemini: plugin discovery is a
union, and `plugin/usd/plugInfo.json` (`"Includes": ["*/resources/"]`) finds
every delegate's resources, and the schema's.

```
python compile.py              # build "dev" and install
python compile.py test         # build, install, run the core and stage tests
python compile.py core-only    # the core and its tests, without OpenUSD
python compile.py clean        # remove build/, then build "dev"
```

The Python in the environment must be the one OpenUSD was built against
(3.12 for 26.03); the setup scripts check it. `HDPHANTASOS_SKIP_PYTHON=1`
skips the check for builds that do not run usdview or usdrecord.

The stage test (`tests/usd/stage_test.py`) needs Pillow in that Python. It
renders through the installed plugin, so it runs after the install step,
which `compile.py test` does in order.

## Linux

The scripts mirror hdClaude's, which builds on the Linux box. hdPhantasos
itself has not been compiled there yet; the first build there is the check.
