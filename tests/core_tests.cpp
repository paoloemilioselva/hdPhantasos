// The rasterizer and the proxy look, with neither OpenUSD nor a GPU.

#include "hdphantasos/core/raster.h"
#include "test_support.h"

#include <cmath>
#include <set>

using namespace phantasos;

namespace {

/// OpenGL-style perspective in OpenUSD's row-vector layout, as
/// GfFrustum::ComputeProjectionMatrix produces it.
Matrix4 Perspective(double fovYDegrees, double aspect, double nearPlane, double farPlane)
{
    const double f = 1.0 / std::tan(fovYDegrees * M_PI / 360.0);
    Matrix4 p;
    for (auto& row : p.m) for (double& v : row) v = 0;
    p.m[0][0] = f / aspect;
    p.m[1][1] = f;
    p.m[2][2] = (farPlane + nearPlane) / (nearPlane - farPlane);
    p.m[2][3] = -1;
    p.m[3][2] = 2 * farPlane * nearPlane / (nearPlane - farPlane);
    return p;
}

Camera DefaultCamera(double aspect)
{
    Camera camera;
    camera.projection = Perspective(60, aspect, 0.1, 1000);
    return camera;  // at the origin, looking down -Z
}

/// A square of side 2*half at depth z, facing +Z (the camera), as two triangles.
struct Quad {
    std::vector<float> points;
    std::vector<std::int32_t> triangles = {0, 1, 2, 0, 2, 3};
    Quad(double half, double z, double cx = 0, double cy = 0)
    {
        points = {float(cx - half), float(cy - half), float(z), float(cx + half), float(cy - half), float(z),
                  float(cx + half), float(cy + half), float(z), float(cx - half), float(cy + half), float(z)};
    }
};

DrawItem Item(const Quad& quad, int subject, int primId)
{
    DrawItem item;
    item.points = quad.points.data();
    item.pointCount = quad.points.size() / 3;
    item.triangles = quad.triangles.data();
    item.triangleCount = quad.triangles.size() / 3;
    item.subject = subject;
    item.primId = primId;
    return item;
}

std::size_t At(const Frame& frame, int x, int yFromTop)
{
    return static_cast<std::size_t>(frame.height - 1 - yFromTop) * frame.width + x;
}

bool IsBlack(const Frame& frame, std::size_t p)
{
    return frame.color[p * 4] == 0 && frame.color[p * 4 + 1] == 0 && frame.color[p * 4 + 2] == 0;
}

void TestInvert()
{
    Matrix4 m = Perspective(45, 1.5, 0.5, 200);
    m.m[3][0] = 3;
    Matrix4 inverse;
    CHECK(Invert(m, &inverse));
    const Matrix4 identity = Multiply(m, inverse);
    double error = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) error += std::abs(identity.m[i][j] - (i == j ? 1 : 0));
    CHECK(error < 1e-9);

    Matrix4 singular;
    for (auto& row : singular.m) for (double& v : row) v = 0;
    CHECK(!Invert(singular, &inverse));
}

void TestQuadIdsDepthPosition()
{
    const int w = 96, h = 64;
    Quad quad(1.0, -5.0);
    std::vector<Subject> subjects(1);
    subjects[0].key = "box";
    Frame frame;
    RasterStats stats;
    Rasterize({Item(quad, 0, 7)}, subjects, DefaultCamera(double(w) / h), ProxyStyle{}, w, h,
              &frame, &stats);

    CHECK(frame.width == w && frame.height == h);
    CHECK(stats.triangles == 2);
    const std::size_t centre = At(frame, w / 2, h / 2);
    CHECK(frame.subject[centre] == 0);
    CHECK(frame.primId[centre] == 7);
    CHECK(frame.depth[centre] > 0.0f && frame.depth[centre] < 1.0f);
    CHECK(std::abs(frame.position[centre * 3 + 2] + 5.0f) < 1e-3f);
    CHECK(std::abs(frame.normal[centre * 3 + 2] - 1.0f) < 1e-6f);

    const std::size_t corner = At(frame, 0, 0);
    CHECK(frame.subject[corner] == -1);
    CHECK(frame.depth[corner] == 1.0f);
    CHECK(IsBlack(frame, corner));  // the sky

    // Two triangles sharing a diagonal leave no gap: every pixel inside the
    // quad's middle is covered.
    int holes = 0;
    for (int y = h / 2 - 5; y <= h / 2 + 5; ++y)
        for (int x = w / 2 - 5; x <= w / 2 + 5; ++x)
            if (frame.subject[At(frame, x, y)] != 0) ++holes;
    CHECK(holes == 0);
}

void TestOcclusionAndDeterminism()
{
    const int w = 80, h = 60;
    Quad back(2.0, -8.0), front(0.5, -3.0);
    std::vector<Subject> subjects(2);
    subjects[0].key = "wall";
    subjects[1].key = "urn";
    // The nearer item listed first and last: the answer must not depend on it.
    for (int order = 0; order < 2; ++order) {
        std::vector<DrawItem> items = {Item(back, 0, 1), Item(front, 1, 2)};
        if (order) std::swap(items[0], items[1]);
        Frame frame;
        Rasterize(items, subjects, DefaultCamera(double(w) / h), ProxyStyle{}, w, h, &frame);
        CHECK(frame.subject[At(frame, w / 2, h / 2)] == 1);
        CHECK(frame.subject[At(frame, 2, h / 2)] == 0 || frame.subject[At(frame, 2, h / 2)] == -1);
    }

    std::vector<DrawItem> items = {Item(back, 0, 1), Item(front, 1, 2)};
    Frame a, b;
    Rasterize(items, subjects, DefaultCamera(double(w) / h), ProxyStyle{}, w, h, &a);
    Rasterize(items, subjects, DefaultCamera(double(w) / h), ProxyStyle{}, w, h, &b);
    CHECK(a.color == b.color && a.subject == b.subject && a.depth == b.depth);
}

void TestNearClippingAndChecker()
{
    // A ground plane at y = -1 running from behind the camera to far away: it
    // crosses the near plane, so it has to be clipped, not dropped.
    const int w = 128, h = 72;
    std::vector<float> points = {-50, -1, 10, 50, -1, 10, 50, -1, -100, -50, -1, -100};
    std::vector<std::int32_t> triangles = {0, 1, 2, 0, 2, 3};
    DrawItem ground;
    ground.points = points.data();
    ground.pointCount = 4;
    ground.triangles = triangles.data();
    ground.triangleCount = 2;
    ground.subject = 0;
    ground.primId = 0;

    std::vector<Subject> subjects(1);
    subjects[0].key = "floor";
    subjects[0].role = Role::Ground;
    ProxyStyle style;
    style.outlineWidth = 0;
    Frame frame;
    RasterStats stats;
    Rasterize({ground}, subjects, DefaultCamera(double(w) / h), style, w, h, &frame, &stats);

    CHECK(frame.subject[At(frame, w / 2, h - 1)] == 0);  // bottom: floor
    CHECK(frame.subject[At(frame, w / 2, 0)] == -1);     // top: sky
    CHECK(stats.triangles >= 2);

    // A checkerboard: two greys, and nothing else on the floor.
    std::set<float> greys;
    for (int y = h / 2 + 2; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t p = At(frame, x, y);
            if (frame.subject[p] != 0) continue;
            CHECK(frame.color[p * 4] == frame.color[p * 4 + 1]);
            greys.insert(frame.color[p * 4]);
        }
    }
    CHECK(greys.size() == 2);
}

void TestOutlines()
{
    const int w = 96, h = 96;
    Quad quad(1.0, -5.0);
    std::vector<Subject> subjects(1);
    subjects[0].key = "box";
    Frame inked, plain;
    ProxyStyle none;
    none.outlineWidth = 0;
    Rasterize({Item(quad, 0, 1)}, subjects, DefaultCamera(1.0), ProxyStyle{}, w, h, &inked);
    Rasterize({Item(quad, 0, 1)}, subjects, DefaultCamera(1.0), none, w, h, &plain);

    // Find the quad's leftmost covered pixel on the middle row.
    int left = -1;
    for (int x = 0; x < w; ++x) {
        if (inked.subject[At(inked, x, h / 2)] == 0) {
            left = x;
            break;
        }
    }
    CHECK(left > 0);
    CHECK(IsBlack(inked, At(inked, left, h / 2)));       // the edge is inked
    CHECK(!IsBlack(inked, At(inked, w / 2, h / 2)));     // the middle is not
    CHECK(!IsBlack(plain, At(plain, left, h / 2)));      // and without outlines,
    CHECK(inked.subject == plain.subject);               // ids are unchanged
}

void TestFacingAndManifest()
{
    const int w = 64, h = 64;
    Quad quad(1.0, -5.0);
    std::vector<Subject> subjects(2);
    subjects[0].key = "chest";
    subjects[0].description = "an old \"oak\" chest\nbound in iron";
    subjects[0].hasFront = true;
    subjects[1].key = "ghost";  // not drawn by anything

    DrawItem toward = Item(quad, 0, 1);
    Frame frame;
    Camera camera = DefaultCamera(1.0);
    Rasterize({toward}, subjects, camera, ProxyStyle{}, w, h, &frame);
    auto views = MeasureSubjects({toward}, subjects, camera, frame);
    CHECK(views.size() == 2);
    CHECK(views[0].pixels > 0);
    CHECK(std::string(views[0].facing) == "toward the camera");
    CHECK(std::abs(views[0].centroid[0] - 0.5f) < 0.02f);
    CHECK(std::abs(views[0].nearest - 5.0f) < 0.01f);
    CHECK(views[1].pixels == 0);

    // Turned half way round about Y: its front now points away.
    DrawItem away = toward;
    away.objectToWorld.m[0][0] = -1;
    away.objectToWorld.m[2][2] = -1;
    auto turned = MeasureSubjects({away}, subjects, camera, frame);
    CHECK(std::string(turned[0].facing) == "away from the camera");

    const std::string json = ManifestJson(subjects, views, camera, w, h);
    CHECK(json.find("\\\"oak\\\" chest\\nbound") != std::string::npos);
    CHECK(json.find("\"visible\": false") != std::string::npos);
    CHECK(json.find("\"facing\": \"toward the camera\"") != std::string::npos);
}

void TestProxyRgb8IsTopFirst()
{
    // A quad in the upper half of the view shows in the first rows of the
    // 8-bit image, which is stored top row first.
    const int w = 32, h = 32;
    Quad quad(0.5, -3.0, 0.0, 1.0);
    std::vector<Subject> subjects(1);
    subjects[0].key = "lamp";
    subjects[0].hasColor = true;
    subjects[0].color[0] = 1.0f;
    ProxyStyle style;
    style.outlineWidth = 0;
    Frame frame;
    Rasterize({Item(quad, 0, 1)}, subjects, DefaultCamera(1.0), style, w, h, &frame);
    const auto rgb = ProxyRgb8(frame);
    int topRed = 0, bottomRed = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (rgb[(static_cast<std::size_t>(y) * w + x) * 3] == 255) (y < h / 2 ? topRed : bottomRed)++;
    CHECK(topRed > 0);
    CHECK(bottomRed == 0);
}

void TestSubjectColors()
{
    Subject a, b;
    a.key = "chest";
    b.key = "urn";
    float ca[3], cb[3], again[3];
    SubjectColor(a, ca);
    SubjectColor(b, cb);
    SubjectColor(a, again);
    CHECK(ca[0] == again[0] && ca[1] == again[1] && ca[2] == again[2]);
    CHECK(ca[0] != cb[0] || ca[1] != cb[1] || ca[2] != cb[2]);
    // Never grey, never dark: neither ground, sky nor outline.
    const float spread = std::max({ca[0], ca[1], ca[2]}) - std::min({ca[0], ca[1], ca[2]});
    CHECK(spread > 0.2f);
    CHECK(std::max({ca[0], ca[1], ca[2]}) >= 0.7f);
}

}  // namespace

int main()
{
    TestInvert();
    TestQuadIdsDepthPosition();
    TestOcclusionAndDeterminism();
    TestNearClippingAndChecker();
    TestOutlines();
    TestFacingAndManifest();
    TestProxyRgb8IsTopFirst();
    TestSubjectColors();
    return phantasos_test::Finish("core");
}
