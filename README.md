# Roost Print

Rapid Roost's slicer for the shop's printers. It's [Creality Print](https://github.com/CrealityOfficial/CrealityPrint) (7.0) with three changes:

1. **The look.** Rapid Roost blue instead of Creality green, the Rapid Roost printer-and-chicken icon, its own splash screen, and the name Roost Print.
2. **The printers.** On a fresh install, the shop's printers are set up straight away: Creality K2 Plus and Creality K1C, 0.4 mm nozzles. No first-run printer picker.
3. **PrintCam.** A PrintCam tab next to Device shows PrintCam's page. Roost Print starts PrintCam in the background when it opens, if PrintCam is installed on the computer. It never holds PrintCam's settings or keys: those stay in PrintCam's own folder.

Roost Print keeps its settings in its own folder, so it can sit next to a normal Creality Print.

## Download

Actions tab → **Build Roost Print** → the latest green run → **RoostPrint-Windows** (a zip). Unzip it anywhere and run `RoostPrint.exe`.

Tagged versions (`v1.0.0` and so on) are also on the Releases page.

## How it's made

The changes live in a few small places, so Creality's updates can be merged in:

| What | Where |
| --- | --- |
| Green to blue, everywhere | `tools/roost/recolor.py` (run it again after merging an update) |
| Names, exe name, settings folder | `tools/roost/rebrand.py` |
| Icon, splash, top-bar logo | `tools/roost/make_art.py`, from `tools/roost/art/` |
| Printers on first run | `GUI_App::roost_install_fleet()` in `src/slic3r/GUI/GUI_App.cpp` |
| PrintCam tab | `src/slic3r/GUI/RoostPrintCam.cpp`, wired in `MainFrame.cpp` and `BBLTopbar.cpp` |
| Windows build | `.github/workflows/roost_build.yml` |

To pick up a new Creality Print: merge it in, then run `python tools/roost/recolor.py`, `python tools/roost/rebrand.py` and `python tools/roost/make_art.py`.

## Licence

Creality Print is free software under the GNU Affero General Public License v3, and so is Roost Print (see `LICENSE.txt`). This is a modified version, changed by Rapid Roost as listed above. Creality Print's own readme is kept as `README-CrealityPrint.md`.

The Orbitron font (`tools/roost/art/`) is under the SIL Open Font License (`Orbitron-LICENSE.txt`). "Creality" and "Creality Print" are Creality's names; Roost Print isn't made or endorsed by Creality.
