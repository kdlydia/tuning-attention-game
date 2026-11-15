## Summary

Adds `Context::set_focus(id)`, so keyboard focus can move to a Forma element without a mouse press. Closes #190.

- **What `set_focus` does.** It runs the same focus-gained and focus-lost callbacks, and the `WINDOW_FOCUS_*` subscriptions, as a press that moves focus. Key and text callbacks then reach the element.
- **When it's refused.** It returns false for an id the Layer doesn't hold, or whose element is hidden or not interactive. Focusing the element that already has focus does nothing and returns true.
- **One shared path.** The focus transfer in `handle_press` moves into a private `move_focus`, which `set_focus` and `clear_focus` also use.
- **One behaviour change.** `clear_focus` now also dispatches `WINDOW_FOCUS_LOST` to the element's subscribers, as `subscribe()` documents for any loss of focus. Before, it only ran the `on_focus_lost` callback.
- **Docs.** The keyboard focus section of `docs/Portal-Forma.md` now covers `set_focus`.

No data members change, so `Context`'s layout is the same.

## Testing

- **Build:** this fork's PR Build Check, on Linux, macOS and Windows: passed for `2dd9ce4e` on Linux (Fedora 44, GCC 16), macOS 26 (Apple Silicon) and Windows 2025 (MSVC). [Run](https://github.com/kdlydia/MayaFlux/actions/runs/43750909813)
- **Exercised** in a `user_project.hpp` against the installed nightly (main `38e09827`), with the changed `Context.cpp` compiled into the project. Three elements, the third one hidden:
  - `set_focus(a)`, then `set_focus(b)`: gained a, lost a, gained b.
  - `set_focus(b)` again: returns true, nothing fires.
  - The hidden element and an unknown id are refused.
  - A `KEY_PRESSED` Enter signalled on the window's event source reaches only b's `on_press(Enter)`.
  - `clear_focus()`: lost b, and b's `WINDOW_FOCUS_LOST` subscription fires.
- **Tests:** none added under `tests/`, since the tests live in mayaflux-tests. I'm happy to add one there if you want this covered.

<details><summary>The check, as run</summary>

```cpp
// three elements on one surface; c hidden
layer.set_visible(c, false);
ctx.set_focus(a);     // true
ctx.set_focus(b);     // true
ctx.set_focus(b);     // true, no callbacks
ctx.set_focus(c);     // false
ctx.set_focus(9999);  // false
// next step: Enter as if typed
Core::WindowEvent ev;
ev.type = Core::WindowEventType::KEY_PRESSED;
ev.data = Core::WindowEvent::KeyData { .key = static_cast<int16_t>(IO::Keys::Enter), .scancode = 0, .mods = 0 };
window->get_event_source().signal(ev);
// next step
ctx.clear_focus();
```

Output:

```
returns=11100 focused_after_sets=2 focused_after_clear=none
log: [gained 1] [lost 1] [gained 2] [enter 2] [lost 2] [subscribed-lost 2]
```

</details>
