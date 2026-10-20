"""The Phantasos parameters of prims: what there is, what a prim has, and
how to add, change and remove them. No Qt here, so it can be tested without
usdview.

The parameters are the PhantasosAPI schema's properties, read from the
schema registry with their types, fallbacks, allowed values and
documentation, so a property added to schema/schema.usda appears in the
editor without changing it. They are constant primvars: a value authored on
a group's Xform is inherited by every mesh under it, and the editor says
where an inherited value comes from.
"""

from pxr import Gf, Sdf, Usd, UsdGeom

SCHEMA = "PhantasosAPI"
PREFIX = "primvars:"
# The schema registry lists properties alphabetically; this is the order a
# person fills them in. Properties not named here follow, alphabetically.
ORDER = ["phantasos:subject", "phantasos:description", "phantasos:role", "phantasos:hasFront",
         "phantasos:reference", "phantasos:proxyColor", "phantasos:shot"]
GROUPS = ["Subject", "Proxy", "Shot"]


class Parameter:
    def __init__(self, name, type_name, fallback, doc, display_name, group, allowed):
        self.name = name                  # primvar name: "phantasos:description"
        self.attribute = PREFIX + name    # "primvars:phantasos:description"
        self.type_name = type_name        # Sdf.ValueTypeName
        self.fallback = fallback
        self.doc = doc
        self.display_name = display_name
        self.group = group
        self.allowed = allowed            # allowed tokens, or None

    @property
    def kind(self):
        """What editor the value needs: bool, token, string, text, asset, color."""
        if self.type_name == Sdf.ValueTypeNames.Bool:
            return "bool"
        if self.type_name == Sdf.ValueTypeNames.Token:
            return "token"
        if self.type_name == Sdf.ValueTypeNames.Asset:
            return "asset"
        if self.type_name in (Sdf.ValueTypeNames.Color3f, Sdf.ValueTypeNames.Float3):
            return "color"
        if self.name in ("phantasos:description", "phantasos:shot"):
            return "text"
        return "string"


def parameters():
    """Every PhantasosAPI parameter, in editing order. Empty when the schema
    is not registered (phantasosSchema not on PXR_PLUGINPATH_NAME)."""
    definition = Usd.SchemaRegistry().FindAppliedAPIPrimDefinition(SCHEMA)
    if not definition:
        return []
    found = []
    for attribute_name in definition.GetPropertyNames():
        if not attribute_name.startswith(PREFIX):
            continue
        spec = definition.GetAttributeDefinition(attribute_name)
        if not spec:
            continue
        name = attribute_name[len(PREFIX):]
        type_name = Sdf.ValueTypeNames.Find(str(spec.GetTypeName()))
        allowed = spec.GetMetadata("allowedTokens")
        found.append(Parameter(
            name, type_name, spec.GetFallbackValue(), spec.GetDocumentation() or "",
            spec.GetMetadata("displayName") or name.split(":")[-1],
            spec.GetMetadata("displayGroup") or "Subject",
            [str(t) for t in allowed] if allowed else None))
    rank = {name: i for i, name in enumerate(ORDER)}
    found.sort(key=lambda p: (rank.get(p.name, len(ORDER)), p.name))
    return found


def schema_doc():
    definition = Usd.SchemaRegistry().FindAppliedAPIPrimDefinition(SCHEMA)
    return definition.GetDocumentation() if definition else ""


# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------

class Reading:
    """A parameter across the selected prims.

    value     the value, when every prim has the same one (else None)
    mixed     the prims have different values
    authored  how many prims author it themselves
    origin    'here' (all author it), 'inherited' (all inherit it from the
              same prim, `source`), 'fallback' (no prim has it), 'mixed'
    """

    def __init__(self, value, mixed, authored, total, origin, source):
        self.value, self.mixed, self.authored, self.total = value, mixed, authored, total
        self.origin, self.source = origin, source


def _read_one(prim, parameter):
    attribute = prim.GetAttribute(parameter.attribute)
    if attribute and attribute.HasAuthoredValue():
        return attribute.Get(), "here", prim.GetPath()
    primvar = UsdGeom.PrimvarsAPI(prim).FindPrimvarWithInheritance(parameter.name)
    if primvar and primvar.HasAuthoredValue():
        return primvar.Get(), "inherited", primvar.GetAttr().GetPrim().GetPath()
    return parameter.fallback, "fallback", None


def read(prims, parameter):
    if not prims:
        return Reading(None, False, 0, 0, "fallback", None)
    results = [_read_one(prim, parameter) for prim in prims]
    values = [_comparable(v) for v, _, _ in results]
    mixed = any(v != values[0] for v in values[1:])
    authored = sum(1 for _, origin, _ in results if origin == "here")
    origins = {(origin, source if origin == "inherited" else None) for _, origin, source in results}
    if len(origins) == 1:
        origin, source = next(iter(origins))
    else:
        origin, source = "mixed", None
    return Reading(None if mixed else results[0][0], mixed, authored, len(prims), origin, source)


def _comparable(value):
    if isinstance(value, Sdf.AssetPath):
        return value.path
    if isinstance(value, Gf.Vec3f):
        return tuple(round(c, 6) for c in value)
    return value


def has_schema(prim):
    return SCHEMA in prim.GetAppliedSchemas()


# ---------------------------------------------------------------------------
# Writing: always into the stage's current edit target
# ---------------------------------------------------------------------------

def convert(parameter, value):
    """A value from the editor as the attribute's type."""
    kind = parameter.kind
    if kind == "bool":
        return bool(value)
    if kind == "asset":
        return Sdf.AssetPath(value.path if isinstance(value, Sdf.AssetPath) else str(value or ""))
    if kind == "color":
        return Gf.Vec3f(*[float(c) for c in value])
    return str(value or "")


def set_value(prims, parameter, value):
    """Author `value` on every prim, applying PhantasosAPI where missing."""
    value = convert(parameter, value)
    for prim in prims:
        if not has_schema(prim):
            prim.AddAppliedSchema(SCHEMA)
        primvar = UsdGeom.PrimvarsAPI(prim).CreatePrimvar(
            parameter.name, parameter.type_name, UsdGeom.Tokens.constant)
        primvar.Set(value)


def remove(prims, parameter):
    """Remove the parameter from every prim, in the edit target. Returns the
    prims that still author it, from a layer the edit target cannot change."""
    left = []
    for prim in prims:
        if prim.GetAttribute(parameter.attribute):
            prim.RemoveProperty(parameter.attribute)
        attribute = prim.GetAttribute(parameter.attribute)
        if attribute and attribute.HasAuthoredValue():
            left.append(prim)
    return left


def remove_all(prims, every):
    """Remove every parameter and PhantasosAPI itself from the prims."""
    left = set()
    for parameter in every:
        left.update(p.GetPath() for p in remove(prims, parameter))
    for prim in prims:
        if has_schema(prim):
            prim.RemoveAppliedSchema(SCHEMA)
    return sorted(left)
