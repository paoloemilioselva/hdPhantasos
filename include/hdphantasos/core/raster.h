#pragma once

// The CPU rasterizer and the proxy look.
//
// No OpenUSD here: the Hydra adapter flattens the stage into DrawItems and
// Subjects, and everything below is testable with a compiler alone. That is
// what the core-only preset is for.
//
// Conventions are OpenUSD's, so nothing is converted at the seam:
//   - matrices are row-major and transform row vectors, p' = p * M;
//   - the projection is OpenGL-style, clip z in [-w, w];
//   - image rows are stored bottom-up (row 0 is the bottom of the picture),
//     as Hydra render buffers are.

#include <cstdint>
#include <string>
#include <vector>

namespace phantasos {

struct Matrix4 {
    double m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
};

/// `a` then `b`: a point p goes to p * a * b.
Matrix4 Multiply(const Matrix4& a, const Matrix4& b);

/// False, leaving `out` untouched, when `in` is singular.
bool Invert(const Matrix4& in, Matrix4* out);

/// What part an object plays in the picture, and so how the proxy draws it
/// and how a prompt describes it.
enum class Role : std::uint8_t {
    Subject,   // a thing in the scene: drawn in its own flat colour
    Ground,    // what things stand on: drawn as a grey checkerboard
    Backdrop,  // walls, sky domes, distant set: flat colour, described last
    Hero,      // the subject the shot follows: drawn like a subject, described first
};

const char* RoleName(Role role);

/// One described thing. Several meshes share a subject when they carry the
/// same `phantasos:subject` key, which is how the parts of a chest (body,
/// lid, handles) become one chest.
struct Subject {
    std::string key;          // phantasos:subject, or the mesh path when unset
    std::string description;  // phantasos:description, may be empty
    std::string reference;    // resolved phantasos:reference, may be empty
    Role role = Role::Subject;
    bool hasFront = false;    // +Z of the subject is its front
    bool hasColor = false;    // phantasos:proxyColor authored
    float color[3] = {0, 0, 0};  // sRGB, 0..1, when hasColor
};

/// One placement of one mesh: geometry in object space plus where it goes.
///
/// The arrays are borrowed, not owned: the Hydra adapter keeps them alive
/// for the duration of a Rasterize call.
struct DrawItem {
    const float* points = nullptr;           // xyz per point
    std::size_t pointCount = 0;
    const std::int32_t* triangles = nullptr; // three point indices each
    std::size_t triangleCount = 0;
    const std::int32_t* faces = nullptr;     // coarse face per triangle, optional
    Matrix4 objectToWorld;
    std::int32_t subject = -1;               // index into the subject list
    std::int32_t primId = -1;
    std::int32_t instanceId = -1;
};

struct Camera {
    Matrix4 worldToView;
    Matrix4 projection;
};

/// How surfaces are coloured and lit.
enum class Look : std::uint8_t {
    /// Matrix-Game 4.0's training proxies: flat colour per subject, unlit.
    Flat,
    /// The same colours, lit by one light fixed to the camera, above and to
    /// the left, so the top, the front and the side of a box differ.
    Shaded,
    /// One grey material on everything, ground included, under the same
    /// light: a clay render, for models that read form rather than identity.
    Clay,
};

const char* LookName(Look look);

/// How the proxy is drawn. Defaults follow Matrix-Game 4.0's training proxies:
/// flat colour per object, black outlines, grey checker ground, black sky.
struct ProxyStyle {
    /// Outline width in pixels for a 480-row image, scaled with the image
    /// height so the guide looks the same at any size. 0 draws none.
    float outlineWidth = 1.0f;
    Look look = Look::Flat;
    /// Ground checker cell size, in world units.
    float checkerSize = 1.0f;
};

/// Everything one rasterization produces, one value per pixel, bottom-up rows.
struct Frame {
    int width = 0;
    int height = 0;
    std::vector<float> depth;            // window depth in [0,1], 1 where empty
    std::vector<std::int32_t> subject;   // -1 where empty
    std::vector<std::int32_t> primId;    // -1 where empty
    std::vector<std::int32_t> instanceId;
    std::vector<std::int32_t> elementId; // coarse face, -1 where empty
    std::vector<float> normal;           // world space, facing the camera, xyz
    std::vector<float> position;         // world space, xyz
    std::vector<float> color;            // the proxy, linear RGBA
};

struct RasterStats {
    std::size_t triangles = 0;        // after near-plane clipping
    std::size_t culledTriangles = 0;  // entirely behind the camera
    double milliseconds = 0.0;
};

/// Draws the items and the proxy look into `frame`, resizing it to
/// width x height.
void Rasterize(const std::vector<DrawItem>& items,
               const std::vector<Subject>& subjects, const Camera& camera,
               const ProxyStyle& style, int width, int height, Frame* frame,
               RasterStats* stats = nullptr);

/// The colour a subject is drawn in, sRGB 0..1: its authored proxyColor, or
/// one derived from its key, so it is the same in every frame and session.
void SubjectColor(const Subject& subject, float rgb[3]);

/// What a frame shows of each subject, for the prompt writer.
struct SubjectView {
    std::int32_t subject = -1;
    std::uint64_t pixels = 0;
    float coverage = 0.0f;          // fraction of the image
    float bbox[4] = {0, 0, 0, 0};   // x0, y0, x1, y1; 0..1, top-left origin
    float centroid[2] = {0, 0};     // 0..1, top-left origin
    float nearest = 0.0f;           // distance from the camera, world units
    float mean = 0.0f;
    float position[3] = {0, 0, 0};  // mean world position of the visible pixels
    bool hasFacing = false;
    float front[3] = {0, 0, 0};     // the subject's +Z in view space
    const char* facing = "";        // "toward the camera", "away", "left", "right"
};

/// One entry per subject, in subject order, including those not in view
/// (pixels == 0).
std::vector<SubjectView> MeasureSubjects(const std::vector<DrawItem>& items,
                                         const std::vector<Subject>& subjects,
                                         const Camera& camera,
                                         const Frame& frame);

/// The frame's subjects and what is seen of them, as JSON: what the worker
/// turns into a prompt. `shot` describes the whole picture (time of day,
/// weather, style), from the stage's phantasos:shot.
std::string ManifestJson(const std::vector<Subject>& subjects,
                         const std::vector<SubjectView>& views,
                         const Camera& camera, int width, int height,
                         const std::string& shot = {});

/// The proxy as 8-bit sRGB, top row first: the guide a model is given.
std::vector<std::uint8_t> ProxyRgb8(const Frame& frame);

}  // namespace phantasos
