# Bug Fix Record Template

## 1. Basic Info
- Bug ID: `15013`
- Title: `Clicking "Send print" opens two dialogs`
- Date: `2026-02-24`
- Reporter:
- Assignee:
- Branch/Commit:

## 2. Symptom
- User clicks `Send print` once.
- Two print-related dialogs may appear.
- Impact: duplicate action, confusing UX, risk of duplicate operations.

## 3. Scope
- Module: `GUI print toolbar`
- Key files:
  - `src/slic3r/GUI/GLCanvas3D.cpp`
  - `src/slic3r/GUI/MainFrame.cpp`
  - `src/slic3r/GUI/Plater.cpp`
- Affected flows:
  - Normal device print path
  - Fluidd device print path

## 4. Reproduction (Before Fix)
1. Open Preview page and ensure print is enabled (`enprint=true`).
2. Click `Send print`.
3. Observe `MainFrame::print_plate(...)` can be triggered twice in one frame:
   - Main button click path
   - Dropdown selection callback path
4. Result: two dialogs may be shown.

## 5. Root Cause
- In `GLCanvas3D::_render_slice_control()` there were two independent `print_plate(...)` call paths under the same print UI:
  - Main button click executes print.
  - Dropdown result (`ret >= 0`) also executes print.
- In some interaction timing, both paths can run in one frame and trigger duplicate dialog display.

## 6. Fix Strategy
- Add per-frame single-shot guards in print UI rendering logic:
  - `fluidd_print_triggered` for Fluidd branch.
  - `print_triggered` for normal branch.
- Rule: if main button already triggered `print_plate(...)` in current frame, skip the dropdown-triggered call.
- Keep existing behavior otherwise (dropdown still updates selected print type).

## 7. Code Change Summary
- File: `src/slic3r/GUI/GLCanvas3D.cpp`
  - Added `fluidd_print_triggered` near Fluidd main print button.
  - Added `print_triggered` near normal main print button.
  - Guarded dropdown-triggered `print_plate(...)` with `!fluidd_print_triggered` / `!print_triggered`.

## 8. Verification Checklist
- [ ] Normal device: click main `Send print` once -> only one dialog.
- [ ] Normal device: change option from dropdown -> no duplicate dialog.
- [ ] Fluidd device: click main `Send print` once -> only one dialog.
- [ ] Fluidd device: select dropdown option -> no duplicate dialog.
- [ ] Breakpoint in `MainFrame::print_plate(...)`: one user action hits once.

## 9. Related Commits (Investigation Notes)
- Candidate introducing commit on normal path: `7b94ecc70f`
- Historical behavior setup on normal path: `f4e41c6abe`
- Fluidd path introduction with duplicated trigger logic: `c4d3a6cccd`

## 10. Rollback / Risk
- Rollback: revert `GLCanvas3D.cpp` guard variables and related conditions.
- Risk level: low (localized UI event trigger de-duplication).
- Side effects to monitor:
  - Dropdown action expectations on legacy workflows.
  - Device-type-specific print routes.

## 11. Follow-up
- Add a small UI interaction regression test (if framework supports it) for "single click -> single print trigger".
- Optionally centralize print trigger dispatch to one path to avoid future duplicated calls.
