// Roost Print: the simple view. See RoostSlim.hpp.
#include "RoostSlim.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include "imgui/imgui.h"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/format.hpp"

#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "ImGuiWrapper.hpp"
#include "Jobs/Worker.hpp"
#include "MainFrame.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "RoostImGuiBrand.hpp"
#include "RoostOrient.hpp"
#include "RoostStages.hpp"
#include "SiderBar.h"

namespace Slic3r {
namespace GUI {
namespace RoostSlim {

namespace {

using Clock = std::chrono::steady_clock;

// Auto-slicing: what the plate looked like when we last started a slice for it, and when it last changed.
struct AutoSlice
{
    std::string       seen;
    std::string       tried;
    Clock::time_point changed{};
};
AutoSlice g_auto;
bool      g_applied_once = false;

bool config_flag(const char* key, bool default_on)
{
    AppConfig* cfg = wxGetApp().app_config;
    if (cfg == nullptr)
        return default_on;
    const std::string v = cfg->get(key);
    return v.empty() ? default_on : v != "0";
}

void set_config_flag(const char* key, bool on)
{
    if (AppConfig* cfg = wxGetApp().app_config)
        cfg->set(key, on ? "1" : "0");
}

void redraw_all()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    for (GLCanvas3D* c : {plater->get_view3D_canvas3D(), plater->get_preview_canvas3D()})
        if (c != nullptr) {
            c->set_as_dirty();
            c->request_extra_frame();
        }
}

// "2h 14m", "48m", "35s".
std::string short_time(double seconds)
{
    const long s = long(std::lround(std::max(0.0, seconds)));
    const long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if (d > 0)
        return std::to_string(d) + "d " + std::to_string(h) + "h";
    if (h > 0)
        return std::to_string(h) + "h " + std::to_string(m) + "m";
    if (m > 0)
        return std::to_string(m) + "m";
    return std::to_string(s) + "s";
}

// What the current plate is, for auto-slicing: when this changes, it's sliced again.
std::string plate_signature(Plater* plater, PartPlate* plate, int plate_index)
{
    PresetBundle* pb = wxGetApp().preset_bundle;
    std::string   s  = std::to_string(plate_index) + "|" + std::to_string(plater->get_active_snapshot_time()) + "|" +
                    std::string(plater->get_project_name().ToUTF8().data());
    if (pb != nullptr) {
        s += "|" + pb->printers.get_selected_preset_name() + "|" + pb->prints.get_selected_preset_name();
        for (const std::string& f : pb->filament_presets)
            s += "|" + f;
    }
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

// The shop's filaments: the ones shown in the filament list (Polymaker; Creality's are hidden).
struct FilamentChoice
{
    std::string name;  // preset name
    std::string label; // what the card shows
};
std::vector<FilamentChoice> filament_choices()
{
    std::vector<FilamentChoice> out;
    PresetBundle*               pb = wxGetApp().preset_bundle;
    if (pb == nullptr)
        return out;
    for (const Preset& p : pb->filaments.get_presets()) {
        if (p.is_default || !p.is_visible || !p.is_compatible)
            continue;
        std::string label = !p.alias.empty() ? p.alias : p.name;
        out.push_back({p.name, label});
    }
    std::sort(out.begin(), out.end(), [](const FilamentChoice& a, const FilamentChoice& b) { return a.label < b.label; });
    out.erase(std::unique(out.begin(), out.end(), [](const FilamentChoice& a, const FilamentChoice& b) { return a.name == b.name; }),
              out.end());
    return out;
}

std::string filament_label(const std::string& preset_name)
{
    PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb != nullptr)
        if (const Preset* p = pb->filaments.find_preset(preset_name, false))
            return !p->alias.empty() ? p->alias : p->name;
    return preset_name;
}

// Switch filament `idx` to a preset, the same steps as the filament panel's own picker.
void select_filament(int idx, const std::string& preset_name)
{
    PresetBundle* pb     = wxGetApp().preset_bundle;
    Plater*       plater = wxGetApp().plater();
    if (pb == nullptr || plater == nullptr || idx < 0 || idx >= int(pb->filament_presets.size()))
        return;
    if (pb->filament_presets[idx] == preset_name)
        return;
    pb->set_filament_preset(idx, preset_name);
    plater->update_project_dirty_from_presets();
    pb->export_selections(*wxGetApp().app_config);
    plater->sidebar().update_dynamic_filament_list();
    plater->on_config_change(pb->full_config());
    if (wxGetApp().app_config->get("auto_calculate_when_filament_change") == "true")
        plater->sidebar().auto_calc_flushing_volumes(idx);
    Slic3r::put_other_changes();
    for (PartPlate* plate : plater->get_partplate_list().get_plate_list())
        plate->update_slice_result_valid_state(false);
    plater->sidebar().update_filament(idx);
    BOOST_LOG_TRIVIAL(info) << "Roost Print: filament " << idx + 1 << " is now " << preset_name;
}

ImVec4 hex_colour(const std::string& hex)
{
    unsigned int r = 200, g = 200, b = 200;
    if (hex.size() >= 7 && hex[0] == '#')
        if (std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3)
            r = g = b = 200;
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

ImVec4 to_imvec(const ColorRGBA& c, float alpha = 1.0f) { return ImVec4(c.r(), c.g(), c.b(), alpha); }

// A small label in the card's dim colour.
void dim_text(const std::string& s) { ImGui::TextColored(RoostImGui::dim(), "%s", s.c_str()); }

// A combo whose preview is `preview` and whose rows are `rows`; returns the clicked row or -1.
int roost_combo(const char* id, const std::string& preview, const std::vector<std::string>& rows, int selected, float width,
                const ImVec4* swatch = nullptr)
{
    int clicked = -1;
    ImGui::PushItemWidth(width);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, RoostImGui::field());
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, RoostImGui::wash());
    ImGui::PushStyleColor(ImGuiCol_PopupBg, RoostImGui::surface());
    ImGui::PushStyleColor(ImGuiCol_Header, RoostImGui::yolk_row());
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, RoostImGui::wash());
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * wxGetApp().plater()->get_current_canvas3D()->get_scale());
    std::string shown = preview;
    if (swatch != nullptr)
        shown = "      " + shown; // room for the colour dot
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (ImGui::BeginCombo(id, shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < int(rows.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(rows[i].c_str(), i == selected))
                clicked = i;
            if (i == selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (swatch != nullptr) {
        const float h = ImGui::GetFrameHeight();
        const float r = h * 0.28f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(at.x + h * 0.55f, at.y + h * 0.5f);
        dl->AddCircleFilled(c, r, ImGui::GetColorU32(*swatch));
        dl->AddCircle(c, r, ImGui::GetColorU32(RoostImGui::ink()), 0, 1.5f);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    ImGui::PopItemWidth();
    return clicked;
}

// One of the four stage buttons. Selected = filled with the stage's colour.
bool stage_chip(const char* label, RoostStages::Stage stage, bool selected, const ImVec2& size, float scale)
{
    const ImVec2 pos     = ImGui::GetCursorScreenPos();
    const bool   clicked = ImGui::InvisibleButton(label, size);
    const bool   hovered = ImGui::IsItemHovered();
    ImDrawList*  dl      = ImGui::GetWindowDrawList();
    const ImVec2 mx(pos.x + size.x, pos.y + size.y);
    const ImVec4 col     = to_imvec(RoostStages::color(stage));
    const float  round   = size.y * 0.5f;
    if (selected)
        dl->AddRectFilled(pos, mx, ImGui::GetColorU32(col), round);
    else if (hovered)
        dl->AddRectFilled(pos, mx, ImGui::GetColorU32(RoostImGui::wash()), round);
    dl->AddRect(pos, mx, ImGui::GetColorU32(selected ? RoostImGui::to_vec4(RoostBrand::kInk) : col), round, 0, 1.5f * scale);
    const char*  end = RoostImGui::label_end(label);
    const ImVec2 ts  = ImGui::CalcTextSize(label, end);
    const ImVec4 tc  = selected ? (stage == RoostStages::Stage::Printing ? RoostImGui::yolk_text() : ImVec4(1, 1, 1, 1)) : RoostImGui::text();
    dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f), ImGui::GetColorU32(tc), label, end);
    return clicked;
}

void maybe_auto_slice(GLCanvas3D& canvas, Plater* plater, PartPlate* plate, int plate_index, bool has_objects, bool valid, bool slicing)
{
    if (!has_objects || valid || slicing || RoostOrient::busy() || !plate->can_slice())
        return;
    if (canvas.get_gizmos_manager().get_current_type() != GLGizmosManager::Undefined || canvas.is_mouse_dragging())
        return;
    if (!plater->get_ui_job_worker().is_idle()) { // arranging or turning parts: slice after
        canvas.schedule_extra_frame(300);
        return;
    }
    const std::string sig = plate_signature(plater, plate, plate_index);
    const auto        now = Clock::now();
    if (sig != g_auto.seen) {
        g_auto.seen    = sig;
        g_auto.changed = now;
    }
    if (sig == g_auto.tried)
        return; // already sliced (or tried) like this; wait for the next change
    if (now - g_auto.changed < std::chrono::milliseconds(900)) {
        canvas.schedule_extra_frame(300); // still changing; look again in a moment
        return;
    }
    g_auto.tried = sig;
    wxGetApp().CallAfter([]() {
        if (Plater* p = wxGetApp().plater())
            p->roost_slice_in_place();
    });
}

} // namespace

bool enabled() { return config_flag("roost_simple_view", true); }

void set_enabled(bool on)
{
    if (on && !enabled())
        RoostOrient::mark_all_seen(); // don't suddenly turn parts that were loaded in the full slicer
    set_config_flag("roost_simple_view", on);
    apply();
}

bool shop_light() { return config_flag("roost_shop_light", true); }

void set_shop_light(bool on)
{
    set_config_flag("roost_shop_light", on);
    redraw_all();
}

void apply()
{
    g_applied_once = true;
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    plater->collapse_sidebar(enabled());
    // Leave anything the full slicer had open (a cut or paint tool) before hiding its tools.
    if (enabled())
        plater->exit_gizmo();
    g_auto = AutoSlice{};
    redraw_all();
}

bool active(GLCanvas3D& canvas)
{
    if (!wxGetApp().is_editor() || !enabled())
        return false;
    const auto type = canvas.get_canvas_type();
    return type == GLCanvas3D::CanvasView3D || type == GLCanvas3D::CanvasPreview;
}

void render_card(GLCanvas3D& canvas)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr || wxGetApp().preset_bundle == nullptr)
        return;
    if (!g_applied_once) {
        g_applied_once = true; // once, not every frame until it runs
        wxGetApp().CallAfter([]() { apply(); });
    }

    PartPlateList& plates      = plater->get_partplate_list();
    const int      plate_index = plates.get_curr_plate_index();
    PartPlate*     plate       = plates.get_curr_plate();
    if (plate == nullptr)
        return;
    const int  plate_count = plates.get_plate_count();
    const bool preview     = canvas.get_canvas_type() == GLCanvas3D::CanvasPreview;
    const bool has_objects = !plate->get_objects_on_this_plate().empty();
    const bool slicing     = plater->is_background_process_slicing();
    const bool valid       = plate->is_slice_result_valid();

    if (!preview) {
        RoostOrient::on_frame();
        maybe_auto_slice(canvas, plater, plate, plate_index, has_objects, valid, slicing);
    }

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    const float   scale = canvas.get_scale();
    const float   width = 300.0f * scale;
    const float   pad   = 14.0f * scale;
    const float   inner = width - 2.0f * pad;

    // Prepare: top left (where the printer panel was). Preview: bottom left, clear of the legend.
    if (preview)
        imgui.set_next_window_pos(12.0f * scale, float(canvas.get_canvas_size().get_height()) - 12.0f * scale, ImGuiCond_Always, 0.0f, 1.0f);
    else
        imgui.set_next_window_pos(12.0f * scale, 12.0f * scale, ImGuiCond_Always, 0.0f, 0.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, -1.0f), ImVec2(width, -1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RoostImGui::kRadiusCard * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, RoostImGui::kOutline * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * scale, 8.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, RoostImGui::surface());
    ImGui::PushStyleColor(ImGuiCol_Border, RoostImGui::ink());
    ImGui::PushStyleColor(ImGuiCol_Text, RoostImGui::text());
    ImGui::PushStyleColor(ImGuiCol_Separator, RoostImGui::line());

    const int flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoFocusOnAppearing;
    if (imgui.begin(std::string("##roost_card"), flags)) {
        // Hard sticker shadow under the card, like the website's cards.
        {
            const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
            const float  off = RoostImGui::kShadow * scale;
            ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(wp.x + off, wp.y + off), ImVec2(wp.x + ws.x + off, wp.y + ws.y + off),
                                                          ImGui::GetColorU32(RoostImGui::ink()), RoostImGui::kRadiusCard * scale);
        }

        // ---- The file and plate ----
        std::string project = std::string(plater->get_project_name().ToUTF8().data());
        if (project.empty())
            project = _u8L("Roost Print");
        imgui.push_bold_font();
        const float max_title = inner - (plate_count > 1 ? ImGui::CalcTextSize("Plate 88 of 88").x + 8.0f * scale : 0.0f);
        std::string title     = project;
        while (title.size() > 4 && ImGui::CalcTextSize(title.c_str()).x > max_title)
            title = title.substr(0, title.size() - 4) + "...";
        ImGui::TextUnformatted(title.c_str());
        imgui.pop_bold_font();
        if (plate_count > 1) {
            const std::string pl = format(_u8L("Plate %1% of %2%"), plate_index + 1, plate_count);
            ImGui::SameLine(width - pad - ImGui::CalcTextSize(pl.c_str()).x);
            dim_text(pl);
        }

        if (!has_objects) {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
            ImGui::TextUnformatted(_u8L("Drop a file anywhere on the plate, or open one.").c_str());
            ImGui::PopTextWrapPos();
            if (RoostImGui::sticker_button((_u8L("Open a file") + "##roost_open").c_str(), ImVec2(inner, 34.0f * scale),
                                           RoostImGui::StickerKind::Primary, true, scale))
                wxGetApp().CallAfter([]() {
                    if (Plater* p = wxGetApp().plater())
                        p->add_file();
                });
        } else {
            // ---- Where it's up to ----
            const RoostStages::Stage stage  = RoostStages::stage(plate_index);
            const std::string        detail = RoostStages::detail(plate_index);
            {
                const std::string label = RoostStages::name(stage);
                const ImVec2      ts    = ImGui::CalcTextSize(label.c_str());
                const ImVec2      pos   = ImGui::GetCursorScreenPos();
                const ImVec2      size(ts.x + 20.0f * scale, ts.y + 8.0f * scale);
                ImDrawList*       dl  = ImGui::GetWindowDrawList();
                const ImVec4      col = to_imvec(RoostStages::color(stage));
                dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(col), size.y * 0.5f);
                const ImVec4 tc = stage == RoostStages::Stage::Printing ? RoostImGui::yolk_text() : ImVec4(1, 1, 1, 1);
                dl->AddText(ImVec2(pos.x + 10.0f * scale, pos.y + 4.0f * scale), ImGui::GetColorU32(tc), label.c_str());
                ImGui::Dummy(size);
                if (!detail.empty()) {
                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * scale);
                    dim_text(detail);
                }
            }

            if (!preview) {
                ImGui::Separator();
                // ---- Printer ----
                SidebarPrinter&            bar      = plater->sidebar_printer();
                const PrinterSelectorModel model    = bar.printer_selector_model();
                const PrinterMachineItem*  selected = model.selected_machine_item();
                std::vector<std::string>       rows;
                std::vector<PrinterMachineKey> keys;
                int                            sel = -1;
                for (const PrinterSelectorSection& section : model.sections)
                    for (const PrinterMachineItem& item : section.machines) {
                        if (model.has_selected_machine && item.key == model.selected_machine)
                            sel = int(rows.size());
                        rows.push_back(item.display_name);
                        keys.push_back(item.key);
                    }
                const float label_w = 74.0f * scale;
                dim_text(_u8L("Printer"));
                ImGui::SameLine(pad + label_w);
                const std::string preview_name = selected != nullptr ? selected->display_name : _u8L("Choose a printer");
                const int pick = roost_combo("##roost_printer", preview_name, rows, sel, inner - label_w);
                if (pick >= 0 && pick != sel) {
                    const PrinterMachineKey key = keys[pick];
                    wxGetApp().CallAfter([key]() {
                        if (Plater* p = wxGetApp().plater())
                            p->sidebar_printer().select_machine(key);
                    });
                }

                // ---- Material(s) ----
                PresetBundle*                      pb      = wxGetApp().preset_bundle;
                const std::vector<FilamentChoice>  choices = filament_choices();
                std::vector<std::string>           labels;
                for (const FilamentChoice& c : choices)
                    labels.push_back(c.label);
                const ConfigOptionStrings* colours = pb->project_config.option<ConfigOptionStrings>("filament_colour");
                const int                  count   = std::min<int>(int(pb->filament_presets.size()), 8);
                for (int i = 0; i < count; ++i) {
                    ImGui::PushID(i);
                    dim_text(count > 1 ? format(_u8L("Material %1%"), i + 1) : _u8L("Material"));
                    ImGui::SameLine(pad + label_w);
                    const std::string& current = pb->filament_presets[i];
                    int                csel    = -1;
                    for (int k = 0; k < int(choices.size()); ++k)
                        if (choices[k].name == current)
                            csel = k;
                    const ImVec4 swatch = hex_colour(colours != nullptr && i < int(colours->values.size()) ? colours->values[i] : std::string());
                    const int    cpick  = roost_combo("##roost_filament", filament_label(current), labels, csel, inner - label_w, &swatch);
                    if (cpick >= 0 && cpick != csel) {
                        const std::string name = choices[cpick].name;
                        wxGetApp().CallAfter([i, name]() { select_filament(i, name); });
                    }
                    ImGui::PopID();
                }

                // ---- Orientation (the speed optimizer) ----
                const std::string orient = RoostOrient::status_line();
                if (!orient.empty() && !RoostOrient::busy()) {
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
                    dim_text(orient);
                    ImGui::PopTextWrapPos();
                    if (RoostOrient::can_undo() &&
                        RoostImGui::sticker_button((_u8L("Undo the turn") + "##roost_undo_orient").c_str(), ImVec2(0.0f, 24.0f * scale),
                                                   RoostImGui::StickerKind::Secondary, true, scale))
                        wxGetApp().CallAfter([]() { RoostOrient::undo(); });
                }
            }

            ImGui::Separator();
            // ---- Slicing, or the result ----
            if (RoostOrient::busy()) {
                ImGui::TextUnformatted(_u8L("Finding the fastest way up...").c_str());
                ImGui::ProgressBar(RoostOrient::progress(), ImVec2(inner, 6.0f * scale), "");
            } else if (slicing) {
                const float pct = plate->get_slicing_percent();
                ImGui::TextUnformatted(_u8L("Slicing...").c_str());
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, RoostImGui::brand_blue());
                ImGui::ProgressBar(pct > 0.0f ? std::min(pct, 100.0f) / 100.0f : 0.02f, ImVec2(inner, 6.0f * scale), "");
                ImGui::PopStyleColor();
            } else if (valid && plate->get_slice_result() != nullptr) {
                const auto& mode = plate->get_slice_result()->print_statistics.modes[0];
                double      grams = 0.0;
                Print*      print = nullptr;
                plate->get_print((PrintBase**) &print, nullptr, nullptr);
                if (print != nullptr)
                    grams = print->print_statistics().total_weight;
                dim_text(_u8L("Print time"));
                ImGui::SameLine(pad + 96.0f * scale);
                imgui.push_bold_font();
                ImGui::TextUnformatted(short_time(mode.time).c_str());
                imgui.pop_bold_font();
                dim_text(_u8L("Filament"));
                ImGui::SameLine(pad + 96.0f * scale);
                imgui.push_bold_font();
                ImGui::TextUnformatted(grams > 0.0 ? (std::to_string(int(std::lround(grams))) + " g").c_str() : "-");
                imgui.pop_bold_font();
            } else if (!plate->get_validate_error_message().empty()) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
                ImGui::TextColored(RoostImGui::error(), "%s", plate->get_validate_error_message().c_str());
                ImGui::PopTextWrapPos();
            } else if (!plate->can_slice()) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
                ImGui::TextColored(RoostImGui::error(), "%s", _u8L("Part of the model is off the plate. Move it on to slice.").c_str());
                ImGui::PopTextWrapPos();
            } else {
                dim_text(_u8L("Getting ready to slice..."));
            }

            // ---- Send ----
            const bool can_send = valid && !slicing && !RoostOrient::busy();
            if (RoostImGui::sticker_button((_u8L("Send to printer") + "##roost_send").c_str(), ImVec2(inner, 36.0f * scale),
                                           RoostImGui::StickerKind::Primary, can_send, scale))
                wxGetApp().CallAfter([]() {
                    if (Plater* p = wxGetApp().plater())
                        wxPostEvent(p, SimpleEvent(EVT_GLTOOLBAR_SEND_TO_LOCAL_NET_PRINTER));
                });

            // ---- Mark it by hand ----
            ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
            dim_text(_u8L("Mark as"));
            const float  gap  = 6.0f * scale;
            const ImVec2 chip((inner - 3.0f * gap) / 4.0f, 24.0f * scale);
            const RoostStages::Stage all[] = {RoostStages::Stage::ToPrint, RoostStages::Stage::Printing, RoostStages::Stage::Printed,
                                              RoostStages::Stage::Failed};
            for (int i = 0; i < 4; ++i) {
                if (i > 0)
                    ImGui::SameLine(0.0f, gap);
                const std::string label = RoostStages::name(all[i]) + "##roost_stage" + std::to_string(i);
                if (stage_chip(label.c_str(), all[i], stage == all[i], chip, scale) && stage != all[i]) {
                    const RoostStages::Stage s = all[i];
                    wxGetApp().CallAfter([plate_index, s]() { RoostStages::set_stage(plate_index, s); });
                }
            }
        }

        // ---- Footer: layers, and the way back to the full slicer ----
        ImGui::Separator();
        const float half = (inner - 8.0f * scale) / 2.0f;
        const std::string layers = (preview ? _u8L("Back to the model") : _u8L("Layers")) + "##roost_layers";
        if (RoostImGui::sticker_button(layers.c_str(), ImVec2(half, 28.0f * scale), RoostImGui::StickerKind::Secondary,
                                       preview || (valid && has_objects), scale)) {
            const bool to_preview = !preview;
            wxGetApp().CallAfter([to_preview]() {
                if (MainFrame* f = wxGetApp().mainframe)
                    f->select_tab(size_t(to_preview ? MainFrame::tpPreview : MainFrame::tp3DEditor));
            });
        }
        ImGui::SameLine(0.0f, 8.0f * scale);
        if (RoostImGui::sticker_button((_u8L("Full slicer") + "##roost_full").c_str(), ImVec2(half, 28.0f * scale),
                                       RoostImGui::StickerKind::Secondary, true, scale))
            wxGetApp().CallAfter([]() { set_enabled(false); });
    }
    imgui.end();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);
}

} // namespace RoostSlim
} // namespace GUI
} // namespace Slic3r
