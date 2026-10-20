# PhantasosAPI

A codeless, single-apply API schema that declares the `primvars:phantasos:*`
properties the delegate reads. `schema.usda` is the source;
`generatedSchema.usda` and `plugInfo.json` are usdGenSchema's output and are
committed, so a build needs no Python.

The schema is optional. The delegate reads primvars, which reach it with or
without the schema loaded; the schema gives those primvars a type, a
fallback, documentation and a place in a host's UI.

## Regenerate after editing schema.usda

From a shell with the OpenUSD environment (`setup_usd_env.bat` or `.sh`):

```
cd schema
usdGenSchema schema.usda .
```

usdGenSchema warns that `__init__.py`, `CMakeLists.txt` and `module.cpp` are
missing. Those are for compiled schemas; a codeless one does not use them, and
`CMakeLists.txt` here is ours. Check that it did not overwrite it.
