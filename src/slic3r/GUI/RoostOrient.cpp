// Roost Print: stand every new part the fastest way up. See RoostOrient.hpp.
#include "RoostOrient.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <set>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelInstance.hpp"
#include "libslic3r/ModelObject.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/RoostOrientCore.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/format.hpp"

#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Jobs/Job.hpp"
#include "Jobs/Worker.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "RoostSlim.hpp"
#include "RoostStages.hpp"

namespace Slic3r {
namespace GUI {

// The geometry side lives in libslic3r (RoostOrientCore.hpp).
namespace Core = Slic3r::RoostOrient;

namespace RoostOrient {

namespace {

std::atomic<bool>  g_busy{false};
std::atomic<float> g_progress{0.0f};
std::string        g_status;          // main thread only
std::set<size_t>   g_seen;            // parts (ModelObject ids) already looked at
size_t             g_undo_time = 0;   // the undo snapshot from before the last turn; 0 = none
bool               g_arrange_pending = false;

const char* kSliceGuard = "roost_orient_slicing.lock";

std::string guard_path() { return (boost::filesystem::path(data_dir()) / kSliceGuard).string(); }

// Round 2 (real slicing) is on unless Roost Print once closed during it.
bool slicing_allowed()
{
    AppConfig* cfg = wxGetApp().app_config;
    if (cfg == nullptr)
        return false;
    if (boost::filesystem::exists(guard_path())) {
        boost::system::error_code ec;
        boost::filesystem::remove(guard_path(), ec);
        cfg->set("roost_orient_slicing", "0");
        BOOST_LOG_TRIVIAL(warning) << "Roost Print: closed while test-slicing orientations last time; quick check only from now on";
    }
    return cfg->get("roost_orient_slicing") != "0";
}

Vec3d tilt_of(const Matrix3d& rotation)
{
    const double cos_pitch = std::hypot(rotation(2, 1), rotation(2, 2));
    return Vec3d(cos_pitch < 1e-10 ? 0.0 : std::atan2(rotation(2, 1), rotation(2, 2)), std::atan2(-rotation(2, 0), cos_pitch), 0.0);
}

// Turn an instance so the part stands as `rotation` says (keeping which way it faces on the plate),
// back on the plate, and back inside its plate if it now hangs over the edge.
void turn_instance(ModelInstance* instance, int object_index, int instance_index, const Matrix3d& rotation)
{
    ModelObject*   object  = instance->get_object();
    const Matrix3d current = instance->get_transformation().get_rotation_matrix().linear();
    const Matrix3d tilt    = Geometry::rotation_transform(tilt_of(current)).linear();
    const Matrix3d heading = current * tilt.transpose();
    Vec3d          euler   = tilt_of(rotation);
    euler.z()              = std::atan2(heading(1, 0), heading(0, 0));
    instance->set_rotation(euler);
    object->invalidate_bounding_box();
    object->ensure_on_bed();

    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    PartPlateList& plates      = plater->get_partplate_list();
    const int      plate_index = plates.find_instance(object_index, instance_index);
    PartPlate*     plate       = plate_index >= 0 ? plates.get_plate(plate_index) : nullptr;
    if (plate == nullptr || !plate->check_outside(object_index, instance_index))
        return;
    const BoundingBoxf3 box    = object->instance_convex_hull_bounding_box(size_t(instance_index));
    const Vec3d         centre = plate->get_center_origin();
    object->translate_instance(size_t(instance_index), Vec3d(centre.x() - box.center().x(), centre.y() - box.center().y(), 0.0));
}

std::string minutes(double seconds)
{
    const long m = long(std::lround(seconds / 60.0));
    if (m >= 60)
        return std::to_string(m / 60) + "h " + std::to_string(m % 60) + "m";
    return std::to_string(std::max(1L, m)) + " min";
}

std::string thousands(int n)
{
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3)
        s.insert(size_t(i), ",");
    return s;
}

// One part (all its copies, and any other parts with exactly the same shape).
struct Group
{
    std::string                         name;
    indexed_triangle_set                mesh;         // scaled, not turned
    Vec3d                               current_down; // which of its directions faces the plate now
    Model                               model;        // a copy of the part, for test slices
    std::vector<std::pair<size_t, int>> instances;    // (ModelObject id, instance index)
    // Results
    bool     change{false};
    Matrix3d rotation{Matrix3d::Identity()};
    double   saved_s{0};
    int      checked{0};
    bool     sliced{false};
};

class OrientJob : public Job
{
public:
    OrientJob(std::vector<Group> groups, DynamicPrintConfig config, Core::Settings settings, std::vector<Vec2d> bed,
              double height, bool slices, bool bbl, bool cx, MixedFilamentManager mixed)
        : m_groups(std::move(groups)), m_config(std::move(config)), m_settings(settings), m_bed(std::move(bed)), m_height(height)
        , m_slices(slices), m_bbl(bbl), m_cx(cx), m_mixed(std::move(mixed))
    {}

    void process(Ctl& ctl) override
    {
        const auto cancel = [&ctl]() { return ctl.was_canceled(); };
        for (size_t g = 0; g < m_groups.size() && !ctl.was_canceled(); ++g) {
            Group&      grp  = m_groups[g];
            const float base = float(g) / float(m_groups.size());
            const float span = 1.0f / float(m_groups.size());
            g_progress       = base;
            ctl.update_status(int(base * 100), format(_u8L("Finding the fastest way up: %1%"), grp.name));

            Core::Result res = Core::rank(grp.mesh, grp.current_down, m_settings, cancel);
            grp.checked             = res.checked;
            g_progress              = base + 0.15f * span;
            if (ctl.was_canceled() || res.best.empty())
                continue;

            // The ways to try for real: the way it is now first, then the quick round's best.
            std::vector<Matrix3d> tries{Matrix3d::Identity()};
            std::vector<double>   quick{res.current.estimate_s};
            const Vec3d           now_down = grp.current_down.normalized();
            for (const Core::Candidate& c : res.best)
                if (c.down.dot(now_down) < 0.996) { // not just the way it is now
                    tries.push_back(c.rotation);
                    quick.push_back(c.estimate_s);
                }
            if (tries.size() == 1)
                continue; // already the best way up

            std::vector<double> real(tries.size(), std::numeric_limits<double>::infinity());
            if (m_slices) {
                const auto started = std::chrono::steady_clock::now();
                for (size_t i = 0; i < tries.size() && !ctl.was_canceled(); ++i) {
                    // Keep it to about a minute and a half per part.
                    if (i >= 2 && std::chrono::steady_clock::now() - started > std::chrono::seconds(90))
                        break;
                    real[i]    = slice_seconds(grp, i == 0 ? nullptr : &tries[i]);
                    g_progress = base + span * (0.15f + 0.85f * float(i + 1) / float(tries.size()));
                    ctl.update_status(int(g_progress * 100), format(_u8L("Test-slicing %1% (%2% of %3%)"), grp.name, i + 1, tries.size()));
                }
            }

            const bool use_real = m_slices && std::isfinite(real[0]) &&
                                  std::count_if(real.begin(), real.end(), [](double t) { return std::isfinite(t); }) >= 2;
            const std::vector<double>& t = use_real ? real : quick;
            size_t best = 0;
            for (size_t i = 1; i < t.size(); ++i)
                if (t[i] < t[best])
                    best = i;
            // Only turn it if that's clearly faster (5%, and at least a minute when really sliced).
            const double gain = t[0] - t[best];
            if (best != 0 && gain > 0.05 * t[0] && (!use_real || gain > 60.0)) {
                grp.change   = true;
                grp.rotation = tries[best];
                grp.saved_s  = gain;
                grp.sliced   = use_real;
            }
            BOOST_LOG_TRIVIAL(info) << "Roost Print: orient " << grp.name << ": checked " << res.checked << ", "
                                    << (use_real ? "sliced " : "estimated ") << t[0] << " s now, best " << t[best] << " s"
                                    << (grp.change ? " (turning it)" : " (keeping it)");
        }
        g_progress = 1.0f;
    }

    void finalize(bool canceled, std::exception_ptr& eptr) override
    {
        g_busy = false;
        if (eptr) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "Roost Print: finding the fastest way up failed: " << e.what();
            } catch (...) {
            }
            eptr     = nullptr;
            g_status = _u8L("Couldn't check the orientation; kept it as it was.");
            return;
        }
        if (canceled) {
            g_status = _u8L("Stopped checking the orientation.");
            return;
        }
        Plater* plater = wxGetApp().plater();
        if (plater == nullptr)
            return;

        int    turned = 0, checked = 0;
        double saved  = 0;
        bool   sliced = false;
        for (const Group& g : m_groups) {
            checked += g.checked;
            if (g.change)
                ++turned;
        }
        if (turned > 0) {
            plater->take_snapshot(_u8L("Fastest way up"));
            g_undo_time = plater->get_active_snapshot_time();
            Model& model = plater->model();
            for (const Group& g : m_groups) {
                if (!g.change)
                    continue;
                bool any = false;
                for (const auto& [object_id, inst] : g.instances)
                    for (int oi = 0; oi < int(model.objects.size()); ++oi) {
                        ModelObject* obj = model.objects[oi];
                        if (obj != nullptr && obj->id().id == object_id && inst < int(obj->instances.size())) {
                            turn_instance(obj->instances[inst], oi, inst, g.rotation);
                            any = true;
                        }
                    }
                if (any) {
                    saved += g.saved_s * double(g.instances.size());
                    sliced = sliced || g.sliced;
                }
            }
            plater->update();
            // Parts sharing a plate may now overlap: rearrange once the job's done (on_frame).
            PartPlateList& plates = plater->get_partplate_list();
            for (int p = 0; p < plates.get_plate_count() && !g_arrange_pending; ++p)
                if (PartPlate* plate = plates.get_plate(p)) {
                    int count = 0;
                    for (int oi = 0; oi < int(model.objects.size()); ++oi)
                        for (int k = 0; k < int(model.objects[oi]->instances.size()); ++k)
                            if (plate->contain_instance(oi, k))
                                ++count;
                    g_arrange_pending = count > 1;
                }
        }
        if (turned == 0)
            g_status = format(_u8L("Already the fastest way up (checked %1% ways)."), thousands(checked));
        else
            g_status = format(sliced ? _u8L("Turned %1% to print faster: about %2% saved (checked %3% ways, test-sliced the best).") :
                                       _u8L("Turned %1% to print faster: about %2% saved (checked %3% ways)."),
                              turned == 1 ? _u8L("the part") : format(_u8L("%1% parts"), turned), minutes(saved), thousands(checked));
        if (GLCanvas3D* c = plater->get_current_canvas3D()) {
            c->set_as_dirty();
            c->request_extra_frame();
        }
    }

private:
    // Slice the part on its own, turned by `rotation` (null: as it is now). Seconds, or infinity.
    double slice_seconds(Group& grp, const Matrix3d* rotation)
    {
        const std::string guard = guard_path();
        try {
            { boost::nowide::ofstream mark(guard); mark << "slicing\n"; }
            Model model = grp.model;
            for (ModelObject* obj : model.objects) {
                while (obj->instances.size() > 1)
                    obj->delete_instance(obj->instances.size() - 1);
                ModelInstance* inst = obj->instances.front();
                if (rotation != nullptr)
                    inst->set_rotation(tilt_of(*rotation));
                obj->invalidate_bounding_box();
                obj->ensure_on_bed();
                // In the middle of the plate.
                BoundingBoxf bb;
                for (const Vec2d& p : m_bed)
                    bb.merge(p);
                const BoundingBoxf3 box = obj->instance_convex_hull_bounding_box(size_t(0));
                obj->translate_instance(size_t(0), Vec3d(bb.center().x() - box.center().x(), bb.center().y() - box.center().y(), 0.0));
            }
            BuildVolume volume(m_bed, m_height);
            if (model.update_print_volume_state(volume) == 0) {
                boost::filesystem::remove(guard);
                return std::numeric_limits<double>::infinity(); // doesn't fit this way up
            }
            Print print;
            print.set_is_BBL_printer(m_bbl);
            print.set_is_CX_printer(m_cx);
            print.mixed_filament_manager() = m_mixed;
            print.apply(model, m_config);
            StringObjectException warning;
            if (!print.validate(&warning).string.empty()) {
                boost::filesystem::remove(guard);
                return std::numeric_limits<double>::infinity();
            }
            print.process();
            GCodeProcessorResult result;
            const boost::filesystem::path out = boost::filesystem::temp_directory_path() /
                                                boost::filesystem::unique_path("roost_orient_%%%%%%%%.gcode");
            print.export_gcode(out.string(), &result, nullptr);
            boost::system::error_code ec;
            boost::filesystem::remove(out, ec);
            boost::filesystem::remove(guard, ec);
            const double t = result.print_statistics.modes[0].time;
            return t > 0 ? t : std::numeric_limits<double>::infinity();
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "Roost Print: test slice failed: " << e.what();
        } catch (...) {
        }
        boost::system::error_code ec;
        boost::filesystem::remove(guard, ec);
        return std::numeric_limits<double>::infinity();
    }

    std::vector<Group>    m_groups;
    DynamicPrintConfig    m_config;
    Core::Settings m_settings;
    std::vector<Vec2d>    m_bed;
    double                m_height;
    bool                  m_slices, m_bbl, m_cx;
    MixedFilamentManager  m_mixed;
};

double opt_number(const DynamicPrintConfig& cfg, const char* key, double fallback)
{
    try {
        const ConfigOption* opt = cfg.option(key);
        if (opt == nullptr)
            return fallback;
        if (const auto* f = dynamic_cast<const ConfigOptionFloat*>(opt))
            return f->value;
        if (const auto* i = dynamic_cast<const ConfigOptionInt*>(opt))
            return double(i->value);
        if (const auto* fs = dynamic_cast<const ConfigOptionFloats*>(opt))
            return fs->values.empty() ? fallback : fs->values.front();
        if (const auto* is = dynamic_cast<const ConfigOptionInts*>(opt))
            return is->values.empty() ? fallback : double(is->values.front());
        if (const auto* fn = dynamic_cast<const ConfigOptionFloatOrPercent*>(opt))
            return fn->percent ? fallback : fn->value;
    } catch (...) {
    }
    return fallback;
}

// Start on these parts (indices into the model's objects).
void start(Plater* plater, const std::vector<int>& objects)
{
    Model&        model = plater->model();
    PresetBundle* pb    = wxGetApp().preset_bundle;
    if (pb == nullptr)
        return;
    const DynamicPrintConfig config = pb->full_config();

    Core::Settings s;
    s.layer_height       = opt_number(config, "layer_height", 0.2);
    s.first_layer_height = opt_number(config, "initial_layer_print_height", s.layer_height);
    s.supports_on        = config.has("enable_support") && config.opt_bool("enable_support");
    const double thr     = opt_number(config, "support_threshold_angle", 30.0);
    s.overhang_deg       = thr > 0 ? thr : 30.0;
    s.min_layer_time_s   = opt_number(config, "slow_down_layer_time", 6.0);
    std::vector<Vec2d> bed;
    if (const auto* area = config.option<ConfigOptionPoints>("printable_area"))
        bed = area->values;
    if (bed.size() < 3)
        return;
    BoundingBoxf bb;
    for (const Vec2d& p : bed)
        bb.merge(p);
    s.bed_x      = bb.size().x();
    s.bed_y      = bb.size().y();
    s.max_height = opt_number(config, "printable_height", 250.0);

    // One group per distinct shape (copies of a part only need checking once).
    std::vector<Group>              groups;
    std::map<std::string, size_t>   by_shape;
    for (int oi : objects) {
        if (oi < 0 || oi >= int(model.objects.size()))
            continue;
        ModelObject* obj = model.objects[oi];
        if (obj == nullptr || obj->instances.empty() || obj->volumes.empty())
            continue;
        const ModelInstance* inst = obj->instances.front();
        TriangleMesh mesh = obj->raw_mesh();
        mesh.transform(inst->get_transformation().get_matrix(/*dont_translate=*/true, /*dont_rotate=*/true));
        if (mesh.empty())
            continue;
        const BoundingBoxf3 box = mesh.bounding_box();
        char key[256];
        snprintf(key, sizeof(key), "%zu|%zu|%.1f|%.1f|%.1f|%.0f", mesh.its.vertices.size(), mesh.its.indices.size(), box.size().x(),
                 box.size().y(), box.size().z(), double(mesh.volume()));
        const Matrix3d rot  = inst->get_transformation().get_rotation_matrix().linear();
        const Vec3d    down = rot.transpose() * Vec3d(0, 0, -1);
        auto it = by_shape.find(key);
        if (it != by_shape.end() && groups[it->second].current_down.dot(down) > 0.999) {
            for (int k = 0; k < int(obj->instances.size()); ++k)
                groups[it->second].instances.emplace_back(obj->id().id, k);
            continue;
        }
        Group g;
        g.name         = obj->name;
        g.mesh         = mesh.its;
        g.current_down = down;
        g.model.add_object(*obj);
        for (int k = 0; k < int(obj->instances.size()); ++k)
            g.instances.emplace_back(obj->id().id, k);
        by_shape[key] = groups.size();
        groups.push_back(std::move(g));
    }
    if (groups.empty())
        return;

    // The same preparation "Slice plate" does before slicing.
    Model::setExtruderParams(config, int(pb->filament_presets.size()));
    {
        PrintConfig print_config;
        print_config.apply(config, true);
        Model::setPrintSpeedTable(config, print_config);
    }

    const bool slices = slicing_allowed();
    g_busy            = true;
    g_progress        = 0.0f;
    g_status          = _u8L("Finding the fastest way up...");
    g_undo_time       = 0;
    queue_job(plater->get_ui_job_worker(),
              std::make_unique<OrientJob>(std::move(groups), config, s, bed, s.max_height, slices, pb->is_bbl_vendor(), pb->is_cx_vendor(),
                                          pb->mixed_filaments));
}

} // namespace

bool busy() { return g_busy; }

float progress() { return std::clamp(g_progress.load(), 0.0f, 1.0f); }

std::string status_line() { return g_status; }

bool can_undo() { return g_undo_time != 0 && !g_busy; }

void undo()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr || g_undo_time == 0)
        return;
    plater->undo_redo_to(g_undo_time);
    g_undo_time       = 0;
    g_arrange_pending = false;
    g_status          = _u8L("Put back the way it was.");
}

void mark_all_seen()
{
    if (Plater* plater = wxGetApp().plater())
        for (const ModelObject* obj : plater->model().objects)
            if (obj != nullptr)
                g_seen.insert(obj->id().id);
}

void on_frame()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr || g_busy)
        return;
    Worker& worker = plater->get_ui_job_worker();

    if (g_arrange_pending && worker.is_idle()) {
        g_arrange_pending = false;
        plater->arrange();
        return;
    }

    // Parts that just arrived.
    Model&           model = plater->model();
    std::vector<int> fresh;
    for (int oi = 0; oi < int(model.objects.size()); ++oi)
        if (model.objects[oi] != nullptr && g_seen.count(model.objects[oi]->id().id) == 0)
            fresh.push_back(oi);
    if (fresh.empty())
        return;
    if (!worker.is_idle() || plater->is_background_process_slicing()) {
        if (GLCanvas3D* c = plater->get_current_canvas3D())
            c->schedule_extra_frame(300); // try again in a moment
        return;
    }
    for (int oi : fresh)
        g_seen.insert(model.objects[oi]->id().id);
    if (!RoostSlim::enabled() || wxGetApp().app_config->get("roost_auto_orient") == "0")
        return;

    // Never reshuffle a project that's already being printed.
    PartPlateList& plates = plater->get_partplate_list();
    for (int p = 0; p < plates.get_plate_count(); ++p)
        if (RoostStages::stage(p) != RoostStages::Stage::ToPrint) {
            g_status = _u8L("Kept the orientation: some plates are already sent.");
            return;
        }
    start(plater, fresh);
}

} // namespace RoostOrient
} // namespace GUI
} // namespace Slic3r
