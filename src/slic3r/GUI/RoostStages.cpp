// Roost Print: where every plate is up to. See RoostStages.hpp.
#include "RoostStages.hpp"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/Utils.hpp"

#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "Widgets/RoostBrand.hpp"

namespace Slic3r {
namespace GUI {
namespace RoostStages {

namespace {

struct Record
{
    Stage       stage{Stage::ToPrint};
    std::string file;     // the print's file name, as sent
    std::string ip;       // the printer it was sent to
    std::string printer;  // PrintCam's name for that printer, once seen
    int         progress{0};
    int64_t     sent{0};  // unix seconds it was sent
    int64_t     at{0};    // unix seconds of the last change
    bool        seen{false};   // seen on the printer, printing
    bool        manual{false}; // set by hand
};

std::map<std::string, Record> g_records;
bool                          g_loaded = false;

// Asked for every plate in every frame: remember each plate's key for a moment.
struct CachedKey
{
    std::string                           key;
    std::chrono::steady_clock::time_point at;
};
std::map<int, CachedKey> g_key_cache;

int64_t now_s() { return int64_t(std::time(nullptr)); }

std::string file_path() { return (boost::filesystem::path(data_dir()) / "roost_plate_stages.json").string(); }

const char* stage_id(Stage s)
{
    switch (s) {
    case Stage::Printing: return "printing";
    case Stage::Printed: return "printed";
    case Stage::Failed: return "failed";
    default: return "to_print";
    }
}

Stage stage_from_id(const std::string& s)
{
    if (s == "printing") return Stage::Printing;
    if (s == "printed") return Stage::Printed;
    if (s == "failed") return Stage::Failed;
    return Stage::ToPrint;
}

void load()
{
    if (g_loaded)
        return;
    g_loaded = true;
    try {
        boost::nowide::ifstream in(file_path());
        if (!in)
            return;
        const auto j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("plates") || !j["plates"].is_object())
            return;
        for (auto it = j["plates"].begin(); it != j["plates"].end(); ++it) {
            const auto& v = it.value();
            if (!v.is_object())
                continue;
            Record r;
            r.stage    = stage_from_id(v.value("stage", std::string()));
            r.file     = v.value("file", std::string());
            r.ip       = v.value("ip", std::string());
            r.printer  = v.value("printer", std::string());
            r.progress = v.value("progress", 0);
            r.sent     = v.value("sent", int64_t(0));
            r.at       = v.value("at", int64_t(0));
            r.seen     = v.value("seen", false);
            r.manual   = v.value("manual", false);
            g_records[it.key()] = r;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Roost Print: couldn't read the plate stages: " << e.what();
    }
}

void save()
{
    // Forget plates nobody has touched for 120 days.
    const int64_t old = now_s() - 120 * 24 * 3600;
    for (auto it = g_records.begin(); it != g_records.end();)
        it = it->second.at < old ? g_records.erase(it) : std::next(it);

    nlohmann::json plates = nlohmann::json::object();
    for (const auto& [key, r] : g_records)
        plates[key] = {{"stage", stage_id(r.stage)}, {"file", r.file},     {"ip", r.ip},   {"printer", r.printer},
                       {"progress", r.progress},     {"sent", r.sent},     {"at", r.at},   {"seen", r.seen},
                       {"manual", r.manual}};
    nlohmann::json j = {{"version", 1}, {"plates", plates}};
    try {
        const std::string path = file_path();
        const std::string tmp  = path + ".tmp";
        {
            boost::nowide::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << j.dump(1);
        }
        boost::filesystem::rename(tmp, path);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Roost Print: couldn't save the plate stages: " << e.what();
    }
}

void redraw()
{
    g_key_cache.clear();
    if (Plater* plater = wxGetApp().plater())
        if (GLCanvas3D* canvas = plater->get_current_canvas3D()) {
            canvas->set_as_dirty();
            canvas->request_extra_frame();
        }
}

// Which plate this is: the project, the plate's number, and what's on it (object names and how
// many of each), so a different file or a changed plate doesn't inherit another plate's stage.
std::string plate_key(int plate_index)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr || plate_index < 0)
        return {};
    const auto now    = std::chrono::steady_clock::now();
    auto       cached = g_key_cache.find(plate_index);
    if (cached != g_key_cache.end() && now - cached->second.at < std::chrono::milliseconds(400))
        return cached->second.key;

    std::string key;
    PartPlate*  plate = plater->get_partplate_list().get_plate(plate_index);
    if (plate != nullptr) {
        const Model&             model = plater->model();
        std::vector<std::string> things;
        for (int obj_idx = 0; obj_idx < int(model.objects.size()); ++obj_idx) {
            const ModelObject* obj = model.objects[obj_idx];
            if (obj == nullptr)
                continue;
            int count = 0;
            for (int i = 0; i < int(obj->instances.size()); ++i)
                if (plate->contain_instance(obj_idx, i))
                    ++count;
            if (count > 0)
                things.push_back(obj->name + " x" + std::to_string(count));
        }
        if (!things.empty()) {
            std::sort(things.begin(), things.end());
            std::string project = boost::algorithm::to_lower_copy(std::string(plater->get_project_name().ToUTF8().data()));
            key = project + "|plate " + std::to_string(plate_index + 1) + "|" + boost::algorithm::join(things, ", ");
            if (key.size() > 400) // lots of objects: keep the file small
                key = key.substr(0, 200) + "#" + std::to_string(std::hash<std::string>{}(key));
        }
    }
    g_key_cache[plate_index] = {key, now};
    return key;
}

// File names as the printer and PrintCam show them: no folders, no file type, any case.
std::string bare(const std::string& name)
{
    std::string s = name;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos)
        s = s.substr(slash + 1);
    boost::algorithm::to_lower(s);
    for (const char* ext : {".gcode.3mf", ".bgcode", ".gcode", ".3mf"}) {
        const size_t n = strlen(ext);
        if (s.size() > n && s.compare(s.size() - n, n, ext) == 0) {
            s.resize(s.size() - n);
            break;
        }
    }
    boost::algorithm::trim(s);
    return s;
}

bool active(const std::string& state) { return state == "printing" || state == "paused" || state == "preparing"; }

std::string ago(int64_t when)
{
    const int64_t s = std::max<int64_t>(0, now_s() - when);
    if (s < 90)
        return _u8L("just now");
    if (s < 3600)
        return format(_u8L("%1% minutes ago"), s / 60);
    if (s < 2 * 3600)
        return _u8L("an hour ago");
    if (s < 24 * 3600)
        return format(_u8L("%1% hours ago"), s / 3600);
    if (s < 2 * 24 * 3600)
        return _u8L("yesterday");
    return format(_u8L("%1% days ago"), s / (24 * 3600));
}

} // namespace

Stage stage(int plate_index)
{
    load();
    const std::string key = plate_key(plate_index);
    if (key.empty())
        return Stage::ToPrint;
    auto it = g_records.find(key);
    return it == g_records.end() ? Stage::ToPrint : it->second.stage;
}

std::string detail(int plate_index)
{
    load();
    const std::string key = plate_key(plate_index);
    auto              it  = key.empty() ? g_records.end() : g_records.find(key);
    if (it == g_records.end())
        return {};
    const Record&     r       = it->second;
    const std::string printer = !r.printer.empty() ? r.printer : r.ip;
    switch (r.stage) {
    case Stage::Printing:
        if (r.manual)
            return _u8L("Marked by hand");
        if (r.seen && !printer.empty())
            return format(_u8L("On %1%, %2%%%"), printer, r.progress);
        return printer.empty() ? _u8L("Sent, waiting for the printer") : format(_u8L("Sent to %1%"), printer);
    case Stage::Printed:
        return r.manual ? format(_u8L("Marked printed %1%"), ago(r.at)) :
                          (printer.empty() ? format(_u8L("Printed %1%"), ago(r.at)) : format(_u8L("Printed on %1%, %2%"), printer, ago(r.at)));
    case Stage::Failed:
        return r.manual ? _u8L("Marked failed by hand") :
                          (printer.empty() ? format(_u8L("Stopped %1%"), ago(r.at)) : format(_u8L("Stopped on %1%, %2%"), printer, ago(r.at)));
    default: return {};
    }
}

void set_stage(int plate_index, Stage s)
{
    load();
    const std::string key = plate_key(plate_index);
    if (key.empty())
        return;
    if (s == Stage::ToPrint) {
        g_records.erase(key);
    } else {
        Record& r = g_records[key];
        r.stage   = s;
        r.manual  = true;
        r.at      = now_s();
        if (s == Stage::Printed)
            r.progress = 100;
    }
    BOOST_LOG_TRIVIAL(info) << "Roost Print: plate " << plate_index + 1 << " marked " << stage_id(s) << " by hand";
    save();
    redraw();
}

void on_sent(int plate_index, const std::string& file_name, const std::string& printer_ip)
{
    load();
    g_key_cache.erase(plate_index);
    const std::string key = plate_key(plate_index);
    if (key.empty())
        return;
    Record r;
    r.stage = Stage::Printing;
    r.file  = file_name;
    r.ip    = printer_ip;
    r.sent = r.at = now_s();
    g_records[key] = r;
    BOOST_LOG_TRIVIAL(info) << "Roost Print: plate " << plate_index + 1 << " sent to " << printer_ip << " as " << file_name;
    save();
    redraw();
}

bool on_printers(const std::vector<PrinterJob>& printers)
{
    load();
    bool          changed = false;
    const int64_t now     = now_s();
    for (auto& [key, r] : g_records) {
        if (r.manual || r.file.empty() || r.stage != Stage::Printing)
            continue; // only plates that were sent and aren't finished yet
        const std::string mine = bare(r.file);
        const PrinterJob* job  = nullptr;
        for (const PrinterJob& p : printers)
            if (!p.file.empty() && bare(p.file) == mine && (r.ip.empty() || p.ip.empty() || p.ip == r.ip)) {
                job = &p;
                break;
            }
        Stage new_stage = r.stage;
        if (job != nullptr) {
            if (!job->printer.empty() && job->printer != r.printer) {
                r.printer = job->printer;
                changed   = true;
            }
            if (active(job->state)) {
                if (!r.seen || r.progress != job->progress) {
                    r.seen     = true;
                    r.progress = job->progress;
                    changed    = true;
                }
            } else if (r.seen || now - r.sent > 90) {
                // Finished or stopped. (Right after sending, a printer can still show the last print
                // of a file with the same name; only believe it once ours has been seen printing.)
                if (job->state == "finished")
                    new_stage = Stage::Printed;
                else if (job->state == "stopped" || job->state == "error")
                    new_stage = Stage::Failed;
            }
        } else if (r.seen && !r.ip.empty()) {
            // The printer it was on has moved on to another file without us seeing the end.
            for (const PrinterJob& p : printers)
                if (p.ip == r.ip && !p.file.empty() && bare(p.file) != mine) {
                    new_stage = r.progress >= 95 ? Stage::Printed : Stage::Failed;
                    break;
                }
        }
        if (new_stage != r.stage) {
            r.stage = new_stage;
            r.at    = now;
            if (new_stage == Stage::Printed)
                r.progress = 100;
            changed = true;
            BOOST_LOG_TRIVIAL(info) << "Roost Print: " << r.file << " is now " << stage_id(new_stage);
        }
    }
    if (changed) {
        save();
        redraw();
    }
    return changed;
}

bool confirm_send(wxWindow* parent, int plate_index)
{
    const Stage s = stage(plate_index);
    if (s != Stage::Printed && s != Stage::Printing)
        return true;
    const wxString msg = s == Stage::Printed ?
                             wxString::Format(_L("Plate %d is already printed (%s).\nSend it again?"), plate_index + 1,
                                              wxString::FromUTF8(detail(plate_index))) :
                             wxString::Format(_L("Plate %d is printing right now (%s).\nSend it again?"), plate_index + 1,
                                              wxString::FromUTF8(detail(plate_index)));
    MessageDialog dlg(parent, msg, _L("Send again?"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    return dlg.ShowModal() == wxID_YES;
}

std::string name(Stage s)
{
    switch (s) {
    case Stage::Printing: return _u8L("Printing");
    case Stage::Printed: return _u8L("Printed");
    case Stage::Failed: return _u8L("Failed");
    default: return _u8L("To print");
    }
}

ColorRGBA color(Stage s)
{
    auto rgb = [](const wxColour& c) { return ColorRGBA(c.Red() / 255.0f, c.Green() / 255.0f, c.Blue() / 255.0f, 1.0f); };
    switch (s) {
    case Stage::Printing: return rgb(RoostBrand::kYolk);
    case Stage::Printed: return ColorRGBA(0x2F / 255.0f, 0xB3 / 255.0f, 0x5C / 255.0f, 1.0f); // fresh green
    case Stage::Failed: return ColorRGBA(0xE5 / 255.0f, 0x48 / 255.0f, 0x3B / 255.0f, 1.0f);  // warning red
    default: return rgb(RoostBrand::kBlue);
    }
}

} // namespace RoostStages
} // namespace GUI
} // namespace Slic3r
