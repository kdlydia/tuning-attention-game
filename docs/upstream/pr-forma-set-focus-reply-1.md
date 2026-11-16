Thanks. Both tests fail on the first commit, as you said. b70fab2a fixes them.

`move_focus` now sets the focus first, then delivers one notification at a time, reading the state again after each callback or subscriber. If one of them moves focus, the nested call delivers what is still owed, and the outer call then finds nothing left. What it keeps to:

- `set_focus` returns whether the element has focus when it returns.
- An element whose focus-gained callback has run is told once when it loses focus: its callback, then its `WINDOW_FOCUS_LOST` subscriptions. That is the log your first test expects.
- A `WINDOW_FOCUS_GAINED` subscription is skipped if focus has moved on by the time it would run.
- An element that was never told it gained focus is not told it lost it. In your second test `b` gets nothing.

That also fixes the pointer path you mentioned: a focus-lost callback that redirects focus no longer runs twice, and one that calls `clear_focus()` no longer recurses.

Two other changes in behaviour:

- `unbind(id)` of the focused element goes through the same path, so its `WINDOW_FOCUS_LOST` subscriptions are told before they are removed. Before, only the callback ran.
- Inside a focus-lost callback, `focused()` is now the element focus is moving to. Before, it was the element losing focus. Nothing in the library reads it there.

`Context` gains two private members (`m_focus_notified`, `m_focus_pending`), so its layout changes. I've corrected that line in the description.

**Testing**

- I built `portal/forma/context_test.cpp` from mayaflux-tests (`19c7ed2a`, the commit main points to) against the installed nightly, with this `Context.cpp` linked in. All 65 existing tests pass, plus your two and the eight below. On the first commit the 65 pass and your two fail.
- Fork build: passed on Linux (Fedora 44, GCC 16), macOS 26 (Apple Silicon) and Windows 2025 (MSVC) ([run](https://github.com/kdlydia/MayaFlux/actions/runs/43890017796)).

If you want them in mayaflux-tests, I can open a PR there with these ten.

<details><summary>The eight extra cases</summary>

```cpp
TEST_F(FormaContextTest, ARedirectFromFocusLostTellsTheLosingElementOnceAndSkipsTheAbandonedTarget)
{
    const uint32_t a = left_half();
    const uint32_t b = right_half();
    const uint32_t redirected = add(-0.2F, -0.2F, 0.2F, 0.2F);
    ASSERT_TRUE(ctx->set_focus(a));
    std::vector<std::string> log;
    ctx->on_focus_lost(a, [&](uint32_t) {
        log.emplace_back("own lost a");
        ctx->set_focus(redirected);
    });
    ctx->subscribe(a, filter_for(WindowEventType::WINDOW_FOCUS_LOST), log_into(log, "sub lost a"));
    ctx->on_focus_gained(b, [&](uint32_t) { log.emplace_back("own gained b"); });
    ctx->on_focus_lost(b, [&](uint32_t) { log.emplace_back("own lost b"); });
    ctx->subscribe(b, filter_for(WindowEventType::WINDOW_FOCUS_LOST), log_into(log, "sub lost b"));
    ctx->on_focus_gained(redirected, [&](uint32_t) { log.emplace_back("own gained r"); });
    ctx->subscribe(redirected, filter_for(WindowEventType::WINDOW_FOCUS_GAINED), log_into(log, "sub gained r"));

    EXPECT_FALSE(ctx->set_focus(b));

    EXPECT_EQ(ctx->focused(), redirected);
    EXPECT_EQ(log, (std::vector<std::string> { "own lost a", "sub lost a", "own gained r", "sub gained r" }));
}

TEST_F(FormaContextTest, AFocusLostCallbackMayClearFocusWithoutRecursing)
{
    const uint32_t a = left_half();
    const uint32_t b = right_half();
    ASSERT_TRUE(ctx->set_focus(a));
    int lost = 0;
    int b_gained = 0;
    ctx->on_focus_lost(a, [&](uint32_t) {
        ++lost;
        ctx->clear_focus();
    });
    ctx->on_focus_gained(b, [&](uint32_t) { ++b_gained; });

    EXPECT_FALSE(ctx->set_focus(b));

    EXPECT_EQ(lost, 1);
    EXPECT_EQ(b_gained, 0);
    EXPECT_FALSE(ctx->focused().has_value());
}

TEST_F(FormaContextTest, AFocusLostCallbackMayKeepFocus)
{
    const uint32_t a = left_half();
    const uint32_t b = right_half();
    ASSERT_TRUE(ctx->set_focus(a));
    std::vector<std::string> log;
    bool kept = false;
    ctx->on_focus_lost(a, [&](uint32_t) {
        log.emplace_back("own lost a");
        if (!kept) {
            kept = true;
            ctx->set_focus(a);
        }
    });
    ctx->on_focus_gained(a, [&](uint32_t) { log.emplace_back("own gained a"); });
    ctx->subscribe(a, filter_for(WindowEventType::WINDOW_FOCUS_LOST), log_into(log, "sub lost a"));
    ctx->subscribe(a, filter_for(WindowEventType::WINDOW_FOCUS_GAINED), log_into(log, "sub gained a"));
    ctx->on_focus_gained(b, [&](uint32_t) { log.emplace_back("own gained b"); });

    EXPECT_FALSE(ctx->set_focus(b));

    EXPECT_EQ(ctx->focused(), a);
    EXPECT_EQ(log, (std::vector<std::string> { "own lost a", "sub lost a", "own gained a", "sub gained a" }));
}

TEST_F(FormaContextTest, AFocusSubscriberMayMoveFocus)
{
    const uint32_t a = left_half();
    const uint32_t b = right_half();
    std::vector<std::string> log;
    ctx->subscribe(a, filter_for(WindowEventType::WINDOW_FOCUS_GAINED),
        [&](uint32_t, const Core::WindowEvent&, glm::vec2) {
            log.emplace_back("sub gained a");
            ctx->set_focus(b);
        });
    ctx->subscribe(a, filter_for(WindowEventType::WINDOW_FOCUS_LOST), log_into(log, "sub lost a"));
    ctx->subscribe(b, filter_for(WindowEventType::WINDOW_FOCUS_GAINED), log_into(log, "sub gained b"));

    EXPECT_FALSE(ctx->set_focus(a));

    EXPECT_EQ(ctx->focused(), b);
    EXPECT_EQ(log, (std::vector<std::string> { "sub gained a", "sub lost a", "sub gained b" }));
}

TEST_F(FormaContextTest, AFocusCallbackThatMovesFocusAwayAndBackTellsEachChangeOnce)
{
    const uint32_t a = left_half();
    const uint32_t b = right_half();
    std::vector<std::string> log;
    bool bounced = false;
    ctx->on_focus_gained(a, [&](uint32_t) {
        log.emplace_back("own gained a");
        if (!bounced) {
            bounced = true;
            ctx->set_focus(b);
            ctx->set_focus(a);
        }
    });
    ctx->on_focus_lost(a, [&](uint32_t) { log.emplace_back("own lost a"); });
    ctx->subscribe(a, filter_for(WindowEventType::WINDOW_FOCUS_GAINED), log_into(log, "sub gained a"));
    ctx->on_focus_gained(b, [&](uint32_t) { log.emplace_back("own gained b"); });
    ctx->on_focus_lost(b, [&](uint32_t) { log.emplace_back("own lost b"); });

    EXPECT_TRUE(ctx->set_focus(a));

    EXPECT_EQ(ctx->focused(), a);
    EXPECT_EQ(log, (std::vector<std::string> {
                       "own gained a", "own lost a", "own gained b", "own lost b", "own gained a", "sub gained a" }));
}

TEST_F(FormaContextTest, APressWhoseFocusGainedCallbackRedirectsLeavesFocusOnTheRedirectTarget)
{
    const uint32_t a = left_half();
    const uint32_t b = right_half();
    int pressed = 0;
    ctx->on_focus_gained(a, [&](uint32_t) { ctx->set_focus(b); });
    ctx->on_press(a, MouseButtons::Left, [&](uint32_t, glm::vec2) { ++pressed; });

    window->move_to_ndc(k_left);
    window->press(MouseButtons::Left);

    EXPECT_EQ(ctx->focused(), b);
    EXPECT_EQ(pressed, 1);
}

TEST_F(FormaContextTest, UnbindingTheFocusedElementAlsoTellsItsFocusLostSubscribers)
{
    const uint32_t id = left_half();
    int own = 0;
    int sub = 0;
    ctx->on_focus_lost(id, [&](uint32_t) { ++own; });
    ctx->subscribe(id, filter_for(WindowEventType::WINDOW_FOCUS_LOST), count_into(sub));
    ASSERT_TRUE(ctx->set_focus(id));

    ctx->unbind(id);

    EXPECT_EQ(own, 1);
    EXPECT_EQ(sub, 1);
    EXPECT_FALSE(ctx->focused().has_value());
}

TEST_F(FormaContextTest, RebindingAfterUnbindDoesNotReplayAStaleLoss)
{
    const uint32_t a = left_half();
    const uint32_t b = right_half();
    ASSERT_TRUE(ctx->set_focus(a));
    ctx->unbind(a);
    int lost = 0;
    ctx->on_focus_lost(a, [&](uint32_t) { ++lost; });

    EXPECT_TRUE(ctx->set_focus(b));

    EXPECT_EQ(lost, 0);
}
```

</details>
