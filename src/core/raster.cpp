#include "hdphantasos/core/raster.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>

namespace phantasos {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

namespace {

/// Runs fn(i) for i in [0, count) on every hardware thread. Threads are made
/// per call: a frame makes two calls, and at a few hundred microseconds that
/// is noise beside the work, without a pool to own and shut down.
template <class Fn>
void ParallelFor(std::size_t count, Fn&& fn)
{
    const std::size_t hardware =
        std::max<std::size_t>(1, std::thread::hardware_concurrency());
    const std::size_t workers = std::min(hardware, count);
    if (workers <= 1) {
        for (std::size_t i = 0; i < count; ++i) fn(i);
        return;
    }
    std::atomic<std::size_t> next{0};
    auto work = [&] {
        for (std::size_t i; (i = next.fetch_add(1)) < count;) fn(i);
    };
    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (std::size_t t = 1; t < workers; ++t) threads.emplace_back(work);
    work();
    for (std::thread& thread : threads) thread.join();
}

struct Vec4 {
    double x, y, z, w;
};

Vec4 TransformPoint(const double p[3], const Matrix4& m)
{
    Vec4 r;
    r.x = p[0] * m.m[0][0] + p[1] * m.m[1][0] + p[2] * m.m[2][0] + m.m[3][0];
    r.y = p[0] * m.m[0][1] + p[1] * m.m[1][1] + p[2] * m.m[2][1] + m.m[3][1];
    r.z = p[0] * m.m[0][2] + p[1] * m.m[1][2] + p[2] * m.m[2][2] + m.m[3][2];
    r.w = p[0] * m.m[0][3] + p[1] * m.m[1][3] + p[2] * m.m[2][3] + m.m[3][3];
    return r;
}

void TransformVector(const double v[3], const Matrix4& m, double out[3])
{
    for (int j = 0; j < 3; ++j) {
        out[j] = v[0] * m.m[0][j] + v[1] * m.m[1][j] + v[2] * m.m[2][j];
    }
}

/// A clip-space point back to world space through the inverse of
/// world-to-clip, with the perspective divide.
void Unproject(const Matrix4& clipToWorld, double x, double y, double z,
               double out[3])
{
    const double p[3] = {x, y, z};
    const Vec4 r = TransformPoint(p, clipToWorld);
    const double w = std::abs(r.w) > 1e-300 ? r.w : 1e-300;
    out[0] = r.x / w;
    out[1] = r.y / w;
    out[2] = r.z / w;
}

double Dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void Normalize(double v[3])
{
    const double length = std::sqrt(Dot(v, v));
    if (length > 0) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

float SrgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float c)
{
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

std::uint64_t Fnv1a(const std::string& text)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

void HsvToRgb(float h, float s, float v, float rgb[3])
{
    const float sector = h * 6.0f;
    const int i = static_cast<int>(std::floor(sector)) % 6;
    const float f = sector - std::floor(sector);
    const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    const float table[6][3] = {{v, t, p}, {q, v, p}, {p, v, t},
                               {p, q, v}, {t, p, v}, {v, p, q}};
    rgb[0] = table[i][0];
    rgb[1] = table[i][1];
    rgb[2] = table[i][2];
}

/// One source triangle: what a pixel that shows it reports.
struct TriangleRecord {
    double normal[3];  // world space, unit
    double offset;     // plane: dot(normal, x) == offset
    std::int32_t subject, primId, instanceId, element;
};

/// One triangle on screen, after near-plane clipping. Several can share a
/// record when clipping cut a triangle in two.
struct ScreenTriangle {
    double x[3], y[3];  // pixels, bottom-up
    float z[3];         // window depth
    std::uint32_t record;
};

/// Clips a clip-space polygon against the near plane, z >= -w.
int ClipNear(const Vec4* in, int count, Vec4* out)
{
    int written = 0;
    for (int i = 0; i < count; ++i) {
        const Vec4& a = in[i];
        const Vec4& b = in[(i + 1) % count];
        const double da = a.z + a.w;
        const double db = b.z + b.w;
        if (da >= 0) out[written++] = a;
        if ((da >= 0) != (db >= 0)) {
            const double t = da / (da - db);
            out[written++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                              a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        }
    }
    return written;
}

constexpr int kBandHeight = 16;

}  // namespace

// ---------------------------------------------------------------------------
// Matrices
// ---------------------------------------------------------------------------

Matrix4 Multiply(const Matrix4& a, const Matrix4& b)
{
    Matrix4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double sum = 0;
            for (int k = 0; k < 4; ++k) sum += a.m[i][k] * b.m[k][j];
            r.m[i][j] = sum;
        }
    }
    return r;
}

bool Invert(const Matrix4& in, Matrix4* out)
{
    double a[4][8];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[i][j] = in.m[i][j];
            a[i][j + 4] = i == j ? 1.0 : 0.0;
        }
    }
    for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row) {
            if (std::abs(a[row][column]) > std::abs(a[pivot][column])) pivot = row;
        }
        if (std::abs(a[pivot][column]) < 1e-300) return false;
        if (pivot != column) {
            for (int j = 0; j < 8; ++j) std::swap(a[pivot][j], a[column][j]);
        }
        const double scale = 1.0 / a[column][column];
        for (int j = 0; j < 8; ++j) a[column][j] *= scale;
        for (int row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = a[row][column];
            if (factor == 0) continue;
            for (int j = 0; j < 8; ++j) a[row][j] -= factor * a[column][j];
        }
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) out->m[i][j] = a[i][j + 4];
    }
    return true;
}

const char* LookName(Look look)
{
    switch (look) {
        case Look::Shaded: return "shaded";
        case Look::Clay: return "clay";
        case Look::Flat: break;
    }
    return "flat";
}

const char* RoleName(Role role)
{
    switch (role) {
        case Role::Ground: return "ground";
        case Role::Backdrop: return "backdrop";
        case Role::Hero: return "hero";
        case Role::Subject: break;
    }
    return "subject";
}

void SubjectColor(const Subject& subject, float rgb[3])
{
    if (subject.hasColor) {
        rgb[0] = subject.color[0];
        rgb[1] = subject.color[1];
        rgb[2] = subject.color[2];
        return;
    }
    // Saturated and bright enough never to read as the grey ground, the black
    // sky or a black outline, and fixed by the key alone, so a subject keeps
    // its colour from frame to frame and from one session to the next.
    const std::uint64_t hash = Fnv1a(subject.key);
    const float hue = static_cast<float>(hash % 3600) / 3600.0f;
    const float saturation = 0.45f + 0.1f * static_cast<float>((hash >> 20) % 3);
    const float value = 0.75f + 0.1f * static_cast<float>((hash >> 32) % 3);
    HsvToRgb(hue, saturation, value, rgb);
}

// ---------------------------------------------------------------------------
// Rasterize
// ---------------------------------------------------------------------------

void Rasterize(const std::vector<DrawItem>& items,
               const std::vector<Subject>& subjects, const Camera& camera,
               const ProxyStyle& style, int width, int height, Frame* frame,
               RasterStats* stats)
{
    const auto start = std::chrono::steady_clock::now();
    width = std::max(width, 0);
    height = std::max(height, 0);
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;

    frame->width = width;
    frame->height = height;
    frame->depth.assign(pixelCount, 1.0f);
    frame->subject.assign(pixelCount, -1);
    frame->primId.assign(pixelCount, -1);
    frame->instanceId.assign(pixelCount, -1);
    frame->elementId.assign(pixelCount, -1);
    frame->normal.assign(pixelCount * 3, 0.0f);
    frame->position.assign(pixelCount * 3, 0.0f);
    frame->color.assign(pixelCount * 4, 0.0f);
    for (std::size_t i = 0; i < pixelCount; ++i) frame->color[i * 4 + 3] = 1.0f;
    if (pixelCount == 0) return;

    const Matrix4 worldToClip = Multiply(camera.worldToView, camera.projection);
    Matrix4 clipToWorld;
    if (!Invert(worldToClip, &clipToWorld)) return;

    // --- Setup: every triangle to world and to screen, per item in parallel.
    std::vector<std::vector<TriangleRecord>> itemRecords(items.size());
    std::vector<std::vector<ScreenTriangle>> itemScreen(items.size());
    std::vector<std::size_t> itemCulled(items.size(), 0);

    ParallelFor(items.size(), [&](std::size_t itemIndex) {
        const DrawItem& item = items[itemIndex];
        if (!item.points || !item.triangles) return;
        const Matrix4 objectToClip = Multiply(item.objectToWorld, worldToClip);
        auto& records = itemRecords[itemIndex];
        auto& screen = itemScreen[itemIndex];
        records.reserve(item.triangleCount);
        screen.reserve(item.triangleCount);

        for (std::size_t t = 0; t < item.triangleCount; ++t) {
            double world[3][3];
            Vec4 clip[3];
            bool valid = true;
            for (int v = 0; v < 3; ++v) {
                const std::int32_t index = item.triangles[t * 3 + v];
                if (index < 0 || static_cast<std::size_t>(index) >= item.pointCount) {
                    valid = false;
                    break;
                }
                const double p[3] = {item.points[index * 3 + 0],
                                     item.points[index * 3 + 1],
                                     item.points[index * 3 + 2]};
                const Vec4 w = TransformPoint(p, item.objectToWorld);
                world[v][0] = w.x;
                world[v][1] = w.y;
                world[v][2] = w.z;
                clip[v] = TransformPoint(p, objectToClip);
            }
            if (!valid) continue;

            const double e1[3] = {world[1][0] - world[0][0], world[1][1] - world[0][1],
                                  world[1][2] - world[0][2]};
            const double e2[3] = {world[2][0] - world[0][0], world[2][1] - world[0][1],
                                  world[2][2] - world[0][2]};
            TriangleRecord record;
            record.normal[0] = e1[1] * e2[2] - e1[2] * e2[1];
            record.normal[1] = e1[2] * e2[0] - e1[0] * e2[2];
            record.normal[2] = e1[0] * e2[1] - e1[1] * e2[0];
            if (Dot(record.normal, record.normal) <= 0) continue;  // degenerate
            Normalize(record.normal);
            record.offset = Dot(record.normal, world[0]);
            record.subject = item.subject;
            record.primId = item.primId;
            record.instanceId = item.instanceId;
            record.element = item.faces ? item.faces[t] : static_cast<std::int32_t>(t);

            Vec4 polygon[4];
            const int corners = ClipNear(clip, 3, polygon);
            if (corners < 3) {
                ++itemCulled[itemIndex];
                continue;
            }
            const auto recordIndex = static_cast<std::uint32_t>(records.size());
            records.push_back(record);

            // A fan over the clipped polygon: one triangle, or two when the
            // near plane cut a corner off.
            for (int k = 1; k + 1 < corners; ++k) {
                const Vec4* v[3] = {&polygon[0], &polygon[k], &polygon[k + 1]};
                ScreenTriangle s;
                for (int c = 0; c < 3; ++c) {
                    const double w = v[c]->w > 1e-12 ? v[c]->w : 1e-12;
                    s.x[c] = (v[c]->x / w * 0.5 + 0.5) * width;
                    s.y[c] = (v[c]->y / w * 0.5 + 0.5) * height;
                    s.z[c] = static_cast<float>(v[c]->z / w * 0.5 + 0.5);
                }
                s.record = recordIndex;
                screen.push_back(s);
            }
        }
    });

    // --- Flatten into one table, record indices made global.
    std::vector<TriangleRecord> records;
    std::vector<ScreenTriangle> screen;
    std::size_t culled = 0;
    {
        std::size_t recordTotal = 0, screenTotal = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            recordTotal += itemRecords[i].size();
            screenTotal += itemScreen[i].size();
            culled += itemCulled[i];
        }
        records.reserve(recordTotal);
        screen.reserve(screenTotal);
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto base = static_cast<std::uint32_t>(records.size());
            records.insert(records.end(), itemRecords[i].begin(), itemRecords[i].end());
            for (ScreenTriangle s : itemScreen[i]) {
                s.record += base;
                screen.push_back(s);
            }
        }
    }

    // --- Bin by horizontal bands, so each band is one thread's alone and no
    // pixel is ever written by two threads.
    const int bandCount = (height + kBandHeight - 1) / kBandHeight;
    std::vector<std::uint32_t> bandOffsets(bandCount + 1, 0);
    std::vector<std::uint32_t> binned;
    {
        auto bandsOf = [&](const ScreenTriangle& s, int* first, int* last) {
            const double minY = std::min({s.y[0], s.y[1], s.y[2]});
            const double maxY = std::max({s.y[0], s.y[1], s.y[2]});
            if (maxY < 0 || minY >= height) return false;
            const double minX = std::min({s.x[0], s.x[1], s.x[2]});
            const double maxX = std::max({s.x[0], s.x[1], s.x[2]});
            if (maxX < 0 || minX >= width) return false;
            *first = std::clamp(static_cast<int>(std::floor(minY)), 0, height - 1) / kBandHeight;
            *last = std::clamp(static_cast<int>(std::floor(maxY)), 0, height - 1) / kBandHeight;
            return true;
        };
        for (const ScreenTriangle& s : screen) {
            int first, last;
            if (!bandsOf(s, &first, &last)) continue;
            for (int b = first; b <= last; ++b) ++bandOffsets[b + 1];
        }
        for (int b = 0; b < bandCount; ++b) bandOffsets[b + 1] += bandOffsets[b];
        binned.resize(bandOffsets[bandCount]);
        std::vector<std::uint32_t> cursor(bandOffsets.begin(), bandOffsets.end() - 1);
        for (std::size_t i = 0; i < screen.size(); ++i) {
            int first, last;
            if (!bandsOf(screen[i], &first, &last)) continue;
            for (int b = first; b <= last; ++b) {
                binned[cursor[b]++] = static_cast<std::uint32_t>(i);
            }
        }
    }

    // --- Rasterize each band: depth test, keep the winning record.
    std::vector<std::int32_t> winner(pixelCount, -1);
    ParallelFor(static_cast<std::size_t>(bandCount), [&](std::size_t band) {
        const int rowBegin = static_cast<int>(band) * kBandHeight;
        const int rowEnd = std::min(height, rowBegin + kBandHeight);
        for (std::uint32_t k = bandOffsets[band]; k < bandOffsets[band + 1]; ++k) {
            const ScreenTriangle& s = screen[binned[k]];
            // Edge functions in double: after near clipping a vertex can sit
            // thousands of pixels off screen, where float would leave cracks.
            double area = (s.x[1] - s.x[0]) * (s.y[2] - s.y[0]) -
                          (s.y[1] - s.y[0]) * (s.x[2] - s.x[0]);
            if (area == 0) continue;
            const double sign = area > 0 ? 1.0 : -1.0;
            area *= sign;

            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({s.x[0], s.x[1], s.x[2]}))));
            const int x1 = std::min(width - 1, static_cast<int>(std::floor(std::max({s.x[0], s.x[1], s.x[2]}))));
            const int y0 = std::max(rowBegin, static_cast<int>(std::floor(std::min({s.y[0], s.y[1], s.y[2]}))));
            const int y1 = std::min(rowEnd - 1, static_cast<int>(std::floor(std::max({s.y[0], s.y[1], s.y[2]}))));
            if (x0 > x1 || y0 > y1) continue;

            // E_i is the edge opposite vertex i; it is vertex i's barycentric
            // weight times the area.
            const auto edge = [&](int a, int b, double px, double py) {
                return ((s.x[b] - s.x[a]) * (py - s.y[a]) -
                        (s.y[b] - s.y[a]) * (px - s.x[a])) * sign;
            };
            const double stepX[3] = {-(s.y[2] - s.y[1]) * sign, -(s.y[0] - s.y[2]) * sign,
                                     -(s.y[1] - s.y[0]) * sign};
            const auto record = static_cast<std::int32_t>(s.record);

            for (int y = y0; y <= y1; ++y) {
                const double py = y + 0.5;
                const double px = x0 + 0.5;
                double e0 = edge(1, 2, px, py);
                double e1 = edge(2, 0, px, py);
                double e2 = edge(0, 1, px, py);
                std::size_t pixel = static_cast<std::size_t>(y) * width + x0;
                for (int x = x0; x <= x1; ++x, ++pixel) {
                    if (e0 >= 0 && e1 >= 0 && e2 >= 0) {
                        const float z = static_cast<float>(
                            (e0 * s.z[0] + e1 * s.z[1] + e2 * s.z[2]) / area);
                        if (z >= 0.0f && z <= 1.0f) {
                            float& depth = frame->depth[pixel];
                            std::int32_t& win = winner[pixel];
                            // Ties go to the lower record, so the picture does
                            // not depend on the order threads finish in.
                            if (z < depth || (z == depth && win >= 0 && record < win)) {
                                depth = z;
                                win = record;
                            }
                        }
                    }
                    e0 += stepX[0];
                    e1 += stepX[1];
                    e2 += stepX[2];
                }
            }
        }
    });

    // --- Resolve: ids, world position and normal, and the proxy colour.
    std::vector<std::array<float, 3>> subjectColors(subjects.size());
    for (std::size_t i = 0; i < subjects.size(); ++i) {
        float srgb[3];
        SubjectColor(subjects[i], srgb);
        for (int c = 0; c < 3; ++c) subjectColors[i][c] = SrgbToLinear(srgb[c]);
    }
    const float checkerLight = SrgbToLinear(0.60f);
    const float checkerDark = SrgbToLinear(0.45f);
    const float clay = SrgbToLinear(0.78f);
    const double checkerSize = style.checkerSize > 0 ? style.checkerSize : 1.0;

    // The light is fixed to the camera: above, to the left and a little in
    // front, so a box seen from anywhere shows its top brightest, the side
    // towards the light next, the front darker and the far side in shadow.
    // Fixed to the camera rather than the world, the same view of the same
    // thing always reads the same, whichever way the stage turned it.
    double toLight[3] = {-0.6, 0.8, 0.35};  // view space: x right, y up, +z to the camera
    Normalize(toLight);

    ParallelFor(static_cast<std::size_t>(height), [&](std::size_t row) {
        const double ndcY = (row + 0.5) / height * 2.0 - 1.0;
        for (int x = 0; x < width; ++x) {
            const std::size_t pixel = row * width + x;
            const std::int32_t win = winner[pixel];
            if (win < 0) continue;
            const TriangleRecord& r = records[win];
            frame->subject[pixel] = r.subject;
            frame->primId[pixel] = r.primId;
            frame->instanceId[pixel] = r.instanceId;
            frame->elementId[pixel] = r.element;

            // The pixel's ray, intersected with the triangle's plane: exact
            // at any distance, where reading position back from the depth
            // buffer loses precision towards the far plane.
            const double ndcX = (x + 0.5) / width * 2.0 - 1.0;
            double nearPoint[3], farPoint[3];
            Unproject(clipToWorld, ndcX, ndcY, -1.0, nearPoint);
            Unproject(clipToWorld, ndcX, ndcY, 1.0, farPoint);
            double direction[3] = {farPoint[0] - nearPoint[0], farPoint[1] - nearPoint[1],
                                   farPoint[2] - nearPoint[2]};
            Normalize(direction);
            double normal[3] = {r.normal[0], r.normal[1], r.normal[2]};
            double facing = Dot(normal, direction);
            if (facing > 0) {
                normal[0] = -normal[0];
                normal[1] = -normal[1];
                normal[2] = -normal[2];
                facing = -facing;
            }
            double t = 0;
            if (std::abs(facing) > 1e-12) {
                t = (r.offset - Dot(r.normal, nearPoint)) / Dot(r.normal, direction);
            }
            double position[3] = {nearPoint[0] + direction[0] * t,
                                  nearPoint[1] + direction[1] * t,
                                  nearPoint[2] + direction[2] * t};
            for (int c = 0; c < 3; ++c) {
                frame->normal[pixel * 3 + c] = static_cast<float>(normal[c]);
                frame->position[pixel * 3 + c] = static_cast<float>(position[c]);
            }

            float color[3] = {0.5f, 0.5f, 0.5f};
            const Role role = r.subject >= 0 && static_cast<std::size_t>(r.subject) < subjects.size()
                                  ? subjects[r.subject].role
                                  : Role::Subject;
            if (style.look == Look::Clay) {
                color[0] = color[1] = color[2] = clay;
            } else if (role == Role::Ground && !subjects[r.subject].hasColor) {
                // The checker lies in the plane the surface mostly faces, so a
                // floor on a Y-up or a Z-up stage is checkered alike, and so is
                // a ramp.
                int axis = 0;
                for (int c = 1; c < 3; ++c) {
                    if (std::abs(normal[c]) > std::abs(normal[axis])) axis = c;
                }
                const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                const auto cells = static_cast<long long>(std::floor(position[u] / checkerSize)) +
                                   static_cast<long long>(std::floor(position[v] / checkerSize));
                const float grey = (cells & 1) ? checkerDark : checkerLight;
                color[0] = color[1] = color[2] = grey;
            } else if (r.subject >= 0 && static_cast<std::size_t>(r.subject) < subjects.size()) {
                color[0] = subjectColors[r.subject][0];
                color[1] = subjectColors[r.subject][1];
                color[2] = subjectColors[r.subject][2];
            }
            if (style.look != Look::Flat) {
                double viewNormal[3];
                TransformVector(normal, camera.worldToView, viewNormal);
                Normalize(viewNormal);
                const double key = std::max(0.0, Dot(viewNormal, toLight));
                // Clay adds a soft sky term, so faces away from the key keep
                // their form instead of going flat dark.
                const double light = style.look == Look::Clay
                                         ? 0.18 + 0.67 * key + 0.15 * (0.5 + 0.5 * viewNormal[1])
                                         : 0.4 + 0.6 * key;
                for (float& c : color) c *= static_cast<float>(light);
            }
            for (int c = 0; c < 3; ++c) frame->color[pixel * 4 + c] = color[c];
        }
    });

    // --- Outlines, drawn into the nearer of two pixels that differ, so a line
    // lies on the object in front: between two primitives, against the sky,
    // and along a crease where the normal turns sharply within one primitive.
    if (style.outlineWidth > 0) {
        const int radius = std::max(
            1, static_cast<int>(std::lround(style.outlineWidth * height / 480.0)));
        const auto differs = [&](std::size_t p, std::size_t q, bool* crease) {
            *crease = false;
            if (frame->primId[p] != frame->primId[q] ||
                frame->instanceId[p] != frame->instanceId[q] ||
                (winner[p] < 0) != (winner[q] < 0)) {
                return true;
            }
            if (winner[p] < 0) return false;
            const float* a = &frame->normal[p * 3];
            const float* b = &frame->normal[q * 3];
            // 60 degrees: a box's edges are creases, the facets of a coarse
            // sphere or cylinder (36 degrees apart at ten segments) are not.
            if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] < 0.5f) {
                *crease = true;
                return true;
            }
            return false;
        };
        std::vector<std::uint8_t> ink(pixelCount, 0);
        ParallelFor(static_cast<std::size_t>(height), [&](std::size_t row) {
            const int y = static_cast<int>(row);
            for (int x = 0; x < width; ++x) {
                const std::size_t p = row * width + x;
                if (winner[p] < 0) continue;  // the sky is black already
                const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& offset : offsets) {
                    bool hit = false;
                    for (int k = 1; k <= radius && !hit; ++k) {
                        const int qx = x + offset[0] * k, qy = y + offset[1] * k;
                        if (qx < 0 || qy < 0 || qx >= width || qy >= height) break;
                        const std::size_t q = static_cast<std::size_t>(qy) * width + qx;
                        bool crease;
                        if (!differs(p, q, &crease)) continue;
                        // A crease has one side only, or it would be drawn
                        // twice as wide as a silhouette.
                        if (crease) {
                            hit = offset[0] + offset[1] > 0;
                        } else {
                            hit = frame->depth[p] <= frame->depth[q];
                        }
                    }
                    if (hit) {
                        ink[p] = 1;
                        break;
                    }
                }
            }
        });
        for (std::size_t p = 0; p < pixelCount; ++p) {
            if (ink[p]) {
                frame->color[p * 4 + 0] = 0.0f;
                frame->color[p * 4 + 1] = 0.0f;
                frame->color[p * 4 + 2] = 0.0f;
            }
        }
    }

    if (stats) {
        stats->triangles = screen.size();
        stats->culledTriangles = culled;
        stats->milliseconds = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start)
                                  .count();
    }
}

// ---------------------------------------------------------------------------
// What the frame shows of each subject
// ---------------------------------------------------------------------------

std::vector<SubjectView> MeasureSubjects(const std::vector<DrawItem>& items,
                                         const std::vector<Subject>& subjects,
                                         const Camera& camera, const Frame& frame)
{
    std::vector<SubjectView> views(subjects.size());
    const int width = frame.width, height = frame.height;
    Matrix4 viewToWorld;
    Invert(camera.worldToView, &viewToWorld);
    const double eye[3] = {viewToWorld.m[3][0], viewToWorld.m[3][1], viewToWorld.m[3][2]};

    std::vector<double> sumX(subjects.size(), 0), sumY(subjects.size(), 0),
        sumDistance(subjects.size(), 0), sumPosition(subjects.size() * 3, 0);
    for (std::size_t i = 0; i < views.size(); ++i) {
        views[i].subject = static_cast<std::int32_t>(i);
        views[i].bbox[0] = views[i].bbox[1] = 1.0f;
        views[i].bbox[2] = views[i].bbox[3] = 0.0f;
        views[i].nearest = std::numeric_limits<float>::max();
    }
    for (int row = 0; row < height; ++row) {
        // Measured top-left, as a person describes a picture.
        const float top = (height - 1 - row + 0.5f) / height;
        for (int x = 0; x < width; ++x) {
            const std::size_t p = static_cast<std::size_t>(row) * width + x;
            const std::int32_t s = frame.subject[p];
            if (s < 0 || static_cast<std::size_t>(s) >= views.size()) continue;
            SubjectView& view = views[s];
            const float left = (x + 0.5f) / width;
            ++view.pixels;
            view.bbox[0] = std::min(view.bbox[0], left);
            view.bbox[1] = std::min(view.bbox[1], top);
            view.bbox[2] = std::max(view.bbox[2], left);
            view.bbox[3] = std::max(view.bbox[3], top);
            sumX[s] += left;
            sumY[s] += top;
            const double d[3] = {frame.position[p * 3] - eye[0],
                                 frame.position[p * 3 + 1] - eye[1],
                                 frame.position[p * 3 + 2] - eye[2]};
            const double distance = std::sqrt(Dot(d, d));
            sumDistance[s] += distance;
            for (int c = 0; c < 3; ++c) sumPosition[s * 3 + c] += frame.position[p * 3 + c];
            view.nearest = std::min(view.nearest, static_cast<float>(distance));
        }
    }
    const double total = static_cast<double>(width) * height;
    for (std::size_t i = 0; i < views.size(); ++i) {
        SubjectView& view = views[i];
        if (view.pixels == 0) {
            view.bbox[0] = view.bbox[1] = view.bbox[2] = view.bbox[3] = 0.0f;
            view.nearest = 0.0f;
            continue;
        }
        view.coverage = static_cast<float>(view.pixels / total);
        view.centroid[0] = static_cast<float>(sumX[i] / view.pixels);
        view.centroid[1] = static_cast<float>(sumY[i] / view.pixels);
        view.mean = static_cast<float>(sumDistance[i] / view.pixels);
        for (int c = 0; c < 3; ++c)
            view.position[c] = static_cast<float>(sumPosition[i * 3 + c] / view.pixels);
    }

    // Facing, from the first placement of each subject that has a front: its
    // +Z, as the camera sees it. The parts of a subject are expected to share
    // the orientation of the group they belong to.
    for (const DrawItem& item : items) {
        if (item.subject < 0 || static_cast<std::size_t>(item.subject) >= subjects.size()) continue;
        if (!subjects[item.subject].hasFront) continue;
        SubjectView& view = views[item.subject];
        if (view.hasFacing) continue;
        const double z[3] = {item.objectToWorld.m[2][0], item.objectToWorld.m[2][1],
                             item.objectToWorld.m[2][2]};
        double front[3];
        TransformVector(z, camera.worldToView, front);
        Normalize(front);
        view.hasFacing = true;
        for (int c = 0; c < 3; ++c) view.front[c] = static_cast<float>(front[c]);
        // The camera looks down -Z, so a front pointing along +Z in view
        // space points back at the camera.
        if (front[2] > 0.5) {
            view.facing = "toward the camera";
        } else if (front[2] < -0.5) {
            view.facing = "away from the camera";
        } else {
            view.facing = front[0] > 0 ? "to the right" : "to the left";
        }
    }
    return views;
}

// ---------------------------------------------------------------------------
// Manifest
// ---------------------------------------------------------------------------

namespace {

void AppendString(std::string* out, const std::string& text)
{
    out->push_back('"');
    for (unsigned char c : text) {
        switch (c) {
            case '"': *out += "\\\""; break;
            case '\\': *out += "\\\\"; break;
            case '\n': *out += "\\n"; break;
            case '\r': *out += "\\r"; break;
            case '\t': *out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    *out += buffer;
                } else {
                    out->push_back(static_cast<char>(c));
                }
        }
    }
    out->push_back('"');
}

void AppendNumber(std::string* out, double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6g", std::isfinite(value) ? value : 0.0);
    *out += buffer;
}

void AppendNumbers(std::string* out, const float* values, int count)
{
    out->push_back('[');
    for (int i = 0; i < count; ++i) {
        if (i) *out += ", ";
        AppendNumber(out, values[i]);
    }
    out->push_back(']');
}

}  // namespace

std::string ManifestJson(const std::vector<Subject>& subjects,
                         const std::vector<SubjectView>& views,
                         const Camera& camera, int width, int height, const std::string& shot)
{
    Matrix4 viewToWorld;
    Invert(camera.worldToView, &viewToWorld);
    const float eye[3] = {static_cast<float>(viewToWorld.m[3][0]),
                          static_cast<float>(viewToWorld.m[3][1]),
                          static_cast<float>(viewToWorld.m[3][2])};
    const float forward[3] = {static_cast<float>(-viewToWorld.m[2][0]),
                              static_cast<float>(-viewToWorld.m[2][1]),
                              static_cast<float>(-viewToWorld.m[2][2])};
    const float up[3] = {static_cast<float>(viewToWorld.m[1][0]),
                         static_cast<float>(viewToWorld.m[1][1]),
                         static_cast<float>(viewToWorld.m[1][2])};

    std::string out = "{\n  \"width\": ";
    AppendNumber(&out, width);
    out += ",\n  \"height\": ";
    AppendNumber(&out, height);
    out += ",\n  \"shot\": ";
    AppendString(&out, shot);
    out += ",\n  \"camera\": {\"position\": ";
    AppendNumbers(&out, eye, 3);
    out += ", \"forward\": ";
    AppendNumbers(&out, forward, 3);
    out += ", \"up\": ";
    AppendNumbers(&out, up, 3);
    // Full angles of view in degrees, horizontal then vertical, from a
    // perspective projection; zero for an orthographic one.
    float fov[2] = {0, 0};
    if (camera.projection.m[2][3] != 0.0) {
        fov[0] = static_cast<float>(2.0 * std::atan(1.0 / camera.projection.m[0][0]) * 180.0 / 3.14159265358979);
        fov[1] = static_cast<float>(2.0 * std::atan(1.0 / camera.projection.m[1][1]) * 180.0 / 3.14159265358979);
    }
    out += ", \"fov\": ";
    AppendNumbers(&out, fov, 2);
    out += "},\n  \"subjects\": [";
    for (std::size_t i = 0; i < subjects.size(); ++i) {
        const Subject& subject = subjects[i];
        const SubjectView& view = i < views.size() ? views[i] : SubjectView{};
        out += i ? ",\n    {" : "\n    {";
        out += "\"id\": ";
        AppendNumber(&out, static_cast<double>(i));
        out += ", \"key\": ";
        AppendString(&out, subject.key);
        out += ", \"role\": ";
        AppendString(&out, RoleName(subject.role));
        out += ", \"description\": ";
        AppendString(&out, subject.description);
        out += ", \"reference\": ";
        AppendString(&out, subject.reference);
        // A ground is drawn as the checkerboard, not in its colour, unless
        // one was authored for it.
        out += ", \"proxyColor\": ";
        if (subject.role == Role::Ground && !subject.hasColor) {
            out += "null";
        } else {
            float color[3];
            SubjectColor(subject, color);
            AppendNumbers(&out, color, 3);
        }
        out += ", \"visible\": ";
        out += view.pixels > 0 ? "true" : "false";
        out += ", \"pixels\": ";
        AppendNumber(&out, static_cast<double>(view.pixels));
        out += ", \"coverage\": ";
        AppendNumber(&out, view.coverage);
        out += ", \"bbox\": ";
        AppendNumbers(&out, view.bbox, 4);
        out += ", \"centroid\": ";
        AppendNumbers(&out, view.centroid, 2);
        out += ", \"distance\": {\"nearest\": ";
        AppendNumber(&out, view.nearest);
        out += ", \"mean\": ";
        AppendNumber(&out, view.mean);
        out += "}, \"position\": ";
        AppendNumbers(&out, view.position, 3);
        if (view.hasFacing) {
            out += ", \"facing\": ";
            AppendString(&out, view.facing);
            out += ", \"front\": ";
            AppendNumbers(&out, view.front, 3);
        }
        out += "}";
    }
    out += subjects.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

std::vector<std::uint8_t> ProxyRgb8(const Frame& frame)
{
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(frame.width) * frame.height * 3);
    for (int row = 0; row < frame.height; ++row) {
        const std::size_t source = static_cast<std::size_t>(row) * frame.width;
        const std::size_t target = static_cast<std::size_t>(frame.height - 1 - row) * frame.width;
        for (int x = 0; x < frame.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                const float value = LinearToSrgb(frame.color[(source + x) * 4 + c]);
                rgb[(target + x) * 3 + c] =
                    static_cast<std::uint8_t>(std::lround(value * 255.0f));
            }
        }
    }
    return rgb;
}

}  // namespace phantasos
