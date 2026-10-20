#ifndef slic3r_GUI_RoostImGuiBrand_hpp_
#define slic3r_GUI_RoostImGuiBrand_hpp_

// Roost Print: rapidroost.net's look for the Dear ImGui panels in the 3D view.
//
// Colour values come from Widgets/RoostBrand.hpp (the single source of truth). This header only
// converts them to ImGui types and adds the "sticker" drawing helpers:
//   - stickers are pills with a 2px ink outline and a hard ink shadow offset down-right;
//   - hover lifts the face (shadow grows), press sinks it (shadow shrinks);
//   - selected things are yolk with ink text and an ink outline; hover is the blue wash.
//
// Deliberately self-contained: it does not include imgui_internal.h and does not rely on the
// IMGUI_DEFINE_MATH_OPERATORS ImVec2 operators, so it is safe to include from any translation unit.

#include <algorithm>
#include <cstring>

#include "imgui/imgui.h"
#include "Widgets/RoostBrand.hpp"

namespace RoostImGui {

// ---- Theme flag (set from ImGuiWrapper::on_change_color_mode; the app defaults to night) ----
inline bool &dark_flag()
{
    static bool s_dark = true;
    return s_dark;
}
inline bool is_dark() { return dark_flag(); }
inline void set_dark(bool dark) { dark_flag() = dark; }

// ---- Conversions ----
inline ImVec4 to_vec4(const wxColour &c, float alpha = 1.0f)
{
    return ImVec4(c.Red() / 255.0f, c.Green() / 255.0f, c.Blue() / 255.0f, alpha);
}
// Goes through ImGui::GetColorU32 so the current style alpha (disabled_begin etc.) applies.
inline ImU32 to_u32(const wxColour &c, float alpha = 1.0f) { return ImGui::GetColorU32(to_vec4(c, alpha)); }

inline RoostBrand::Palette pal() { return RoostBrand::palette(is_dark()); }

// ---- Theme-aware ImGui colours ----
inline ImVec4 page()          { return to_vec4(pal().page); }
inline ImVec4 surface()       { return to_vec4(pal().surface); }
inline ImVec4 ink()           { return to_vec4(pal().ink); }        // outline / sticker shadow (#000 light, #dfe8f1 night)
inline ImVec4 text()          { return to_vec4(pal().text); }
inline ImVec4 dim()           { return to_vec4(pal().dim); }
inline ImVec4 line()          { return to_vec4(pal().line); }       // quiet rules / separators
inline ImVec4 primary()       { return to_vec4(pal().primary); }    // blue-ink fill (both themes)
inline ImVec4 primary_text()  { return to_vec4(pal().primary_text); }
inline ImVec4 link()          { return to_vec4(pal().link); }       // blue-ink by day, #6cb8ef at night
inline ImVec4 wash()          { return to_vec4(pal().wash); }       // hover
inline ImVec4 yolk()          { return to_vec4(RoostBrand::kYolk); } // selected / focus
inline ImVec4 yolk_text()     { return to_vec4(RoostBrand::kInk); }  // text on yolk is always ink
inline ImVec4 chip()          { return to_vec4(pal().chip); }
inline ImVec4 field()         { return to_vec4(pal().field); }
inline ImVec4 error()         { return to_vec4(pal().error); }
inline ImVec4 disabled_fill() { return to_vec4(pal().disabled_fill); }
inline ImVec4 disabled_text() { return to_vec4(pal().disabled_text); }
inline ImVec4 brand_blue()    { return to_vec4(RoostBrand::kBlue); }
// Pressed (but not selected) state for neutral buttons: a deeper wash.
inline ImVec4 wash_pressed()
{
    return is_dark() ? to_vec4(RoostBrand::kNightChip) : ImVec4(0xD6 / 255.0f, 0xE9 / 255.0f, 0xF7 / 255.0f, 1.0f);
}
// Selected list rows whose text colour we cannot switch to ink: full yolk by day; at night a
// yolk tint so the light night text keeps its contrast.
inline ImVec4 yolk_row()
{
    ImVec4 y = yolk();
    if (is_dark())
        y.w = 0.38f;
    return y;
}
inline ImU32 u32(const ImVec4 &c) { return ImGui::GetColorU32(c); }

// ---- Shapes (DIP; multiply by the canvas scale) ----
constexpr float kOutline     = float(RoostBrand::kOutline);
constexpr float kShadow      = float(RoostBrand::kShadow);
constexpr float kShadowHover = float(RoostBrand::kShadowHover);
constexpr float kShadowPress = float(RoostBrand::kShadowPress);
constexpr float kRadiusCard  = float(RoostBrand::kRadiusCard);
constexpr float kRadiusField = float(RoostBrand::kRadiusField);
constexpr float kRadiusFrame = 8.0f; // small frames (checkboxes, inputs inside panels)

// Hover lifts the face up-left by 1, press sinks it down-right; the shadow's far edge stays put.
inline float sticker_shadow(bool hovered, bool held, float scale)
{
    return (held ? kShadowPress : (hovered ? kShadowHover : kShadow)) * scale;
}
inline float sticker_face_shift(bool hovered, bool held, float scale)
{
    return (kShadow * scale) - sticker_shadow(hovered, held, scale);
}

// ---- Primitive drawing ----
inline void draw_fill(ImDrawList *dl, const ImVec2 &mn, const ImVec2 &mx, ImU32 col, float rounding,
                      ImDrawFlags corners = ImDrawFlags_RoundCornersAll)
{
    dl->AddRectFilled(mn, mx, col, rounding, corners);
}

// Stroke kept inside [mn, mx] so outlines line up with the fill exactly.
inline void draw_outline(ImDrawList *dl, const ImVec2 &mn, const ImVec2 &mx, ImU32 col, float rounding, float width,
                         ImDrawFlags corners = ImDrawFlags_RoundCornersAll)
{
    const float h = width * 0.5f;
    dl->AddRect(ImVec2(mn.x + h, mn.y + h), ImVec2(mx.x - h, mx.y - h), col, std::max(0.0f, rounding - h), corners, width);
}

// The hard, unblurred sticker shadow: the face rect pushed down-right by `offset`.
inline void draw_shadow(ImDrawList *dl, const ImVec2 &mn, const ImVec2 &mx, ImU32 col, float rounding, float offset,
                        ImDrawFlags corners = ImDrawFlags_RoundCornersAll)
{
    if (offset <= 0.0f)
        return;
    dl->AddRectFilled(ImVec2(mn.x + offset, mn.y + offset), ImVec2(mx.x + offset, mx.y + offset), col, rounding, corners);
}

// Focus ring: yolk, just outside the face.
inline void draw_focus_ring(ImDrawList *dl, const ImVec2 &mn, const ImVec2 &mx, float rounding, float scale)
{
    const float g = float(RoostBrand::kFocusRing) * scale;
    dl->AddRect(ImVec2(mn.x - g * 0.5f, mn.y - g * 0.5f), ImVec2(mx.x + g * 0.5f, mx.y + g * 0.5f), u32(yolk()), rounding + g * 0.5f, 0, g);
}

enum class StickerKind { Primary, Secondary, Selected };

struct StickerColors
{
    ImU32 fill, fill_hover, text, outline, shadow;
};

inline StickerColors sticker_colors(StickerKind kind, bool enabled)
{
    StickerColors c;
    if (!enabled) {
        c.fill = c.fill_hover = u32(disabled_fill());
        c.text    = u32(disabled_text());
        c.outline = u32(line());
        c.shadow  = 0;
        return c;
    }
    c.outline = u32(ink());
    c.shadow  = u32(ink());
    switch (kind) {
    case StickerKind::Primary:
        c.fill       = u32(primary());
        c.fill_hover = u32(primary());
        c.text       = u32(primary_text());
        break;
    case StickerKind::Selected:
        c.fill = c.fill_hover = u32(yolk());
        c.text                = u32(yolk_text());
        c.outline = c.shadow = to_u32(RoostBrand::kInk);
        break;
    case StickerKind::Secondary:
    default:
        c.fill       = is_dark() ? u32(surface()) : to_u32(RoostBrand::kWhite);
        c.fill_hover = u32(wash());
        c.text       = u32(text());
        break;
    }
    return c;
}

// Text up to a "##" id suffix, like ImGui's own buttons.
inline const char *label_end(const char *label)
{
    const char *hh = std::strstr(label, "##");
    return hh ? hh : label + std::strlen(label);
}

// Draw a complete sticker (shadow, fill, outline, centred label) for a face whose resting rect is
// [mn, mx]. Returns nothing; the caller owns the hit test (keep it on the resting rect).
inline void draw_sticker(ImDrawList *dl, const ImVec2 &mn, const ImVec2 &mx, const char *label, StickerKind kind,
                         bool enabled, bool hovered, bool held, float scale)
{
    const StickerColors c = sticker_colors(kind, enabled);
    const float h        = mx.y - mn.y;
    const float rounding = h * 0.5f;
    const float shift    = enabled ? sticker_face_shift(hovered, held, scale) : 0.0f;
    const ImVec2 fmn(mn.x + shift, mn.y + shift), fmx(mx.x + shift, mx.y + shift);
    if (enabled)
        draw_shadow(dl, fmn, fmx, c.shadow, rounding, sticker_shadow(hovered, held, scale));
    draw_fill(dl, fmn, fmx, hovered && enabled ? c.fill_hover : c.fill, rounding);
    draw_outline(dl, fmn, fmx, c.outline, rounding, kOutline * scale);
    if (label != nullptr && *label != '\0') {
        const char  *end = label_end(label);
        const ImVec2 ts  = ImGui::CalcTextSize(label, end);
        const ImVec2 tp(fmn.x + (fmx.x - fmn.x - ts.x) * 0.5f, fmn.y + (fmx.y - fmn.y - ts.y) * 0.5f);
        dl->AddText(tp, c.text, label, end);
    }
}

// Size a sticker for a label: text plus pill-friendly padding.
inline ImVec2 sticker_size(const char *label, const ImVec2 &size_arg, float scale)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    const ImVec2      ts = ImGui::CalcTextSize(label, label_end(label));
    ImVec2            sz = size_arg;
    if (sz.y <= 0.0f)
        sz.y = ts.y + st.FramePadding.y * 2.0f + 2.0f * kOutline * scale;
    if (sz.x <= 0.0f)
        sz.x = ts.x + std::max(st.FramePadding.x, sz.y * 0.5f) * 2.0f;
    return sz;
}

// An ImGui button drawn as a rapidroost.net sticker. Hit area = the resting face rect, same as a
// normal button of that size; the shadow (3-4px) is drawn outside it, into the item spacing.
inline bool sticker_button(const char *label, const ImVec2 &size_arg, StickerKind kind, bool enabled = true, float scale = 1.0f)
{
    const ImVec2 size    = sticker_size(label, size_arg, scale);
    const ImVec2 pos     = ImGui::GetCursorScreenPos();
    const bool   clicked = ImGui::InvisibleButton(label, size);
    const bool   hovered = enabled && ImGui::IsItemHovered();
    const bool   held    = enabled && ImGui::IsItemActive();
    draw_sticker(ImGui::GetWindowDrawList(), pos, ImVec2(pos.x + size.x, pos.y + size.y), label, kind, enabled, hovered, held, scale);
    if (enabled && ImGui::IsItemFocused() && !held && !hovered && ImGui::GetIO().NavVisible)
        draw_focus_ring(ImGui::GetWindowDrawList(), pos, ImVec2(pos.x + size.x, pos.y + size.y), size.y * 0.5f, scale);
    return enabled && clicked;
}

// Selected tool / toggle buttons (gizmo brushes, tool pickers): yolk fill, ink icon/text.
// Pushes 4 colours; pop with ImGui::PopStyleColor(4).
inline void push_selected_button_colors()
{
    ImGui::PushStyleColor(ImGuiCol_Button, yolk());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, yolk());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, yolk());
    ImGui::PushStyleColor(ImGuiCol_Text, yolk_text());
}
constexpr int kSelectedButtonColorCount = 4;

} // namespace RoostImGui

#endif
