# ---------------------------------------------------------------------------
# phantasosUsdview: the usdview plugin (Phantasos > Parameters...).
#
# Nothing is compiled. The package goes where setup_usd_env puts Python
# (<prefix>/lib/python) and its plugInfo.json beside the delegate's, where
# plugin/usd/plugInfo.json's "*/resources/" include finds it:
#
#   lib/python/phantasosUsdview/*.py
#   plugin/usd/phantasosUsdview/resources/plugInfo.json
# ---------------------------------------------------------------------------

install(FILES plugInfo.json DESTINATION "plugin/usd/phantasosUsdview/resources")
install(DIRECTORY phantasosUsdview DESTINATION "lib/python"
        FILES_MATCHING PATTERN "*.py"
        PATTERN "__pycache__" EXCLUDE)
