## Summary

`InputConfig::cursor_mode` offers `HIDDEN`, `DISABLED` ("Invisible and locked (FPS camera)") and `CAPTURED` ("Invisible, locked, raw motion"), but only the Cocoa backend applies them. On Wayland, `set_input_config` stores the config and nothing reads `cursor_mode`. So a first-person camera on Linux can't hide or keep the pointer, and the motion stops at the window edge.

## Current behaviour

Read on develop `2084dfae`; these headers are the same in the nightly from main `38e09827`.

- The modes and `InputConfig`: `Core/GlobalGraphicsInfo.hpp:446-465`.
- Wayland: `WaylandWindow::set_input_config` only assigns `m_input_config` (`Core/Backends/Windowing/Wayland/WaylandWindow.hpp:53`). Nothing in `Wayland/` reads `cursor_mode`.
- Win32 does the same (`Core/Backends/Windowing/Win32/Win32Window.hpp:50`).
- Cocoa applies the mode through `setCursorMode` (`Core/Backends/Windowing/Cocoa/CocoaWindow.mm:988-1002`, `340-349`).

## Proposal

For the Wayland backend:

1. **`HIDDEN`:** set a null cursor surface (`wl_pointer_set_cursor` with no surface) while the pointer is over the window.
2. **`DISABLED` and `CAPTURED`:**
   - Lock the pointer with `zwp_pointer_constraints_v1` (`lock_pointer`, persistent lifetime).
   - Read motion from `zwp_relative_pointer_manager_v1`. `CAPTURED`, or `raw_mouse_motion`, uses the unaccelerated deltas.
   - The relative motion has to reach `WindowEvent`, for example as delta fields on `MOUSE_MOTION` or as its own event. Either works for `Kinesis::apply_mouse_delta`.
3. **Release** the lock when the mode returns to `NORMAL` or the window loses focus. Re-lock it on focus gain.
4. **Missing protocols:** if the compositor doesn't offer them, log one warning and keep `NORMAL` behaviour.

Win32 has the same gap (`ClipCursor` plus raw input). It could be a separate item.

## Acceptance

- A Wayland window under KWin, and under GNOME, with `cursor_mode = DISABLED`: the pointer is hidden and stays in the window, and motion keeps arriving when the mouse moves past the edge.
- Setting `NORMAL` again frees and shows the pointer.
- Alt-tab releases the lock, and returning re-locks it.
- On a compositor without the protocols there is one warning, and the window works as `NORMAL`.

## Not requested

No changes to the Cocoa path.

## Context

I'm planning a first-person game on Nexus: a `Locus` driven by `NavigationState` and `apply_mouse_delta`. Right-drag look works for now. Mouse look with a locked pointer is what's missing on Linux.
