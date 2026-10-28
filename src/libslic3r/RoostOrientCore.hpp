// Roost Print: the first round of "which way up prints fastest".
//
// Checks thousands of ways to stand a part on the plate (every direction on a fine grid over the
// sphere, plus every flat face of its convex hull), using quick geometry only:
//   - height (more layers, more time),
//   - support (the area of steep down-facing surfaces, and how far they hang above the plate),
//   - the first layer (a big flat base takes a while at first-layer speed),
//   - the shortest layers can't go faster than the minimum layer time.
// Then it throws out ways that would tip over (the centre of mass must sit well inside the patch
// that touches the plate) or don't fit the printer, and keeps the best few that differ from each
// other. The GUI then slices those few for real and keeps the fastest (slic3r/GUI/RoostOrient.cpp).
#ifndef slic3r_RoostOrientCore_hpp_
#define slic3r_RoostOrientCore_hpp_

#include <functional>
#include <vector>

#include "Point.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {
namespace RoostOrient {

struct Settings
{
    double layer_height       = 0.2;   // mm
    double first_layer_height = 0.2;   // mm
    double overhang_deg       = 30.0;  // surfaces flatter than this (from horizontal) need support
    bool   supports_on        = true;  // otherwise steep overhangs count as unprintable
    double layer_overhead_s   = 2.0;   // layer change, travel, retraction per layer
    double min_layer_time_s   = 6.0;   // the filament's minimum layer time
    double flow_mm3_s         = 12.0;  // typical volumetric speed for the part's walls and infill
    double first_layer_mm2_s  = 18.0;  // first layer: area covered per second
    double support_mm3_s      = 30.0;  // support volume (as a block) printed per second (sparse)
    double max_height         = 1e9;   // printer's height
    double bed_x              = 1e9;   // printer's plate size
    double bed_y              = 1e9;
    int    directions         = 4000;  // grid directions to check (plus the hull's faces)
    int    keep               = 4;     // how many to hand back
    double min_tip_deg        = 12.0;  // must tilt at least this far before it would tip over
};

struct Candidate
{
    Vec3d    down;            // the part's direction that faces the plate (in the input mesh's frame)
    Matrix3d rotation;        // a rotation that puts `down` onto -Z
    double   estimate_s{0};   // the quick estimate of print time
    double   height{0};       // mm
    double   contact_mm2{0};  // area flat on the plate
    double   support_mm3{0};  // support, as a block under the overhangs
    double   overhang_mm2{0}; // steep down-facing area
    double   tip_deg{0};      // how far it can tilt before tipping
};

struct Result
{
    std::vector<Candidate> best;      // best first; empty if the mesh was empty
    int                    checked{0}; // how many ways were checked
    Candidate              current;    // the way it stands now, for comparison
};

// `mesh` is the part scaled but not rotated; `current_down` is which of its directions faces the
// plate now. `cancel` is polled; true stops early.
Result rank(const indexed_triangle_set& mesh, const Vec3d& current_down, const Settings& settings,
            std::function<bool()> cancel = nullptr);

// Score one way up (also used for the way it stands now).
Candidate evaluate(const indexed_triangle_set& mesh, const Vec3d& down, const Settings& settings);

} // namespace RoostOrient
} // namespace Slic3r

#endif
