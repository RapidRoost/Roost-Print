// Roost Print: stand every new part the fastest way up, by itself.
//
// When a file is opened in the simple view (RoostSlim.hpp), each new part gets two rounds:
//   1. thousands of ways up checked with quick geometry (libslic3r/RoostOrientCore.hpp), keeping a
//      few that won't tip over and fit the printer;
//   2. those few, and the way it stands now, sliced for real with the shop's settings; the fastest
//      wins (the way it is now stays unless another is clearly faster).
// Then the parts are turned, and the plates rearranged if more than one part shares a plate.
// It's skipped when any plate is already sent (so a reopened project is never reshuffled). The
// card shows how much time it saved, and Undo puts things back.
//
// If Roost Print ever closes while slicing in round 2, round 2 is switched off from then on
// (round 1 alone still turns parts), so a bad file can't stop it opening again.
#ifndef slic3r_RoostOrient_GUI_hpp_
#define slic3r_RoostOrient_GUI_hpp_

#include <string>

namespace Slic3r {
namespace GUI {
namespace RoostOrient {

// Looking for the fastest way up right now.
bool busy();
// How far along, 0..1.
float progress();
// One line for the card: what it's doing, or what it found. Empty when there's nothing to say.
std::string status_line();
// The last change can be put back.
bool can_undo();
void undo();

// Called every frame from the card: starts on parts that just arrived, and finishes off
// (rearranging plates) once the turn is done.
void on_frame();

// Treat everything already on the plates as looked at (when the simple view is switched on, so
// parts loaded in the full slicer aren't suddenly turned).
void mark_all_seen();

} // namespace RoostOrient
} // namespace GUI
} // namespace Slic3r

#endif
