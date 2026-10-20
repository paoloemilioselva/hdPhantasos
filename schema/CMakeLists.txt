# ---------------------------------------------------------------------------
# phantasosSchema: the codeless PhantasosAPI schema.
#
# Nothing is compiled. usdGenSchema's plugInfo.json has placeholders, which
# configure_file fills (they are written in its own @VAR@ syntax), and the
# result is laid out as a resource plugin beside the delegate:
#
#   plugin/usd/phantasosSchema/resources/plugInfo.json
#   plugin/usd/phantasosSchema/resources/generatedSchema.usda
#
# plugin/usd/plugInfo.json's "*/resources/" include finds it, as it finds the
# delegate's own.
# ---------------------------------------------------------------------------

set(PLUG_INFO_LIBRARY_PATH "")
set(PLUG_INFO_RESOURCE_PATH "resources")
set(PLUG_INFO_ROOT "..")

set(_schema_resources "${CMAKE_BINARY_DIR}/plugin/usd/phantasosSchema/resources")
configure_file(plugInfo.json "${_schema_resources}/plugInfo.json" @ONLY)
configure_file(generatedSchema.usda "${_schema_resources}/generatedSchema.usda" COPYONLY)

install(FILES
    "${_schema_resources}/plugInfo.json"
    "${_schema_resources}/generatedSchema.usda"
    DESTINATION "plugin/usd/phantasosSchema/resources")
