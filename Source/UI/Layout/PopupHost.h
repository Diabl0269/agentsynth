#pragma once

// PopupHost.h -- the one way a popover (a juce::CallOutBox with content) opens in the app. A parentless CallOutBox is
// its own top-level window, so JUCE's own launch leaves four things to chance: a key the content does not use never
// reaches the main window (Cmd+Z, Space), Escape closes it with no fade and no way for the content to put things back,
// a click on the button that opened it closes it and then re-opens it, and the entrance is animated by whatever the
// content brings. launchPopover and PopoverSlot settle all four (docs/layout/card-popovers.md,
// docs/layout/animation.md#popup-windows): one motion engine (PopupMotion), Escape and click-away rules in one place,
// and the app's shortcuts handed to the main window the way the shape editor window does (makeAppShortcutRelay).
#include "UI/Layout/PopoverContent.h"
#include "UI/Layout/PopoverPointer.h"
#include "UI/Layout/PopupMotion.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {

struct PopoverOptions {
    /** The content lists project state and does not implement AppUndoAware: the popover closes after an undo or redo
     *  rather than show it stale. */
    bool closeOnUndo = false;
    /** The button that opened the popover: a press on it closes the popover and is consumed (no re-open). */
    juce::Component* trigger = nullptr;
    /** While this says true (a press that opened the popover, a long press, is still down) a press outside is not a
     *  dismissal: the release that ends the opening press must not close it. */
    std::function<bool()> holdGestureDown;
    /** The motion; the popover rises 8 px out of the middle of the area it points at unless this says otherwise. */
    popup_motion::Style style = [] {
        popup_motion::Style s;
        s.inSlidePx = 8.0f;
        return s;
    }();
};

/** Opens `content` in a CallOutBox pointing at `area` (screen coordinates) and relays app shortcuts to the main window
 *  that holds `anchor` (a card or any component on it, or any panel of the main window: timeline, mixer, MIDI
 *  Remote...). Escape closes it through the live fade (after PopoverContent::onPopoverCancel); a press outside closes
 *  it and lands. Returns the box (null only when a test launcher declines). */
juce::CallOutBox* launchPopover(std::unique_ptr<juce::Component> content, juce::Rectangle<int> area,
                                juce::Component* anchor, PopoverOptions options = {});

/** The popover its opener has open, if any. Own one as a member of the opener, and open every launch through it: a
 *  second request while it is open closes it (the button toggles), one while it is leaving does nothing. */
class PopoverSlot {
public:
    enum class Result { Opened, ClosedIt, Ignored };

    Result open(const std::function<std::unique_ptr<juce::Component>()>& makeContent, juce::Rectangle<int> area,
                juce::Component* anchor, PopoverOptions options = {});

    /** True while a popover this slot opened is on screen (leaving counts). */
    bool isOpen() const;
    /** Fades the popover out; nothing when none is open. */
    void close();
    juce::CallOutBox* box() const { return box_.getComponent(); }
    juce::Component* content() const { return content_.getComponent(); }

private:
    juce::Component::SafePointer<juce::CallOutBox> box_;
    juce::Component::SafePointer<juce::Component> content_;
};

/** Makes `box` relay forwardable app shortcuts (see isWindowForwardableShortcut) for a card `anchor`. A key listener on
 *  the box sees a key after the focused child and every component between it and the box have declined it. Frees
 *  itself when the box goes. launchPopover calls this; a popover built outside it (the help popover) calls it too. */
void relayAppShortcutsFromCallOut(juce::CallOutBox& box, juce::Component* anchor, bool closeOnUndo = false);

namespace test_hooks {
/** What a real key press gives the box's key listeners: offers `key` to the relay `box` got from
 *  relayAppShortcutsFromCallOut, as the peer does after the focused child and its parents declined it. False when the
 *  box has no relay or the relay declines the key. */
bool offerKeyToPopoverForTest(juce::CallOutBox& box, const juce::KeyPress& key);

/** When set, launchPopover hands the content here instead of opening a window (a CallOutBox needs a desktop) and
 *  relays through the box the hook returns. */
using PopoverLauncher = std::function<juce::CallOutBox*(std::unique_ptr<juce::Component>, juce::Rectangle<int>)>;
PopoverLauncher& popoverLauncherForTest();
} // namespace test_hooks

} // namespace synth::ui
