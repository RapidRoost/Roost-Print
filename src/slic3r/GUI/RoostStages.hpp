// Roost Print: where every plate is up to. Built in for the farm.
//
// Each plate is at one of four stages, shown by tinting its bed in the 3D view:
//
//   To print   blue border       nothing sent yet
//   Printing   yolk              sent to a printer and on it now
//   Printed    green             the printer finished it
//   Failed     red               the printer stopped or hit an error
//
// It keeps up by itself. Sending a plate (the send window) makes it Printing and remembers the
// print's file name and the printer's address. PrintCam already watches every printer and reports
// the file each one is printing (RoostPrintCam.cpp reads it every few seconds); when that file
// finishes the plate turns Printed, and if it's stopped or errors out it turns Failed. A stage can
// also be set by hand (the job card), for prints made some other way; that sticks until the plate
// is sent again.
//
// Stages are saved in Roost Print's own folder (roost_plate_stages.json), by project name, plate
// number and what's on the plate, so they're still there when the file is opened again, and plates
// of files that aren't open keep updating too.
#ifndef slic3r_RoostStages_hpp_
#define slic3r_RoostStages_hpp_

#include <string>
#include <vector>

#include "libslic3r/Color.hpp"

class wxWindow;

namespace Slic3r {
namespace GUI {
namespace RoostStages {

enum class Stage { ToPrint = 0, Printing, Printed, Failed };

// A plate of the open project (0-based plate index). Plates with nothing on them are To print.
Stage stage(int plate_index);

// A short line about it, e.g. "On Clucky, 42%" or "Printed 2 hours ago". Empty if nothing to say.
std::string detail(int plate_index);

// Set by hand. To print forgets the plate; the others stick until the plate is sent again.
void set_stage(int plate_index, Stage stage);

// The send window put a plate's print on a printer and started it.
void on_sent(int plate_index, const std::string& file_name, const std::string& printer_ip);

// What each printer is doing, from PrintCam's /api/state.
struct PrinterJob
{
    std::string printer;  // PrintCam's name for it
    std::string ip;
    std::string file;     // the file it's printing (or last printed)
    std::string state;    // printing, paused, preparing, finished, stopped, error, idle, offline
    int         progress{0};
};
// Updates the stages of every plate that was sent. True if any changed.
bool on_printers(const std::vector<PrinterJob>& printers);

// Before sending a plate: if it's already printed (or printing), ask first. False = don't send.
bool confirm_send(wxWindow* parent, int plate_index);

// The stage's name ("To print", ...) and colour.
std::string name(Stage stage);
ColorRGBA   color(Stage stage);

} // namespace RoostStages
} // namespace GUI
} // namespace Slic3r

#endif
