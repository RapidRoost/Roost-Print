# Roost Print

Rapid Roost's slicer for the shop's printers. It's [Creality Print](https://github.com/CrealityOfficial/CrealityPrint) (7.0) with these changes:

1. **The look.** Rapid Roost blue instead of Creality green, and blue-tinted navy and slate instead of flat greys (like rapidroost.net at night). The site's fonts: Manrope for text, Orbitron for big headings. The Rapid Roost printer-and-chicken icon, its own splash screen, the name Roost Print, and RAPID ROOST on the build plates instead of Creality's logo.
2. **The printers.** On a fresh install, the shop's printers are set up straight away: Creality K2 Plus and Creality K1C, 0.4 mm nozzles. No first-run printer picker.
3. **Polymaker filament.** Every Polymaker material has a profile for the K2 Plus and the K1C, and those are the ones shown. Creality's filaments are still installed, just hidden (tick them back on in the filament list).
4. **PrintCam.** A PrintCam tab next to Device shows PrintCam's page. Roost Print keeps PrintCam running in the background, names every print it sends for its order (`RR1042_...`, asking when it can't tell) so customers always get their print, shows **PrintCam (2)** while customers wait for a yes/no on their email, and warns when something would stop a customer watching. It never holds PrintCam's settings or keys: those stay in PrintCam's own folder. See `tools/roost/PRINTCAM.md`.
5. **Sending doesn't lock the app.** The send-to-printer window is an ordinary window: you can minimise it, keep slicing, and open another for the next plate while one uploads (up to 3 printers upload at once).
6. **No Creality updates.** No update checks, no "Update Ready" window, no downloads or installs, no plugin or profile updates from Creality. Roost Print updates come from this repo.
7. **Removed.** AI Creation, Online Models, the AI/Pro mode switch (it's always the full slicer), the customer support button and the AI assistant bot.
8. **No cloud profile packages.** Printer and filament profiles come with the app, so Creality's online packages can't replace them.
9. **The simple view.** Opens straight to the model and one job card: the plate's stage, printer, material, print time and filament, **Send to printer**, and buttons to mark a plate by hand. The settings panel, toolbars and editing tools are hidden, and each plate slices by itself a moment after anything changes (staying on the model; **Layers** shows the sliced layers). **View → Simple Roost view** (or **Full slicer** on the card) goes back to the whole slicer for odd jobs.
10. **Plate stages.** Each plate's bed is tinted by where it's up to: blue border = to print, yolk = printing, green = printed, red = failed. Sending a plate makes it printing; PrintCam reports when that file finishes (green) or stops (red). Stages are saved per file and plate, so they're there when the file is opened again, and Roost Print asks before sending a plate that's already printed.
11. **The fastest way up.** Every part opened in the simple view gets checked thousands of ways up (height, support, first layer, tipping over), the best few are test-sliced with the shop's settings, and the fastest wins. The card says how much time it saved, with **Undo the turn**. Plates already sent are never reshuffled.
12. **Shop lighting.** Models in Prepare are lit by the shop's 360 light and warm key light, the same as rapidroost.net's 3D preview (AgX tone mapping, satin PLA). **View → Shop lighting** turns it off. The layer preview stays plain.

Roost Print keeps its settings in its own folder, so it can sit next to a normal Creality Print.

## Download

Actions tab → **Build Roost Print** → the latest green run → **RoostPrint-Windows** (a zip). Unzip it anywhere and run `RoostPrint.exe`.

Tagged versions (`v1.0.0` and so on) are also on the Releases page.

## How it's made

The changes live in a few small places, so Creality's updates can be merged in:

| What | Where |
| --- | --- |
| Green to blue, everywhere | `tools/roost/recolor.py` (run it again after merging an update) |
| Greys to navy and slate | `tools/roost/tint.py` (after `recolor.py`) |
| Names, exe name, settings folder | `tools/roost/rebrand.py` |
| Icon, splash, top-bar logo | `tools/roost/make_art.py`, from `tools/roost/art/` |
| Build plates | `tools/roost/plates.py` |
| Chickens for every message and alert; Creality logos replaced | `tools/roost/alerts.py`; used in `MsgDialog.cpp` (also every `wxMessageBox`, via `pchheader.hpp`) and `Config/DispConfig.cpp` |
| No "Creality" in on-screen text | `I18N::debrand` in `src/slic3r/GUI/I18N.cpp` (all translated text) and the page script in `Widgets/WebView.cpp` (the app's web pages) |
| Fonts | `resources/fonts/` (Manrope, Orbitron); `Widgets/Label.cpp`, `ImGuiWrapper.cpp`, and `Widgets/WebView.cpp` for the app's web pages |
| Polymaker profiles | `tools/roost/polymaker.py`, from `tools/roost/polymaker/`; turned on by `GUI_App::roost_apply_polymaker()` |
| Printers on first run | `GUI_App::roost_install_fleet()` in `src/slic3r/GUI/GUI_App.cpp` |
| PrintCam (tab, order names, watching) | `src/slic3r/GUI/RoostPrintCam.cpp`, wired in `MainFrame.cpp`, `BBLTopbar.cpp`, `Plater.cpp`, `SendToPrinter.cpp`; design in `tools/roost/PRINTCAM.md` |
| Creality updates off | marked `Roost Print:` in `GUI_App.cpp` (checks, "Update Ready"), `AppUpdater.cpp` (download, install), `Utils/PresetUpdater.cpp` (background sync), `MainFrame.cpp` (Help menu) |
| Send window | `print_manage/App/SendToPrinter.cpp` (`set_modeless`) and `Plater.cpp` |
| Removed features | marked `Roost Print:` in `BBLTopbar.cpp`, `MainFrame.cpp`, `GUI_App.cpp`, `GLCanvas3D.cpp`, `simple/MCPChatPanel.cpp` |
| Simple view (job card, auto-slice, hidden tools) | `src/slic3r/GUI/RoostSlim.cpp`; hooked in `GLCanvas3D.cpp` (`_render_overlays_roost`, toolbar clicks), `MainFrame.cpp` (View menu), `Plater.cpp` (`roost_slice_in_place`) |
| Plate stages | `src/slic3r/GUI/RoostStages.cpp`; tint in `PartPlate.cpp` (`render_roost_stage`), sent from `SendToPrinter.cpp` (`fire_print_send_event`), finished/failed from PrintCam in `RoostPrintCam.cpp` |
| Fastest way up | `src/libslic3r/RoostOrientCore.cpp` (the quick round), `src/slic3r/GUI/RoostOrient.cpp` (test slices, turning, undo) |
| Shop lighting | `resources/shaders/{110,140}/gouraud.{vs,fs}`; the light is baked in by `tools/roost/studio_light.py` from `tools/roost/studio/` (copied from the website's `public/studio/`) |
| Windows build | `.github/workflows/roost_build.yml` |

To pick up a new Creality Print: merge it in, then run `python tools/roost/recolor.py`, `python tools/roost/tint.py`, `python tools/roost/rebrand.py`, `python tools/roost/make_art.py`, `python tools/roost/plates.py` and `python tools/roost/polymaker.py`.

## Licence

Creality Print is free software under the GNU Affero General Public License v3, and so is Roost Print (see `LICENSE.txt`). This is a modified version, changed by Rapid Roost as listed above. Creality Print's own readme is kept as `README-CrealityPrint.md`.

The Polymaker material settings in `tools/roost/polymaker/` come from OrcaSlicer's filament library (AGPL-3.0). The Orbitron and Manrope fonts are under the SIL Open Font License (`resources/fonts/OFL-Orbitron.txt`, `OFL-Manrope.txt`). "Creality", "Creality Print" and "Polymaker" are their owners' names; Roost Print isn't made or endorsed by Creality or Polymaker.
