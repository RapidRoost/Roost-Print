// Roost Print: the first round of "which way up prints fastest". See RoostOrientCore.hpp.
#include "RoostOrientCore.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numeric>

#include <tbb/parallel_for.h>

#include "QuadricEdgeCollapse.hpp"

namespace Slic3r {
namespace RoostOrient {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Tri
{
    Vec3d c;     // centroid
    Vec3d n;     // unit normal
    double a;    // area
    int v[3];
};

struct Prepared
{
    std::vector<Vec3d> verts;
    std::vector<Tri>   tris;
    Vec3d              com{Vec3d::Zero()};
    double             volume{0};
};

// The part, light enough to check thousands of times (at most ~6000 triangles; the shape barely
// changes, and only the shape matters here).
Prepared prepare(const indexed_triangle_set& mesh)
{
    Prepared p;
    indexed_triangle_set its = mesh;
    if (its.indices.size() > 6000) {
        try {
            its_quadric_edge_collapse(its, 6000);
        } catch (...) {
            its = mesh; // keep the full mesh if simplifying fails
        }
    }
    p.verts.reserve(its.vertices.size());
    for (const Vec3f& v : its.vertices)
        p.verts.emplace_back(v.cast<double>());
    p.tris.reserve(its.indices.size());
    double vol = 0;
    Vec3d  vol_c = Vec3d::Zero(), area_c = Vec3d::Zero();
    double area = 0;
    for (const Vec3i32& f : its.indices) {
        if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= int(p.verts.size()) || f[1] >= int(p.verts.size()) || f[2] >= int(p.verts.size()))
            continue;
        const Vec3d& a = p.verts[f[0]];
        const Vec3d& b = p.verts[f[1]];
        const Vec3d& c = p.verts[f[2]];
        const Vec3d  cr = (b - a).cross(c - a);
        const double len = cr.norm();
        if (len <= 1e-12)
            continue;
        Tri t;
        t.c    = (a + b + c) / 3.0;
        t.n    = cr / len;
        t.a    = 0.5 * len;
        t.v[0] = f[0];
        t.v[1] = f[1];
        t.v[2] = f[2];
        p.tris.push_back(t);
        const double sv = a.dot(b.cross(c)) / 6.0;
        vol += sv;
        vol_c += sv * (a + b + c) / 4.0;
        area += t.a;
        area_c += t.a * t.c;
    }
    p.volume = std::abs(vol);
    if (std::abs(vol) > 1e-6)
        p.com = vol_c / vol;
    else if (area > 0)
        p.com = area_c / area;
    return p;
}

struct Quick
{
    double zmin{0}, zmax{0}, contact{0}, overhang{0}, support{0};
};

Quick quick(const Prepared& p, const Vec3d& d, double cos_overhang)
{
    Quick q;
    q.zmin = 1e300;
    q.zmax = -1e300;
    for (const Vec3d& v : p.verts) {
        const double z = -d.dot(v);
        q.zmin = std::min(q.zmin, z);
        q.zmax = std::max(q.zmax, z);
    }
    for (const Tri& t : p.tris) {
        const double nz = -d.dot(t.n); // the triangle's normal's z once `d` faces down
        if (nz < -0.9962) { // within 5 degrees of flat, facing down: on the plate?
            double ztop = -1e300;
            for (int k = 0; k < 3; ++k)
                ztop = std::max(ztop, -d.dot(p.verts[t.v[k]]) - q.zmin);
            if (ztop < 0.15) {
                q.contact += t.a * (-nz);
                continue;
            }
        }
        if (nz > -cos_overhang)
            continue; // not facing down steeply enough to need support
        const double zc = -d.dot(t.c) - q.zmin;
        if (zc > 0.3) {
            q.overhang += t.a;
            q.support += t.a * (-nz) * zc;
        }
    }
    return q;
}

double estimate(const Quick& q, const Prepared& p, const Settings& s)
{
    const double h      = std::max(0.0, q.zmax - q.zmin);
    const double layers = 1.0 + std::max(0.0, h - s.first_layer_height) / std::max(0.01, s.layer_height);
    const double volume = p.volume > 0 ? p.volume : 0.0;
    // Walls and infill: about half of a solid part's volume gets printed.
    const double extrude = 0.5 * volume / std::max(1.0, s.flow_mm3_s);
    // Short layers wait for the minimum layer time (roughly: the part of it the printing doesn't cover).
    const double wait    = std::max(0.0, layers * s.min_layer_time_s - extrude) * 0.6;
    const double first   = q.contact / std::max(1.0, s.first_layer_mm2_s);
    double       support = 0;
    if (s.supports_on)
        support = q.support / std::max(1.0, s.support_mm3_s);
    else
        support = q.overhang * 60.0; // no supports: steep overhangs fail, so avoid them hard
    return extrude + layers * s.layer_overhead_s + wait + first + support;
}

// Andrew's monotone chain; counter-clockwise, no repeats.
std::vector<Vec2d> hull2d(std::vector<Vec2d> pts)
{
    std::sort(pts.begin(), pts.end(), [](const Vec2d& a, const Vec2d& b) { return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const Vec2d& a, const Vec2d& b) { return (a - b).squaredNorm() < 1e-12; }), pts.end());
    if (pts.size() < 3)
        return pts;
    auto cross = [](const Vec2d& o, const Vec2d& a, const Vec2d& b) { return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x()); };
    std::vector<Vec2d> h(2 * pts.size());
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0)
            --k;
        h[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(h[k - 2], h[k - 1], pts[i - 1]) <= 0)
            --k;
        h[k++] = pts[i - 1];
    }
    h.resize(k - 1);
    return h;
}

// How far it can tilt before the centre of mass passes the edge of what touches the plate, and
// whether it fits the printer.
void stability(const Prepared& p, const Vec3d& d, const Quick& q, const Settings& s, double& tip_deg, bool& fits)
{
    const Vec3d a = std::abs(d.x()) < 0.9 ? Vec3d::UnitX() : Vec3d::UnitY();
    const Vec3d u = d.cross(a).normalized();
    const Vec3d v = d.cross(u);
    std::vector<Vec2d> touch;
    double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300;
    for (const Vec3d& pt : p.verts) {
        const double pu = u.dot(pt), pv = v.dot(pt);
        umin = std::min(umin, pu);
        umax = std::max(umax, pu);
        vmin = std::min(vmin, pv);
        vmax = std::max(vmax, pv);
        if (-d.dot(pt) - q.zmin < 0.3)
            touch.emplace_back(pu, pv);
    }
    const double w = umax - umin, l = vmax - vmin, h = q.zmax - q.zmin;
    fits = h <= s.max_height + 1e-6 && ((w <= s.bed_x && l <= s.bed_y) || (w <= s.bed_y && l <= s.bed_x));

    tip_deg = 0;
    const std::vector<Vec2d> hull = hull2d(touch);
    if (hull.size() < 3)
        return;
    double area = 0;
    for (size_t i = 0; i < hull.size(); ++i) {
        const Vec2d& p0 = hull[i];
        const Vec2d& p1 = hull[(i + 1) % hull.size()];
        area += p0.x() * p1.y() - p1.x() * p0.y();
    }
    if (0.5 * area < 1.0)
        return; // a point or a sliver: it would roll
    const Vec2d  com(u.dot(p.com), v.dot(p.com));
    const double com_h = std::max(1e-3, -d.dot(p.com) - q.zmin);
    double       dist  = 1e300;
    for (size_t i = 0; i < hull.size(); ++i) {
        const Vec2d& p0  = hull[i];
        const Vec2d& p1  = hull[(i + 1) % hull.size()];
        const Vec2d  e   = p1 - p0;
        const double len = e.norm();
        if (len < 1e-9)
            continue;
        dist = std::min(dist, (e.x() * (com.y() - p0.y()) - e.y() * (com.x() - p0.x())) / len);
    }
    tip_deg = std::atan2(dist, com_h) * 180.0 / kPi;
}

Candidate make(const Vec3d& d, const Quick& q, double est)
{
    Candidate c;
    c.down         = d;
    c.rotation     = Eigen::Quaterniond::FromTwoVectors(d, -Vec3d::UnitZ()).toRotationMatrix();
    c.estimate_s   = est;
    c.height       = q.zmax - q.zmin;
    c.contact_mm2  = q.contact;
    c.support_mm3  = q.support;
    c.overhang_mm2 = q.overhang;
    return c;
}

Candidate evaluate_prepared(const Prepared& p, const Vec3d& down, const Settings& s)
{
    const double cos_ov = std::cos(std::clamp(s.overhang_deg, 1.0, 89.0) * kPi / 180.0);
    const Vec3d  d      = down.normalized();
    const Quick  q      = quick(p, d, cos_ov);
    Candidate    c      = make(d, q, estimate(q, p, s));
    bool         fits   = true;
    stability(p, d, q, s, c.tip_deg, fits);
    return c;
}

} // namespace

Candidate evaluate(const indexed_triangle_set& mesh, const Vec3d& down, const Settings& settings)
{
    return evaluate_prepared(prepare(mesh), down, settings);
}

Result rank(const indexed_triangle_set& mesh, const Vec3d& current_down, const Settings& s, std::function<bool()> cancel)
{
    Result res;
    const Prepared p = prepare(mesh);
    if (p.tris.empty())
        return res;

    // Directions to try: a fine, even grid over the sphere (Fibonacci), every big flat face of the
    // convex hull (the stable ways to lie), the six axes and the way it is now.
    std::vector<Vec3d> dirs;
    const int n = std::max(100, s.directions);
    const double golden = kPi * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < n; ++i) {
        const double y = 1.0 - 2.0 * (i + 0.5) / n;
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        dirs.emplace_back(std::cos(golden * i) * r, y, std::sin(golden * i) * r);
    }
    for (const Vec3d& axis : {Vec3d::UnitX(), Vec3d::UnitY(), Vec3d::UnitZ()}) {
        dirs.push_back(axis);
        dirs.push_back(-axis);
    }
    dirs.push_back(current_down.normalized());
    try {
        std::vector<Vec3f> pts;
        pts.reserve(p.verts.size());
        for (const Vec3d& v : p.verts)
            pts.emplace_back(v.cast<float>());
        const indexed_triangle_set hull = its_convex_hull(pts);
        struct Face { Vec3d n; double a; };
        std::vector<Face> faces;
        double total = 0;
        for (const Vec3i32& f : hull.indices) {
            const Vec3d a = hull.vertices[f[0]].cast<double>(), b = hull.vertices[f[1]].cast<double>(), c = hull.vertices[f[2]].cast<double>();
            const Vec3d cr = (b - a).cross(c - a);
            const double len = cr.norm();
            if (len < 1e-12)
                continue;
            total += 0.5 * len;
            const Vec3d nn = cr / len;
            bool merged = false;
            for (Face& fc : faces)
                if (fc.n.dot(nn) > 0.9995) {
                    fc.a += 0.5 * len;
                    merged = true;
                    break;
                }
            if (!merged)
                faces.push_back({nn, 0.5 * len});
        }
        std::sort(faces.begin(), faces.end(), [](const Face& x, const Face& y) { return x.a > y.a; });
        for (size_t i = 0; i < faces.size() && i < 400; ++i)
            if (faces[i].a >= 0.002 * total)
                dirs.push_back(faces[i].n); // that face down on the plate
    } catch (...) {
        // no hull (flat or broken mesh): the grid alone is fine
    }

    // Round one: the quick estimate, tipping and fit, for every direction.
    const double cos_ov = std::cos(std::clamp(s.overhang_deg, 1.0, 89.0) * kPi / 180.0);
    std::vector<Quick>  quicks(dirs.size());
    std::vector<double> est(dirs.size(), 1e300), tips(dirs.size(), 0.0);
    std::vector<char>   fit(dirs.size(), 0);
    std::atomic<bool>   stop{false};
    tbb::parallel_for(tbb::blocked_range<size_t>(0, dirs.size(), 32), [&](const tbb::blocked_range<size_t>& r) {
        if (stop.load() || (cancel && cancel())) {
            stop = true;
            return;
        }
        for (size_t i = r.begin(); i < r.end(); ++i) {
            quicks[i] = quick(p, dirs[i], cos_ov);
            est[i]    = estimate(quicks[i], p, s);
            bool f    = true;
            stability(p, dirs[i], quicks[i], s, tips[i], f);
            fit[i] = f ? 1 : 0;
        }
    });
    res.checked = int(dirs.size());
    res.current = evaluate_prepared(p, current_down, s);
    if (stop)
        return res;

    double max_contact = 0;
    for (const Quick& q : quicks)
        max_contact = std::max(max_contact, q.contact);
    const double min_contact = std::min(15.0, 0.5 * max_contact);

    std::vector<size_t> order(dirs.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return est[a] < est[b]; });

    // Round two: keep the best handful that won't tip over, fit the printer and really differ
    // (at least 15 degrees apart, and not just a mirror image with the same numbers).
    const double cos_apart = std::cos(15.0 * kPi / 180.0);
    auto pick = [&](bool need_contact) {
        for (size_t k = 0; k < order.size() && int(res.best.size()) < std::max(1, s.keep); ++k) {
            const size_t i = order[k];
            if (!fit[i] || tips[i] < s.min_tip_deg || (need_contact && quicks[i].contact < min_contact))
                continue;
            bool apart = true;
            for (const Candidate& c : res.best) {
                const bool same_numbers = std::abs(c.estimate_s - est[i]) <= 0.01 * std::max(1.0, est[i]) &&
                                          std::abs(c.height - (quicks[i].zmax - quicks[i].zmin)) < 0.5 &&
                                          std::abs(c.contact_mm2 - quicks[i].contact) <= 0.05 * std::max(1.0, quicks[i].contact);
                if (c.down.dot(dirs[i]) > cos_apart || same_numbers) {
                    apart = false;
                    break;
                }
            }
            if (!apart)
                continue;
            Candidate c = make(dirs[i], quicks[i], est[i]);
            c.tip_deg   = tips[i];
            res.best.push_back(c);
        }
    };
    pick(true);
    if (res.best.empty())
        pick(false); // nothing sits flat enough: steady will do
    // Still nothing steady that fits: hand back nothing, so the part stays as it is.
    return res;
}

} // namespace RoostOrient
} // namespace Slic3r
