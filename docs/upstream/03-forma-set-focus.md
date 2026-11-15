## Summary

In Forma, focus moves only when the mouse presses an element, and key and text callbacks reach only the focused element. `Context` has `focused()` and `clear_focus()` but no way to set focus. So a menu driven by arrow keys or a gamepad can't move focus, and an element's key callbacks, `on_focus_gained` and `on_focus_lost`, and `TextField` editing can only be reached after a mouse click.

## Current behaviour

Read on develop `2084dfae`; the header is the same in the nightly from main `38e09827`.

- `m_focused` is set to an element only in the press handling (`Portal/Forma/Context.cpp:501-515`). Other assignments clear it.
- The public focus API is `on_focus_gained`, `on_focus_lost`, `clear_focus` and `focused` (`Portal/Forma/Context.hpp:245-290`).
- The guide states the same: "Keyboard focus transfers to an element on mouse press. Key events route only to the focused element." (`docs/Portal-Forma.md`, "Keyboard focus and key events").

## Proposal

1. **`Context::set_focus(uint32_t id)`.** It runs the same focus-lost and focus-gained callbacks and the `WINDOW_FOCUS_*` dispatch as a press does. It refuses an element that is hidden or not interactive.
2. **Optionally, traversal.** `focus_next()` and `focus_previous()` over the visible, interactive elements, in Layer order or in an order the caller gives, so Tab or a d-pad can walk a panel.
3. **Optionally, `activate()`.** It delivers the focused element's press and release, as Enter or a gamepad button would.

Item 1 alone is enough to build the rest in a project.

## Acceptance

- Three buttons. `set_focus(b2)` fires `b1`'s focus-lost and `b2`'s focus-gained.
- A key callback registered on `b2` then receives a key press, with no mouse involved.
- `set_focus` on a hidden element changes nothing.
- If item 2 is adopted, `focus_next()` from `b3` wraps to `b1`.

## Not requested

Layout or styling changes.

## Context

Pause and settings menus in a game, played with a keyboard or a gamepad.
