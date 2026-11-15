## Summary

`HIDNode` reads a control by its byte offset in the raw report. Each controller model therefore needs its offsets written by hand, and one game needs different code for each pad. A binding by HID usage, with a standard gamepad profile on top of it, would let a project ask for "left stick X" or "button south" instead.

## Current behaviour

Read on develop `2084dfae`; the headers are the same in the nightly from main `38e09827`.

- `HIDBackend` passes each report on as raw bytes (`Core/Backends/Input/HIDBackend.cpp:453-463`).
- `HIDConfig::axis(offset, bytes, signed)`, `button(offset, mask)` and `custom(fn)` read from those bytes (`Nodes/Input/HIDNode.hpp`).
- Axes are normalised to [0, 1] through `with_range(min_raw, max_raw)`, which the caller has to know.
- The deadzone is applied around 0.5 on each axis on its own (`HIDNode.cpp`, `apply_deadzone`). That suits one centred axis, but not a trigger that rests at 0, and not a stick, whose deadzone is usually radial.
- `GlobalInputConfig::with_gamepads()` already filters devices by usage page 0x01, usage 0x05.

## Proposal

1. **Bind by usage.** Read the report descriptor (hidapi's `hid_get_report_descriptor`) and let `HIDConfig` name a usage instead of an offset. For example: Generic Desktop X, Y, Z, Rx, Ry, Rz and Hat Switch, and the Button page by index. Normalise with the logical minimum and maximum from the descriptor.
2. **A standard gamepad profile on top**, mapping usages to named controls:
   - sticks in [-1, 1], resting at 0
   - triggers in [0, 1]
   - the face buttons, the shoulders, the d-pad, start and back
   - a table of overrides by vendor and product id, for pads that report differently
3. **Deadzones:** radial for a stick pair, and none by default for triggers.
4. **Hotplug:** connect and disconnect, through the existing device callback.

## Acceptance

- Over USB, an Xbox-style pad and a DualSense-style pad both give left X and left Y in [-1, 1] with 0 at rest, and triggers at 0 released and 1 fully pressed.
- The bottom face button (A on one, cross on the other) arrives as the same named control.
- None of this needs per-pad byte offsets in project code.
- Existing offset-based `HIDConfig`s keep working.

## Not requested

A rumble or LED API. `HIDBackend::write` and `send_feature_report` already exist, and that could be a follow-up.

## Context

Gamepad play in a game I'm planning.
