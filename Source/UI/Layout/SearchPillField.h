#pragma once

#include "UI/Layout/NavigationSearchField.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ShortcutManager;

namespace synth::ui {

/** The one search box of the app (docs/layout/search-field.md): a 26 px pill with a magnifier on the left, a
 *  placeholder, and the shortcut that focuses it as a key cap on the right while the field is empty and wide enough.
 *  While there is text, a round clear button takes the hint's place. A site sets its bounds, title, placeholder,
 *  tooltip and (when a rebindable action focuses the field) the action, and never paints or colours the field itself:
 *  every colour comes from the theme, so all themes look the same everywhere. */
class SearchPillField
    : public NavigationSearchField
    , private juce::TextEditor::Listener {
public:
    static constexpr int kHeight = 26;
    static constexpr int kRingOutset = 4; // room outside the pill that the focus ring paints into

    SearchPillField();
    ~SearchPillField() override;

    /** The grey line shown while the field is empty, focused or not. */
    void setPlaceholder(const juce::String& text);
    const juce::String& getPlaceholder() const noexcept { return placeholder_; }

    /** The key cap shown at the right while the field is empty and wide enough ("" for none). */
    void setShortcutHint(const juce::String& keyLabel);
    /** The hint from the shortcut settings: the live binding of `actionId`, none when it is unbound or `manager` is
     *  null. Call again when the bindings change. */
    void setShortcutFromAction(const ShortcutManager* manager, const juce::String& actionId);
    const juce::String& getShortcutHint() const noexcept { return hint_; }

    /** The tooltip, "<text> (<key>)" while a hint is set. */
    void setSearchTooltip(const juce::String& text);

    // Geometry the tests and layout checks read; all in this component's coordinates.
    juce::Rectangle<int> getMagnifierBounds() const;
    juce::Rectangle<int> getClearButtonBounds() const;
    juce::Rectangle<int> getShortcutHintBounds() const; // where the cap sits, whether it shows now or not
    juce::Rectangle<int> getTextArea() const;           // what the text and the placeholder may use

    /** True while the hint is shown or fading in (the logical answer; it fades over 110 to 160 ms). */
    bool isShortcutHintShown() const noexcept;
    /** True while the clear button is shown or fading in. */
    bool isClearButtonShown() const;
    juce::Component& getClearButton();

    /** Empties the field, keeps the focus in it and fires onTextChange (what the clear button does). */
    void clearText();

    /** Hides juce::TextEditor::setText so a silent change (`dontSendNotification`) also updates the hint and the clear
     *  button at once. */
    void setText(const juce::String& newText, bool sendTextChangeMessage = true);

    bool keyPressed(const juce::KeyPress& key) override;
    void insertTextAtCaret(const juce::String& text) override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void parentHierarchyChanged() override;
    void focusGained(FocusChangeType type) override;
    void focusLost(FocusChangeType type) override;

private:
    class ClearButton;

    void textEditorTextChanged(juce::TextEditor&) override { refreshAccessories(); }
    void applyTheme();
    bool hintFits() const;
    int capWidth() const;
    int rightReserve(bool hasText, bool hintShown) const;
    void refreshAccessories();
    bool accessoriesOutOfStep() const;
    void updateRing();
    void repaintWithRing();
    void updateTooltip();

    juce::String placeholder_;
    juce::String hint_;
    juce::String tooltipBase_;
    std::unique_ptr<ClearButton> clear_;
    struct Fades; ///< the hint, clear-button and focus-ring fades, kept in the .cpp so this header stays light
    std::unique_ptr<Fades> fades_;
};

} // namespace synth::ui
