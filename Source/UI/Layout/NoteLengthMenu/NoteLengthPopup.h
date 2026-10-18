#pragma once

#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthMenuModel.h"
#include "UI/Layout/PopupMotion.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

/** The note-length popup (docs/layout/note-length-menu.md): a compact window with three letter buttons (S straight,
 *  T triplet, D dotted) over one row of narrow chips. The chips write "1/" once, as a prefix, and only the bottom
 *  number on each chip (1 2 4 ... 128); extra names that come before the notes ("Off", "Bar") sit in front of them
 *  behind a divider, and bar lengths or extras that come after sit behind another. Picking a chip calls onPick with
 *  the choice name and closes the popup; Esc closes it without a change.
 *
 *  Keys: S / T / D switch the kind, Left / Right move between the buttons and chips, Up / Down walk the same order
 *  (ArrowKeyNavigation), Enter or Space picks the focused one, Esc closes. The window grows out of the box that opened
 *  it and returns into it (PopupMotion). One window per box; the box owns it. */
class NoteLengthPopup final
    : public juce::Component
    , private juce::Timer {
public:
    struct Options {
        juce::StringArray choices; ///< Every choice name of the list, in list order.
        juce::String current;      ///< The name the popup opens on (may be empty).
        juce::String title;        ///< The screen-reader name of the popup.
        /** Tooltip of a letter whose kind the list does not have; empty uses a general text. */
        std::array<juce::String, 3> unavailableTip;
        juce::ApplicationProperties* appProperties = nullptr; ///< Where "Show info tooltips" is read; may be null.
    };

    /** `anchor` is the box the popup grows out of; it must outlive the popup. */
    NoteLengthPopup(juce::Component& anchor, Options options);
    ~NoteLengthPopup() override;

    /** Called with the picked choice name, before the popup closes. */
    std::function<void(const juce::String&)> onPick;
    /** Called once the popup is gone (picked, cancelled or clicked away); the owner then deletes it. */
    std::function<void()> onClosed;

    /** Puts the popup on the desktop beside its anchor, in front, with the soft entrance every popup has. */
    void showOnDesktop();
    /** Closes through the soft exit; onClosed runs when it is gone (at once when it has no native window). */
    void close();
    bool isClosing() const noexcept { return closing_; }
    /** True when the popup closed from the keyboard or a pick, so the owner takes the keyboard focus back; false for a
     *  click elsewhere, which has moved the focus on its own. */
    bool restoresFocus() const noexcept { return restoreFocus_; }

    NoteKind getKind() const noexcept { return kind_; }
    /** Switches the chips to `kind`; ignored when the list has no value of that kind. */
    void setKind(NoteKind kind);
    /** The letter button of `kind`. */
    juce::Button& kindButton(NoteKind kind);
    /** The chips on show now, left to right (leading, numbers of the current kind, trailing). */
    std::vector<juce::Button*> shownChips() const;
    /** The button or chip that holds the keyboard cursor, or null. */
    juce::Button* activeItem() const;

    /** The grow the popup attaches to its window: the body scales out of the point of the box it hangs from. */
    popup_motion::Style motionStyle() const;
    AppTooltipWindow* tooltipWindow() noexcept { return tooltipWindow_.get(); }
    const NoteLengthMenuModel& model() const noexcept { return model_; }

    bool keyPressed(const juce::KeyPress& key) override;
    void resized() override;
    /** A press anywhere outside the popup (the box that opened it included) closes it. */
    void mouseDown(const juce::MouseEvent& e) override;

    /** Test seam: showOnDesktop() shows the popup in place and never opens a native window. */
    static void setSkipDesktopForTest(bool skip);

    class Body;
    class Item;

private:
    void buildChips();
    void applyKind();
    void layoutRows();
    int rowWidth(NoteKind kind) const;
    void place();
    std::vector<juce::Button*> focusOrder() const;
    void stepActive(int direction);
    void noteFocus(juce::Button& item);
    void pickChip(const juce::String& name);
    void timerCallback() override;

    juce::Component::SafePointer<juce::Component> anchor_;
    Options options_;
    NoteLengthMenuModel model_;
    NoteKind kind_ = NoteKind::straight;
    std::unique_ptr<Body> body_;
    std::array<std::unique_ptr<Item>, 3> kindButtons_;
    std::vector<std::unique_ptr<Item>> leading_;
    std::vector<std::unique_ptr<Item>> numbers_; ///< One per denominator any kind has, longest note first.
    std::vector<std::unique_ptr<Item>> trailing_;
    std::unique_ptr<ArrowKeyNavigation> arrowKeys_;
    std::unique_ptr<AppTooltipWindow> tooltipWindow_;
    juce::Component::SafePointer<juce::Button> active_;
    juce::Point<float> pivot_;
    juce::Point<int> anchorPoint_;
    bool closing_ = false;
    bool restoreFocus_ = false;
    bool listening_ = false;

    JUCE_DECLARE_NON_COPYABLE(NoteLengthPopup)
};

} // namespace synth::ui
