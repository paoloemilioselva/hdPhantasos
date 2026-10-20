#pragma once

#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_OPEN_SCOPE

/// Spelled exactly as the display name in resources/plugInfo.json, which is
/// what usdview's renderer menu, usdrecord --renderer and the scene index
/// plugin registry all match on.
inline constexpr const char* kHdPhantasosDisplayName = "Phantasos";

// clang-format off
#define HDPHANTASOS_TOKENS                                              \
    /* Primvars that describe a stage to the delegate (schema/). */     \
    ((subject,        "phantasos:subject"))                             \
    ((description,    "phantasos:description"))                         \
    ((role,           "phantasos:role"))                                \
    ((hasFront,       "phantasos:hasFront"))                            \
    ((reference,      "phantasos:reference"))                           \
    ((proxyColor,     "phantasos:proxyColor"))                          \
    ((shot,           "phantasos:shot"))                                \
    /* Role values. */                                                  \
    (hero)                                                              \
    (ground)                                                            \
    (backdrop)                                                          \
    /* Render settings. */                                              \
    ((outlineWidth,   "phantasos:outlineWidth"))                        \
    ((proxyShading,   "phantasos:proxyShading"))                        \
    ((proxyClay,      "phantasos:proxyClay"))                           \
    ((checkerSize,    "phantasos:checkerSize"))                         \
    ((generate,       "phantasos:generate"))                            \
    ((model,          "phantasos:model"))                               \
    ((generateSize,   "phantasos:generateSize"))                        \
    ((seed,           "phantasos:seed"))                                \
    ((style,          "phantasos:style"))                               \
    ((firstFrame,     "phantasos:firstFrame"))                          \
    ((allowDownload,  "phantasos:allowDownload"))                       \
    /* An AOV of our own: the subject each pixel shows. */              \
    (subjectId)
// clang-format on

TF_DECLARE_PUBLIC_TOKENS(HdPhantasosTokens, HDPHANTASOS_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE
