Two corrections to the request above, and a note on priority.

- **The parser already exists.** `TabletBackend::parse_descriptor` (`Core/Backends/Input/TabletBackend.cpp:253-440`) parses report descriptors: long items, push/pop, extended usages, usage ranges, per-report-id bit cursors and signed logical ranges. The backend already fetches the descriptor with `hid_get_report_descriptor`. Moving that parser into a shared helper would cover most of item 1.
- **The Xbox acceptance test can't pass as written.** On Linux, Xbox pads over USB are bound by `xpad` and never appear in hidraw. The acceptance should name DualShock 4, DualSense or Switch Pro over USB, and Xbox only over Bluetooth, unless the backend also reads evdev.

The game behind this request now navigates with a drawing tablet (see discussion #192), so it no longer needs this. I'm leaving the issue open because the gap is general. Close it if you'd rather track it another way.
