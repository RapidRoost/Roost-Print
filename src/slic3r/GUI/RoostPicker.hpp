// Roost Print: the best printer for each plate.
//
// The first step towards "an order comes in and goes to a suitable printer by itself". For the
// plate on screen, every printer in the shop is ranked, in this order:
//   1. ready: online, not printing, no error, and its bed is clear (the last print was taken off);
//   2. the right printer: the same model the plate was sliced for (so the right build volume);
//   3. the right nozzle size;
//   4. the right material loaded: the plate's filament type, and its colour when the printer knows
//      the colour of what's loaded.
//
// Where the facts come from:
//   - The printers, and whether they're online, printing or showing an error, come from the
//     Device page, which talks to every printer on the shop network and hands the whole list to
//     the app every 2 seconds (DM::DataCenter; "update_devices" in DeviceMgrRoutes.cpp).
//   - K2 Plus: its CFS reads each spool's RFID tag and the printer reports what's in every slot
//     (type, name and colour) in that same list ("boxsInfo" > "materialBoxs").
//   - K1C: no RFID, so what's loaded is set by hand here ("Spools by hand" on the job card) and
//     saved on this computer (roost_hand_spools.json). Once set, it counts as known.
//
// The job card shows the best printer and why ("idle, PLA Black loaded"), sends there in one
// click, and lets you pick another from the ranked list. "Auto-send" (off unless turned on) sends
// each plate to its best printer as soon as it's sliced, but never a plate that's printing,
// printed or failed.
//
// Sending reuses the normal send window: the chosen printer is made the current printer (so the
// window opens with it picked), and the window presses Start Print by itself once it's ready. If
// the window ends up with a different printer than the one chosen, nothing is sent.
#ifndef slic3r_RoostPicker_hpp_
#define slic3r_RoostPicker_hpp_

#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace RoostPicker {

// One spool on a printer.
struct Spool
{
    std::string type;    // "PLA", "PETG", ... (as the printer or the hand setting says)
    std::string colour;  // "#RRGGBB", empty if not known
    std::string where;   // "CFS 1 slot B", "Side spool", "Set by hand"
};

// One printer in the shop, as the Device page last saw it.
struct Printer
{
    std::string name;
    std::string mac;
    std::string address;
    std::string model;       // the printer's own model code, e.g. "K1C" or "F008"
    std::string model_name;  // what people call it, e.g. "K1C" or "K2 Plus"
    bool        online{false};
    bool        printing{false};  // printing or paused
    bool        idle{false};
    bool        error{false};
    bool        bed_clear{true};  // false when the last print finished/stopped and nobody said it's been taken off
    int         progress{0};
    double      nozzle{0.0};      // 0 = not known
    bool        reads_spools{false}; // the printer reports what's loaded (K2 Plus CFS)
    bool        hand_set{false};     // what's loaded was set by hand
    std::vector<Spool> spools;
};

// One printer's place in the ranking for the current plate.
struct Pick
{
    Printer     printer;
    bool        suits{false};  // ready, right model and nozzle, right material loaded
    bool        can_send{false}; // online and not printing (you can still choose it by hand)
    long long   score{0};
    std::string reason;  // short, for the card: "idle, PLA Black loaded" / "busy, 42%"
};

// What the current plate needs.
struct Need
{
    std::string model_name;  // "K2 Plus"
    double      nozzle{0.0};
    struct Material
    {
        std::string type;    // "PLA"
        std::string colour;  // "#RRGGBB"
    };
    std::vector<Material> materials;
};

struct Ranking
{
    bool              valid{false}; // false: no plate or no printers yet
    Need              need;
    std::vector<Pick> picks;    // best first
    std::string       why_none; // when nothing suits: why, in plain words
    bool              any_printers{false};
    // When nothing suits but one printer would if its last print were off the bed: that one
    // (the card offers "<name>: bed is clear"). Empty otherwise.
    std::string       bed_check_mac;
    std::string       bed_check_name;
};

// The ranking for the current plate (worked out again about once a second).
const Ranking& ranking();

// The best printer that suits, or nullptr.
const Pick* best();

// "PLA Black" for one of the plate's materials (for the card).
std::string material_words(const Need::Material& m);
// "Black", "White", ... the nearest plain colour name for "#RRGGBB".
std::string colour_name(const std::string& hex);

/* ----- Choosing another printer for this plate (the card's list) ----- */
// The printer chosen by hand for the current plate, or empty (then the best one is used).
std::string chosen_mac();
void        choose(const std::string& mac);   // empty = go back to the best

/* ----- Sending ----- */
// Send the current plate to this printer through the normal send window, which presses
// Start Print by itself once it's ready. False if it couldn't start (printer not found/offline).
bool send_to(const std::string& mac);

// The send window asks this when it opens: the printer to start on, if the window was opened by
// send_to() for this plate in the last minute. Clears it.
bool take_pending(int plate_index, std::string& ip, std::string& mac, std::string& name);

// Auto-send: off unless turned on (saved).
bool auto_send();
void set_auto_send(bool on);
// Called every frame of the job card: sends the current plate if auto-send is on, it's sliced
// and still To print, and a printer suits.
void on_frame();

/* ----- Spools set by hand (printers that can't read their spool, like the K1C) ----- */
struct HandSpool
{
    std::string type;    // empty = not set
    std::string colour;  // "#RRGGBB"
    double      nozzle{0.0}; // 0 = what the printer says
};
HandSpool hand_spool(const std::string& mac);
void      set_hand_spool(const std::string& mac, const HandSpool& spool);

// Printers whose spool is set by hand (or could be): every printer that doesn't report its own.
std::vector<Printer> hand_set_printers();

// The material types and plain colours offered for hand-set spools.
const std::vector<std::string>&                         hand_types();
const std::vector<std::pair<std::string, std::string>>& hand_colours(); // name, "#RRGGBB"

// The last print was taken off this printer's bed (the card's "Bed is clear" button).
void mark_bed_clear(const std::string& mac);

} // namespace RoostPicker
} // namespace GUI
} // namespace Slic3r

#endif
