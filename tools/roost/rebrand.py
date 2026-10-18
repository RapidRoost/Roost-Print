"""
Roost Print: the names. Shows "Roost Print" where Creality Print said its own name, runs as
RoostPrint.exe, and keeps its settings in its own folder, so it sits happily next to a normal
Creality Print install. The names Creality's servers know the app by are left as they are, so
printer profiles and updates for the printers still come down.

Run from the repo root: python tools/roost/rebrand.py   (safe to run again)
"""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

EDITS = {
    'version.inc': [
        ('set(SLIC3R_APP_USE_FORDER "Creality Print")', 'set(SLIC3R_APP_USE_FORDER "Roost Print")'),
        ('set(PROCESS_NAME "CrealityPrint")', 'set(PROCESS_NAME "RoostPrint")'),
        ('set(VENDOR_NAME "Creality")', 'set(VENDOR_NAME "Rapid Roost")'),
        ('set(PROJECT_VERSION_EXTRA "Beta")', 'set(PROJECT_VERSION_EXTRA "Roost")'),
    ],
    'src/libslic3r/libslic3r.h': [
        ('#define SLIC3R_APP_FULL_NAME "Creality Print"', '#define SLIC3R_APP_FULL_NAME "Roost Print"'),
        ('#define GCODEVIEWER_APP_NAME "Creality3DSlicer G-code Viewer"', '#define GCODEVIEWER_APP_NAME "Roost Print G-code Viewer"'),
    ],
    'src/slic3r/Utils/Process.cpp': [
        ('? "CrealityPrint.exe" :', '? SLIC3R_PROCESS_NAME ".exe" :'),
        ('? "CrealityPrint" : "bambu-gcodeviewer"', '? SLIC3R_PROCESS_NAME : "bambu-gcodeviewer"'),
    ],
    'src/slic3r/GUI/Plater.cpp': [
        ('SetTitle(m_project_name + " - CrealityPrint");', 'SetTitle(m_project_name + " - RoostPrint");'),
    ],
    # Finding its own other window (one copy open at a time): look for ours, not Creality Print's.
    'src/slic3r/GUI/GUI_App.cpp': [
        ('wndTextString.find(L"CrealityPrint")', 'wndTextString.find(L"RoostPrint")'),
        ('std::wstring prog_id = L" Creality.Slicer.1";', 'std::wstring prog_id = L" RapidRoost.RoostPrint.1";'),
        ('std::wstring prog_desc = L"CrealityPrint";', 'std::wstring prog_desc = L"Roost Print";'),
    ],
    'src/slic3r/GUI/InstanceCheck.cpp': [
        ('wndTextString.find(L"CrealityPrint")', 'wndTextString.find(L"RoostPrint")'),
    ],
    'src/slic3r/GUI/MainFrame.cpp': [
        ('_L("Quit %s"), SLIC3R_APP_NAME)', '_L("Quit %s"), SLIC3R_APP_FULL_NAME)'),
        ('wxString(SLIC3R_APP_NAME) + " - " + _L("Settings")', 'wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Settings")'),
    ],
    'package/package.cmake': [
        ('set(DESKTOP_LINK_NAME "Creality Print")', 'set(DESKTOP_LINK_NAME "Roost Print")'),
        ('"Creality Print is an open source slicer for FDM printers"', '"Roost Print: Rapid Roost\'s slicer, built on Creality Print"'),
        ('"CrealityPrint-${CREALITYPRINT_VERSION_MAJOR}"', '"RoostPrint-${CREALITYPRINT_VERSION_MAJOR}"'),
        ('set(CPACK_PACKAGE_EXECUTABLES "CrealityPrint;CrealityPrint")', 'set(CPACK_PACKAGE_EXECUTABLES "RoostPrint;Roost Print")'),
    ],
}

def main():
    missing = 0
    for rel, pairs in EDITS.items():
        p = os.path.join(ROOT, rel)
        t = open(p, encoding='utf-8', errors='surrogateescape').read()
        for old, new in pairs:
            if new in t and old not in t:
                continue
            if old not in t:
                print(f'NOT FOUND in {rel}: {old}')
                missing += 1
                continue
            t = t.replace(old, new)
        open(p, 'w', encoding='utf-8', errors='surrogateescape').write(t)
    print('done' if not missing else f'{missing} edits not found')
    sys.exit(1 if missing else 0)

if __name__ == '__main__':
    main()
