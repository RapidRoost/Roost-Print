// Roost Print: the simple view. Open a file, look at it, send it.
//
// With the simple view on (the default), the 3D view shows the model on the plate and one job
// card, and nothing else from the slicer:
//   - the settings panel, the printer/object panel, the toolbars, the editing tools and the
//     "Slice plate" button are hidden;
//   - the card shows the file and plate, where the plate is up to (RoostStages.hpp), the printer
//     and the material(s), the print time and filament once it's sliced, Send, and buttons to mark
//     the stage by hand;
//   - each plate slices by itself with the shop's saved settings, a moment after anything changes,
//     and stays on the model (the Layers button shows the sliced layers);
//   - models are lit by the shop, as on rapidroost.net (the shop light, gouraud.fs).
//
// View > Simple Roost view (or "Full slicer" on the card) goes back to the whole slicer, for odd
// jobs; it's remembered. View > Shop lighting turns the shop light off and on.
#ifndef slic3r_RoostSlim_hpp_
#define slic3r_RoostSlim_hpp_

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

namespace RoostSlim {

// The simple view is on (saved in Roost Print's settings; on unless turned off).
bool enabled();
void set_enabled(bool on);

// Models in Prepare are lit by the shop (on unless turned off).
bool shop_light();
void set_shop_light(bool on);

// Bring the window in line with the setting (hide or show the settings panel). Called once the
// window is up, and whenever the setting changes.
void apply();

// The simple view is on and this is the editor's Prepare or Preview view.
bool active(GLCanvas3D& canvas);

// The job card, drawn over the 3D view (Dear ImGui). Also starts slicing when a plate needs it.
void render_card(GLCanvas3D& canvas);

} // namespace RoostSlim
} // namespace GUI
} // namespace Slic3r

#endif
