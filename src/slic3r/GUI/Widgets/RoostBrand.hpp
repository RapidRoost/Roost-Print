#ifndef slic3r_GUI_RoostBrand_hpp_
#define slic3r_GUI_RoostBrand_hpp_

// Roost Print: rapidroost.net's look, in one place.
//
// The website (app/styles/rr.css, farm.css, dark.css) is the source of truth. Buttons there are
// "stickers": pills with a 2px ink outline and a hard ink shadow offset down-right. Selected
// things are yolk with an ink outline. Hover is a pale blue wash. Focus is a yolk ring.
// Primary actions are blue-ink with white text. Cards have a 2px outline and 14-16px corners.
//
// Every wx widget, the ImGui panels in the 3D view and the app's web pages use these values.

#include <algorithm>
#include <wx/colour.h>

namespace RoostBrand {

// ---- Light theme (rr.css :root) ----
inline const wxColour kPaper    {0xF6, 0xF9, 0xFB}; // page
inline const wxColour kWhite    {0xFF, 0xFF, 0xFF}; // cards, buttons
inline const wxColour kInk      {0x00, 0x00, 0x00}; // outlines, sticker shadow
inline const wxColour kBlue     {0x1E, 0x90, 0xD8}; // brand blue
inline const wxColour kBlueInk  {0x12, 0x68, 0xA8}; // primary buttons, links
inline const wxColour kBlueDeep {0x0B, 0x4F, 0x82};
inline const wxColour kBlueWash {0xEE, 0xF6, 0xFC}; // hover
inline const wxColour kYolk     {0xFF, 0xC9, 0x3C}; // selected, focus ring
inline const wxColour kPencil   {0xC8, 0x32, 0x2A}; // errors
inline const wxColour kLine     {0xD5, 0xDB, 0xE1};
inline const wxColour kText     {0x1F, 0x2A, 0x36};
inline const wxColour kMuted    {0x2B, 0x38, 0x46};
inline const wxColour kChip     {0xEE, 0xF1, 0xF4}; // segmented control track
inline const wxColour kChipLine {0xB9, 0xC2, 0xCB};

// ---- Night theme (dark.css) ----
inline const wxColour kNightPaper   {0x0E, 0x1A, 0x27};
inline const wxColour kNightSurface {0x17, 0x28, 0x39};
inline const wxColour kNightOutline {0xDF, 0xE8, 0xF1}; // ink, at night
inline const wxColour kNightBlueInk {0x6C, 0xB8, 0xEF}; // links at night
inline const wxColour kNightFillBlue{0x12, 0x68, 0xA8}; // primary buttons at night
inline const wxColour kNightWash    {0x13, 0x31, 0x4A}; // hover at night
inline const wxColour kNightChip    {0x22, 0x36, 0x4B};
inline const wxColour kNightField   {0x4A, 0x60, 0x76};
inline const wxColour kNightRule    {0x2B, 0x3E, 0x52};
inline const wxColour kNightDim     {0x9F, 0xB0, 0xC1};
inline const wxColour kNightPanel   {0x07, 0x0C, 0x12};
inline const wxColour kNightWell    {0x0D, 0x18, 0x23};
inline const wxColour kNightPencil  {0xFF, 0x7F, 0x70};

// ---- Shapes (DIP) ----
constexpr int kOutline      = 2;  // sticker / card outline
constexpr int kShadow       = 3;  // sticker shadow offset (.rr-btn--sm)
constexpr int kShadowHover  = 4;
constexpr int kShadowPress  = 1;
constexpr int kRadiusCard   = 14; // --radius-card
constexpr int kRadiusBox    = 20; // --radius-box
constexpr int kRadiusField  = 10; // inputs
constexpr int kFocusRing    = 3;

// Theme-aware picks.
struct Palette {
    wxColour page, surface, ink, text, dim, line, primary, primary_text, link, wash, yolk, yolk_text,
             chip, field, error, disabled_fill, disabled_text;
};

inline Palette palette(bool dark)
{
    if (dark)
        return {kNightPaper, kNightSurface, kNightOutline, kNightOutline, kNightDim, kNightRule,
                kNightFillBlue, kWhite, kNightBlueInk, kNightWash, kYolk, kInk,
                kNightChip, kNightField, kNightPencil, kNightChip, kNightDim};
    return {kPaper, kWhite, kInk, kText, kMuted, kLine,
            kBlueInk, kWhite, kBlueInk, kBlueWash, kYolk, kInk,
            kChip, kChipLine, kPencil, kChip, wxColour(0x8A, 0x96, 0xA3)};
}

// Is this colour one of the app's blues (a "primary" fill), rather than a neutral?
inline bool is_brand_blue(const wxColour &c)
{
    if (!c.IsOk() || c.Alpha() == 0)
        return false;
    const int r = c.Red(), g = c.Green(), b = c.Blue();
    const int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    if (mx - mn < 60 || b != mx || b < 120)
        return false;
    return b - r > 70; // clearly blue, not a slate grey
}

// Is this colour a red/error fill?
inline bool is_alarm(const wxColour &c)
{
    if (!c.IsOk())
        return false;
    return c.Red() > 170 && c.Red() - std::max(c.Green(), c.Blue()) > 80;
}

} // namespace RoostBrand

#endif
