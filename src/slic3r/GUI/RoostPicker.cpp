// Roost Print: the best printer for each plate. See RoostPicker.hpp.
#include "RoostPicker.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/format.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "RoostOrient.hpp"
#include "RoostStages.hpp"
#include "print_manage/data/DataCenter.hpp"
#include "simple/DeviceListSimple.hpp"

namespace Slic3r {
namespace GUI {
namespace RoostPicker {

namespace {

using Clock = std::chrono::steady_clock;
using json  = nlohmann::json;

// ---------------------------------------------------------------- small JSON helpers
// The device list comes from a web page; never trust a field's type.
std::string jstr(const json& o, const char* key)
{
    if (!o.is_object() || !o.contains(key))
        return std::string();
    const json& v = o[key];
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number_integer())
        return std::to_string(v.get<long long>());
    if (v.is_number())
        return std::to_string(v.get<double>());
    return std::string();
}

long long jint(const json& o, const char* key, long long def)
{
    if (!o.is_object() || !o.contains(key))
        return def;
    const json& v = o[key];
    if (v.is_number_integer() || v.is_number_unsigned())
        return v.get<long long>();
    if (v.is_number_float())
        return (long long) std::llround(v.get<double>());
    if (v.is_boolean())
        return v.get<bool>() ? 1 : 0;
    if (v.is_string()) {
        try {
            return std::stoll(v.get<std::string>());
        } catch (...) {
        }
    }
    return def;
}

double jnum(const json& o, const char* key, double def)
{
    if (!o.is_object() || !o.contains(key))
        return def;
    const json& v = o[key];
    if (v.is_number())
        return v.get<double>();
    if (v.is_string()) {
        try {
            return std::stod(v.get<std::string>());
        } catch (...) {
        }
    }
    return def;
}

bool jbool(const json& o, const char* key, bool def)
{
    if (!o.is_object() || !o.contains(key))
        return def;
    const json& v = o[key];
    if (v.is_boolean())
        return v.get<bool>();
    if (v.is_number())
        return v.get<double>() != 0.0;
    return def;
}

// ---------------------------------------------------------------- words
// "K2 Plus", "k2plus", "K2-Plus" all compare the same.
std::string squash(const std::string& s)
{
    std::string out;
    for (char c : s)
        if (std::isalnum((unsigned char) c))
            out += char(std::tolower((unsigned char) c));
    return out;
}

// "PLA-CF" -> "PLACF", "pla+" -> "PLA": the material family the printer and slicer both use.
std::string type_key(const std::string& s)
{
    std::string out;
    for (char c : s)
        if (std::isalnum((unsigned char) c))
            out += char(std::toupper((unsigned char) c));
    return out;
}

std::string nozzle_words(double d)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", d);
    return std::string(buf);
}

std::string join(const std::vector<std::string>& v, const std::string& sep)
{
    std::string out;
    for (size_t i = 0; i < v.size(); ++i)
        out += (i > 0 ? sep : std::string()) + v[i];
    return out;
}

// ---------------------------------------------------------------- printer model names
// The printers report a model code ("F008" for the K2 Plus); the slicer's printer presets say
// "Creality K2 Plus". machineList.json (shipped with the printer profiles) links the two.
const std::map<std::string, std::string>& model_names()
{
    static std::map<std::string, std::string> names;
    static bool                               loaded = false;
    if (loaded)
        return names;
    loaded = true;
    names  = {{"F008", "K2 Plus"}, {"K1C", "K1C"}, {"CR-K1", "K1"}, {"K1", "K1"}, {"K1Max", "K1 Max"},
              {"K1 Max", "K1 Max"}, {"CR-K1 Max", "K1 Max"}, {"F021", "K2"}, {"F012", "K2 Pro"}};
    const std::vector<boost::filesystem::path> files = {
        boost::filesystem::path(data_dir()) / "system" / "Creality" / "machineList.json",
        boost::filesystem::path(resources_dir()) / "profiles" / "Creality" / "machineList.json"};
    for (const auto& file : files) {
        try {
            if (!boost::filesystem::exists(file))
                continue;
            boost::nowide::ifstream in(file.string());
            const json j = json::parse(in, nullptr, false);
            if (j.is_discarded() || !j.is_object() || !j.contains("printerList") || !j["printerList"].is_array())
                continue;
            for (const json& p : j["printerList"]) {
                const std::string code = jstr(p, "printerIntName"), name = jstr(p, "name");
                if (!code.empty() && !name.empty())
                    names[code] = name;
            }
            break;
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "Roost Print: couldn't read the printer model list: " << e.what();
        }
    }
    return names;
}

// "Creality K2 Plus" -> "K2 Plus".
std::string preset_model_name(const std::string& printer_model)
{
    std::string s = printer_model;
    if (boost::istarts_with(s, "Creality "))
        s = s.substr(9);
    return s;
}

std::string model_name_for(const std::string& code, const std::string& reported_name)
{
    const auto& names = model_names();
    const auto  it    = names.find(code);
    if (it != names.end())
        return preset_model_name(it->second);
    if (!reported_name.empty())
        return preset_model_name(reported_name);
    return code;
}

// ---------------------------------------------------------------- colours
bool parse_hex(const std::string& s, int& r, int& g, int& b)
{
    std::string hex;
    for (char c : s)
        if (std::isxdigit((unsigned char) c))
            hex += c;
    if (hex.size() < 6)
        return false;
    hex = hex.substr(hex.size() - 6); // "#0FFFFFF" and "FFAABBCC" both end in RRGGBB
    unsigned int v = 0;
    if (std::sscanf(hex.c_str(), "%06x", &v) != 1)
        return false;
    r = int((v >> 16) & 0xFF);
    g = int((v >> 8) & 0xFF);
    b = int(v & 0xFF);
    return true;
}

std::string to_hex(int r, int g, int b)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r & 0xFF, g & 0xFF, b & 0xFF);
    return std::string(buf);
}

std::string normalise_hex(const std::string& s)
{
    int r, g, b;
    return parse_hex(s, r, g, b) ? to_hex(r, g, b) : std::string();
}

// How different two colours look ("redmean", roughly 0..765).
double colour_distance(int r1, int g1, int b1, int r2, int g2, int b2)
{
    const double rm = (r1 + r2) / 2.0;
    const double dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
    return std::sqrt((2.0 + rm / 256.0) * dr * dr + 4.0 * dg * dg + (2.0 + (255.0 - rm) / 256.0) * db * db);
}

const std::vector<std::pair<std::string, std::string>>& colour_list()
{
    static const std::vector<std::pair<std::string, std::string>> list = {
        {"Black", "#1A1A1A"},  {"White", "#F5F5F5"}, {"Grey", "#8A8D91"},   {"Silver", "#C4C7CA"}, {"Red", "#D32F2F"},
        {"Orange", "#F57C00"}, {"Yellow", "#FBC02D"}, {"Green", "#2E9D4A"},  {"Blue", "#1E88E5"},   {"Navy", "#1A2A6C"},
        {"Purple", "#7B1FA2"}, {"Pink", "#F06292"},   {"Brown", "#6D4C41"},  {"Beige", "#D9C7A3"}};
    return list;
}

bool colours_match(const std::string& a, const std::string& b)
{
    int r1, g1, b1, r2, g2, b2;
    if (!parse_hex(a, r1, g1, b1) || !parse_hex(b, r2, g2, b2))
        return true; // a colour we don't know can't rule a printer out
    if (colour_name(a) == colour_name(b))
        return true;
    return colour_distance(r1, g1, b1, r2, g2, b2) < 110.0;
}

// ---------------------------------------------------------------- saved on this computer
struct Saved
{
    std::map<std::string, HandSpool>   hand;    // mac -> spool set by hand
    std::map<std::string, std::string> cleared; // mac -> the finished print that was taken off its bed
};
Saved g_saved;
bool  g_saved_loaded = false;

std::string saved_path() { return (boost::filesystem::path(data_dir()) / "roost_hand_spools.json").string(); }

void load_saved()
{
    if (g_saved_loaded)
        return;
    g_saved_loaded = true;
    try {
        boost::nowide::ifstream in(saved_path());
        if (!in)
            return;
        const json j = json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return;
        if (j.contains("printers") && j["printers"].is_object())
            for (auto it = j["printers"].begin(); it != j["printers"].end(); ++it) {
                HandSpool s;
                s.type   = jstr(it.value(), "type");
                s.colour = normalise_hex(jstr(it.value(), "colour"));
                s.nozzle = jnum(it.value(), "nozzle", 0.0);
                g_saved.hand[it.key()] = s;
            }
        if (j.contains("bed_cleared") && j["bed_cleared"].is_object())
            for (auto it = j["bed_cleared"].begin(); it != j["bed_cleared"].end(); ++it)
                if (it.value().is_string())
                    g_saved.cleared[it.key()] = it.value().get<std::string>();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Roost Print: couldn't read the hand-set spools: " << e.what();
    }
}

void save_saved()
{
    json printers = json::object();
    for (const auto& [mac, s] : g_saved.hand)
        printers[mac] = {{"type", s.type}, {"colour", s.colour}, {"nozzle", s.nozzle}};
    json cleared = json::object();
    for (const auto& [mac, id] : g_saved.cleared)
        cleared[mac] = id;
    const json j = {{"version", 1}, {"printers", printers}, {"bed_cleared", cleared}};
    try {
        const std::string path = saved_path();
        const std::string tmp  = path + ".tmp";
        {
            boost::nowide::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << j.dump(1);
        }
        boost::filesystem::rename(tmp, path);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Roost Print: couldn't save the hand-set spools: " << e.what();
    }
}

// ---------------------------------------------------------------- state
Ranking           g_rank;
Clock::time_point g_rank_at{};
std::string       g_rank_sig;

std::string       g_choice;     // mac chosen by hand
std::string       g_choice_sig; // ...for this plate

struct Pending
{
    int               plate{-1};
    std::string       ip, mac, name;
    Clock::time_point at{};
};
Pending g_pending;

std::map<std::string, Clock::time_point> g_claimed; // mac -> when a plate was sent there
std::set<std::string>                    g_auto_tried;
std::set<std::string>                    g_auto_jumped; // plates auto-send has moved the view to (once each)

const auto kClaim   = std::chrono::minutes(3);
const auto kPending = std::chrono::seconds(60);

bool claimed(const std::string& mac)
{
    const auto it = g_claimed.find(mac);
    return it != g_claimed.end() && Clock::now() - it->second < kClaim;
}

// Which plate this is: the project, the plate number and what's on it.
std::string plate_sig(Plater* plater, int plate_index)
{
    std::string s = std::string(plater->get_project_name().ToUTF8().data()) + "|" + std::to_string(plate_index);
    PartPlate* plate = plater->get_partplate_list().get_plate(plate_index);
    if (plate == nullptr)
        return s;
    const Model& model = plater->model();
    for (int i = 0; i < int(model.objects.size()); ++i) {
        const ModelObject* obj = model.objects[i];
        if (obj == nullptr)
            continue;
        int n = 0;
        for (int k = 0; k < int(obj->instances.size()); ++k)
            if (plate->contain_instance(i, k))
                ++n;
        if (n > 0)
            s += "|" + std::to_string(obj->id().id) + "x" + std::to_string(n);
    }
    return s;
}

// ---------------------------------------------------------------- the printers
std::vector<Printer> read_printers()
{
    load_saved();
    std::vector<Printer> out;
    const json&          all = DM::DataCenter::Ins().get_data();
    if (!all.is_object() || !all.contains("data") || !all["data"].is_object())
        return out;
    const json& data = all["data"];
    if (!data.contains("printerList") || !data["printerList"].is_array())
        return out;

    for (const json& group : data["printerList"]) {
        if (!group.is_object() || !group.contains("list") || !group["list"].is_array())
            continue;
        for (const json& p : group["list"]) {
            if (!p.is_object() || jint(p, "deviceType", 0) != 0) // printers on the shop network only
                continue;
            Printer pr;
            pr.mac     = jstr(p, "mac");
            pr.address = jstr(p, "address");
            if (pr.mac.empty() || pr.address.empty())
                continue;
            pr.name       = jstr(p, "name");
            pr.model      = jstr(p, "model");
            pr.model_name = model_name_for(pr.model, jstr(p, "modelName"));
            if (pr.name.empty())
                pr.name = pr.model_name.empty() ? pr.address : pr.model_name;
            pr.online = jbool(p, "online", false);

            // deviceState: 0 idle, 1 printing (10000 = nothing heard yet). state: the print itself,
            // 0 none, 1 printing, 2 finished, 3 failed, 4 stopped, 5 paused. (Device page, deviceMgr.)
            const long long device_state = jint(p, "deviceState", 10000);
            const long long print_state  = jint(p, "state", -1);
            pr.printing = pr.online && (device_state == 1 || print_state == 1 || print_state == 5);
            pr.idle     = pr.online && device_state == 0 && !pr.printing;
            pr.progress = int(jint(p, "printProgress", 0));
            if (p.contains("err") && p["err"].is_object()) {
                const long long key = jint(p["err"], "key", 0);
                // 30000 and 30010 aren't faults (the Device page doesn't show them either).
                pr.error = pr.online && key > 0 && key != 30000 && key != 30010;
            }
            // A finished, failed or stopped print stays on the bed until someone takes it off.
            if (print_state == 2 || print_state == 3 || print_state == 4) {
                std::string start;
                if (p.contains("printStartTime") && !p["printStartTime"].is_null())
                    start = p["printStartTime"].dump();
                const std::string id = jstr(p, "printFileName") + "|" + start;
                const auto        it = g_saved.cleared.find(pr.mac);
                pr.bed_clear         = it != g_saved.cleared.end() && it->second == id;
            }
            if (p.contains("temperature") && p["temperature"].is_object() && p["temperature"].contains("nozzle") &&
                p["temperature"]["nozzle"].is_object())
                pr.nozzle = jnum(p["temperature"]["nozzle"], "size", 0.0);

            // What's loaded, as the printer reports it (K2 Plus: the CFS reads RFID tags).
            if (p.contains("boxsInfo") && p["boxsInfo"].is_object() && p["boxsInfo"].contains("materialBoxs") &&
                p["boxsInfo"]["materialBoxs"].is_array() && !p["boxsInfo"]["materialBoxs"].empty()) {
                pr.reads_spools = true;
                for (const json& box : p["boxsInfo"]["materialBoxs"]) {
                    if (!box.is_object() || !box.contains("materials") || !box["materials"].is_array())
                        continue;
                    const long long box_type = jint(box, "type", 0); // 0 CFS, 1 the side spool
                    const long long box_id   = jint(box, "id", 0);
                    for (const json& m : box["materials"]) {
                        Spool s;
                        s.type = boost::trim_copy(jstr(m, "type"));
                        if (s.type.empty() || s.type == "-" || s.type == "?")
                            continue; // empty slot
                        s.colour = normalise_hex(jstr(m, "color"));
                        const long long slot = jint(m, "id", 0);
                        s.where = box_type == 1 ? _u8L("Side spool") :
                                                  format(_u8L("CFS %1% slot %2%"), box_id, std::string(1, char('A' + std::clamp<long long>(slot, 0, 25))));
                        pr.spools.push_back(s);
                    }
                }
            }
            // Set by hand (the K1C): wins over anything reported.
            const auto hand = g_saved.hand.find(pr.mac);
            if (hand != g_saved.hand.end()) {
                if (!hand->second.type.empty()) {
                    pr.spools   = {Spool{hand->second.type, hand->second.colour, _u8L("Set by hand")}};
                    pr.hand_set = true;
                }
                if (hand->second.nozzle > 0.0)
                    pr.nozzle = hand->second.nozzle;
            }

            // The same printer can be listed twice (old address); keep the one that's online.
            auto same = std::find_if(out.begin(), out.end(), [&](const Printer& o) { return o.mac == pr.mac; });
            if (same == out.end())
                out.push_back(pr);
            else if (pr.online && !same->online)
                *same = pr;
        }
    }
    return out;
}

// ---------------------------------------------------------------- the plate
bool read_need(Need& need, int& plate_index)
{
    Plater*       plater = wxGetApp().plater();
    PresetBundle* pb     = wxGetApp().preset_bundle;
    if (plater == nullptr || pb == nullptr)
        return false;
    plate_index      = plater->get_partplate_list().get_curr_plate_index();
    PartPlate* plate = plater->get_partplate_list().get_plate(plate_index);
    if (plate == nullptr || plate->get_objects_on_this_plate().empty())
        return false;

    const DynamicPrintConfig& printer = pb->printers.get_edited_preset().config;
    if (const ConfigOptionString* o = printer.option<ConfigOptionString>("printer_model"))
        need.model_name = preset_model_name(o->value);
    if (const ConfigOptionFloats* o = printer.option<ConfigOptionFloats>("nozzle_diameter"))
        if (!o->values.empty())
            need.nozzle = o->values.front();

    std::vector<int> used = plate->is_slice_result_valid() ? plate->get_used_extruders() : std::vector<int>();
    if (used.empty())
        used = plate->get_extruders();
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());

    const ConfigOptionStrings* colours = pb->project_config.option<ConfigOptionStrings>("filament_colour");
    for (int e : used) {
        const int i = e - 1; // 1-based
        if (i < 0 || i >= int(pb->filament_presets.size()))
            continue;
        Need::Material m;
        if (const Preset* f = pb->filaments.find_preset(pb->filament_presets[i], false))
            if (const ConfigOptionStrings* t = f->config.option<ConfigOptionStrings>("filament_type"))
                if (!t->values.empty())
                    m.type = t->values.front();
        if (colours != nullptr && i < int(colours->values.size()))
            m.colour = normalise_hex(colours->values[i]);
        if (m.type.empty())
            m.type = "PLA";
        need.materials.push_back(m);
    }
    return true;
}

// ---------------------------------------------------------------- ranking one printer
Pick rank_one(const Printer& pr, const Need& need)
{
    Pick pick;
    pick.printer = pr;

    const bool model_ok  = need.model_name.empty() || squash(pr.model_name) == squash(need.model_name);
    const bool nozzle_ok = pr.nozzle <= 0.0 || need.nozzle <= 0.0 || std::abs(pr.nozzle - need.nozzle) < 0.01;
    const bool is_claimed = claimed(pr.mac);
    const bool ready     = pr.online && pr.idle && !pr.error && pr.bed_clear && !is_claimed;

    // Material: every one the plate uses must be loaded (type), in the right colour when known.
    const bool               known    = !pr.spools.empty();
    bool                     types_ok = known, colours_ok = known;
    std::vector<std::string> loaded_words;
    std::string              missing_type, wrong_colour;
    for (const Need::Material& m : need.materials) {
        const Spool* by_type = nullptr;
        const Spool* exact   = nullptr;
        for (const Spool& s : pr.spools)
            if (type_key(s.type) == type_key(m.type)) {
                if (by_type == nullptr)
                    by_type = &s;
                if (exact == nullptr && (s.colour.empty() || colours_match(s.colour, m.colour)))
                    exact = &s;
            }
        if (exact != nullptr) {
            loaded_words.push_back(material_words({exact->type, exact->colour}));
        } else if (by_type != nullptr) {
            colours_ok = false;
            if (wrong_colour.empty())
                wrong_colour = format(_u8L("has %1%, not %2%"), material_words({by_type->type, by_type->colour}), colour_name(m.colour));
        } else {
            types_ok = colours_ok = false;
            if (missing_type.empty())
                missing_type = m.type;
        }
    }

    pick.suits    = ready && model_ok && nozzle_ok && types_ok && colours_ok;
    pick.can_send = pr.online && !pr.printing;
    pick.score    = (ready ? 1000000LL : 0) + (model_ok ? 100000LL : 0) + (nozzle_ok ? 10000LL : 0) + (types_ok ? 1000LL : 0) +
                 (colours_ok ? 100LL : 0) + (known ? 10LL : 0) + (pr.online ? 1LL : 0);

    // Why, in a few words. The first thing that's wrong, else what's good about it.
    if (!pr.online)
        pick.reason = _u8L("offline");
    else if (pr.printing)
        pick.reason = format(_u8L("busy, %1%%%"), pr.progress);
    else if (is_claimed)
        pick.reason = _u8L("just sent a plate");
    else if (pr.error)
        pick.reason = _u8L("has an error");
    else if (!pr.bed_clear)
        pick.reason = _u8L("last print may still be on the bed");
    else if (!pr.idle)
        pick.reason = _u8L("not ready yet");
    else if (!model_ok)
        pick.reason = format(_u8L("a %1%, plate is for %2%"), pr.model_name, need.model_name);
    else if (!nozzle_ok)
        pick.reason = format(_u8L("has a %1% nozzle"), nozzle_words(pr.nozzle));
    else if (!known)
        pick.reason = pr.reads_spools ? _u8L("no spools loaded") : _u8L("spool not set");
    else if (!types_ok)
        pick.reason = format(_u8L("no %1% loaded"), missing_type);
    else if (!colours_ok)
        pick.reason = wrong_colour;
    else
        pick.reason = _u8L("idle") + ", " + join(loaded_words, " + ") + " " + _u8L("loaded");
    return pick;
}

// When nothing suits: why, in plain words.
std::string why_none(const std::vector<Pick>& picks, const Need& need)
{
    if (picks.empty())
        return _u8L("No printers yet. Add them on the Device page.");
    const std::string model = need.model_name.empty() ? _u8L("printer") : need.model_name;
    std::vector<std::string> mats;
    for (const Need::Material& m : need.materials)
        mats.push_back(material_words(m));
    const std::string with = mats.empty() ? std::string() : " " + format(_u8L("with %1% loaded"), join(mats, " " + _u8L("and") + " "));

    int of_model = 0, online = 0, ready = 0, ready_nozzle = 0;
    for (const Pick& p : picks) {
        const Printer& pr = p.printer;
        if (!need.model_name.empty() && squash(pr.model_name) != squash(need.model_name))
            continue;
        ++of_model;
        if (!pr.online)
            continue;
        ++online;
        if (!(pr.idle && !pr.error && pr.bed_clear && !claimed(pr.mac)))
            continue;
        ++ready;
        if (pr.nozzle <= 0.0 || need.nozzle <= 0.0 || std::abs(pr.nozzle - need.nozzle) < 0.01)
            ++ready_nozzle;
    }
    if (of_model == 0)
        return format(_u8L("No %1% in the printer list."), model);
    if (online == 0)
        return format(_u8L("Every %1% is offline."), model);
    if (ready == 0)
        return format(_u8L("No idle %1%."), model);
    if (ready_nozzle == 0)
        return format(_u8L("No idle %1% with a %2% nozzle."), model, nozzle_words(need.nozzle));
    return format(_u8L("No idle %1%%2%."), model, with);
}

void rebuild()
{
    Ranking r;
    int     plate_index = -1;
    if (!read_need(r.need, plate_index)) {
        g_rank = r;
        return;
    }
    const std::vector<Printer> printers = read_printers();
    r.any_printers                      = !printers.empty();
    for (const Printer& pr : printers)
        r.picks.push_back(rank_one(pr, r.need));
    std::stable_sort(r.picks.begin(), r.picks.end(), [](const Pick& a, const Pick& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.printer.name < b.printer.name;
    });
    r.valid = true;
    if (r.picks.empty() || !r.picks.front().suits) {
        r.why_none = why_none(r.picks, r.need);
        // Would one suit if its last print had been taken off? Then ask about that one.
        for (const Pick& p : r.picks) {
            if (p.printer.bed_clear || !p.printer.online || !p.printer.idle || p.printer.error)
                continue;
            Printer cleared   = p.printer;
            cleared.bed_clear = true;
            if (rank_one(cleared, r.need).suits) {
                r.bed_check_mac  = p.printer.mac;
                r.bed_check_name = p.printer.name;
                r.why_none += " " + format(_u8L("Is %1%'s bed clear?"), p.printer.name);
                break;
            }
        }
    }
    g_rank = std::move(r);
}

} // namespace

// ================================================================ public

std::string colour_name(const std::string& hex)
{
    int r, g, b;
    if (!parse_hex(hex, r, g, b))
        return std::string();
    std::string best;
    double      best_d = 1e9;
    for (const auto& [name, h] : colour_list()) {
        int r2, g2, b2;
        if (!parse_hex(h, r2, g2, b2))
            continue;
        const double d = colour_distance(r, g, b, r2, g2, b2);
        if (d < best_d) {
            best_d = d;
            best   = name;
        }
    }
    return _u8L(best.c_str());
}

std::string material_words(const Need::Material& m)
{
    const std::string c = colour_name(m.colour);
    return c.empty() ? m.type : m.type + " " + c;
}

const Ranking& ranking()
{
    const auto now = Clock::now();
    std::string sig;
    if (Plater* plater = wxGetApp().plater())
        sig = plate_sig(plater, plater->get_partplate_list().get_curr_plate_index());
    if (sig != g_rank_sig || now - g_rank_at > std::chrono::milliseconds(800)) {
        g_rank_sig = sig;
        g_rank_at  = now;
        try {
            rebuild();
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "Roost Print: couldn't rank the printers: " << e.what();
            g_rank = Ranking();
        }
    }
    return g_rank;
}

const Pick* best()
{
    const Ranking& r = ranking();
    if (!r.valid || r.picks.empty() || !r.picks.front().suits)
        return nullptr;
    return &r.picks.front();
}

std::string chosen_mac()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr || g_choice.empty())
        return std::string();
    return plate_sig(plater, plater->get_partplate_list().get_curr_plate_index()) == g_choice_sig ? g_choice : std::string();
}

void choose(const std::string& mac)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    g_choice     = mac;
    g_choice_sig = plate_sig(plater, plater->get_partplate_list().get_curr_plate_index());
}

bool send_to(const std::string& mac)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr || mac.empty())
        return false;
    const Ranking& r = ranking();
    const auto     it = std::find_if(r.picks.begin(), r.picks.end(), [&](const Pick& p) { return p.printer.mac == mac; });
    if (it == r.picks.end() || !it->printer.online) {
        BOOST_LOG_TRIVIAL(warning) << "Roost Print: can't send to " << mac << ", it's not online";
        return false;
    }
    const Printer pr = it->printer;
    // The send window picks the current printer when it opens, so make this one current first.
    SimpleDeviceMgr::instance().switch_current_device_simple(pr.mac, false);
    g_pending.plate = plater->get_partplate_list().get_curr_plate_index();
    g_pending.ip    = pr.address;
    g_pending.mac   = pr.mac;
    g_pending.name  = pr.name;
    g_pending.at    = Clock::now();
    BOOST_LOG_TRIVIAL(info) << "Roost Print: sending plate " << g_pending.plate + 1 << " to " << pr.name << " (" << pr.address << ")";
    // The normal send path: asks before re-sending a printed plate, asks for the order number,
    // opens the send window (not blocking), marks the plate Printing when it's started.
    const bool opened = plater->send_to_local_net_printer();
    if (!opened)
        g_pending = Pending(); // cancelled before the window opened
    return opened;
}

bool take_pending(int plate_index, std::string& ip, std::string& mac, std::string& name)
{
    const Pending p = g_pending;
    g_pending       = Pending();
    if (p.plate < 0 || p.plate != plate_index || p.ip.empty() || Clock::now() - p.at > kPending)
        return false;
    ip   = p.ip;
    mac  = p.mac;
    name = p.name;
    g_claimed[mac] = Clock::now(); // don't hand this printer another plate while this one starts
    return true;
}

bool auto_send()
{
    AppConfig* cfg = wxGetApp().app_config;
    return cfg != nullptr && cfg->get("roost_auto_send") == "1";
}

void set_auto_send(bool on)
{
    if (AppConfig* cfg = wxGetApp().app_config)
        cfg->set("roost_auto_send", on ? "1" : "0");
    if (on) {
        // Only plates sliced from now on: don't fire at whatever is on screen already.
        g_auto_tried.clear();
        g_auto_jumped.clear();
        if (Plater* plater = wxGetApp().plater())
            g_auto_tried.insert(plate_sig(plater, plater->get_partplate_list().get_curr_plate_index()));
    }
}

void on_frame()
{
    if (!auto_send())
        return;
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    if (plater->is_background_process_slicing() || RoostOrient::busy())
        return;
    if (g_pending.plate >= 0 && Clock::now() - g_pending.at < kPending)
        return; // a send window is opening
    PartPlateList& plates = plater->get_partplate_list();
    // A plate auto-send still has to do: has parts, is To print (never printing, printed or
    // failed), and hasn't been tried yet (sent, or the send was cancelled).
    auto waiting = [&](int i) {
        PartPlate* p = plates.get_plate(i);
        return p != nullptr && !p->get_objects_on_this_plate().empty() &&
               RoostStages::stage(i) == RoostStages::Stage::ToPrint && g_auto_tried.count(plate_sig(plater, i)) == 0;
    };
    const int plate_index = plates.get_curr_plate_index();
    if (!waiting(plate_index)) {
        // Every plate in the file, not just the one on screen: go to the next one waiting.
        // Selecting it slices it; the next frames send it.
        for (int i = 0; i < plates.get_plate_count(); ++i)
            if (i != plate_index && waiting(i) && g_auto_jumped.insert(plate_sig(plater, i)).second) {
                BOOST_LOG_TRIVIAL(info) << "Roost Print: auto-send moving on to plate " << i + 1;
                wxGetApp().CallAfter([i]() {
                    if (Plater* p = wxGetApp().plater())
                        p->select_plate(i, true);
                });
                return;
            }
        return;
    }
    PartPlate* plate = plates.get_plate(plate_index);
    if (!plate->is_slice_result_valid())
        return;
    const std::string sig = plate_sig(plater, plate_index);
    const Pick* b = best();
    if (b == nullptr)
        return;
    g_auto_tried.insert(sig);
    const std::string mac = b->printer.mac;
    BOOST_LOG_TRIVIAL(info) << "Roost Print: auto-sending plate " << plate_index + 1 << " to " << b->printer.name;
    wxGetApp().CallAfter([mac]() { send_to(mac); });
}

HandSpool hand_spool(const std::string& mac)
{
    load_saved();
    const auto it = g_saved.hand.find(mac);
    return it == g_saved.hand.end() ? HandSpool() : it->second;
}

void set_hand_spool(const std::string& mac, const HandSpool& spool)
{
    load_saved();
    if (spool.type.empty() && spool.nozzle <= 0.0)
        g_saved.hand.erase(mac);
    else
        g_saved.hand[mac] = spool;
    save_saved();
    g_rank_at = Clock::time_point{}; // rank again now
}

std::vector<Printer> hand_set_printers()
{
    std::vector<Printer> out;
    try {
        for (const Printer& p : read_printers())
            if (!p.reads_spools || p.hand_set)
                out.push_back(p);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Roost Print: couldn't list the printers: " << e.what();
    }
    std::sort(out.begin(), out.end(), [](const Printer& a, const Printer& b) { return a.name < b.name; });
    return out;
}

const std::vector<std::string>& hand_types()
{
    static const std::vector<std::string> types = {"PLA", "PLA-CF", "PETG", "PETG-CF", "ABS", "ASA", "TPU", "PA", "PC"};
    return types;
}

const std::vector<std::pair<std::string, std::string>>& hand_colours() { return colour_list(); }

void mark_bed_clear(const std::string& mac)
{
    load_saved();
    const json& all = DM::DataCenter::Ins().get_data();
    if (!all.is_object() || !all.contains("data") || !all["data"].is_object() || !all["data"].contains("printerList") ||
        !all["data"]["printerList"].is_array())
        return;
    for (const json& group : all["data"]["printerList"]) {
        if (!group.is_object() || !group.contains("list") || !group["list"].is_array())
            continue;
        for (const json& p : group["list"]) {
            if (!p.is_object() || jstr(p, "mac") != mac)
                continue;
            std::string start;
            if (p.contains("printStartTime") && !p["printStartTime"].is_null())
                start = p["printStartTime"].dump();
            g_saved.cleared[mac] = jstr(p, "printFileName") + "|" + start;
            save_saved();
            g_rank_at = Clock::time_point{};
            BOOST_LOG_TRIVIAL(info) << "Roost Print: bed of " << mac << " marked clear";
            return;
        }
    }
}

} // namespace RoostPicker
} // namespace GUI
} // namespace Slic3r
