#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/**
 * The number field: a small integer drawn as one bar with a range fill, "<" and ">" arrows (or, in a tall cell,
 * arrows above and below) and the value centred between them. The whole field is one Tab stop. Click an arrow to
 * step, hold it to repeat (0.4 s pause, then faster), drag the number up or down (Shift for fine), double-click it
 * to type, Cmd-click it to reset; Up/Down/Left/Right step from the keyboard. A right click is left to the card's
 * mouse listener. docs/layout/module-card.md#faders-switches-and-steppers.
 */
class CardStepper
    : public juce::Component
    , public juce::SettableTooltipClient
    , private juce::Timer {
public:
    enum class Orientation { horizontal, vertical };
    enum class Zone { none, down, number, up };

    /** `name` is the parameter's display name. */
    explicit CardStepper(const juce::String& name);
    ~CardStepper() override;

    /** Fired with the number of steps the user took (a click, a key, a repeat, a drag, a reset); the owner applies it
     *  and calls setValue and setValueText. Inside onGestureBegin and onGestureEnd. */
    std::function<void(int delta)> onStep;
    /** Around one user gesture (a click, a hold, a drag, a key press): the owner groups its steps into one change. A
     *  gesture that takes no step fires neither. */
    std::function<void()> onGestureBegin;
    std::function<void()> onGestureEnd;
    /** The text the user typed (see setTypable); the owner applies it and sets the value text again, accepted or not.
     */
    std::function<void(const juce::String& text)> onTypedValue;

    /** Lets the user type the value: a double-click on the number, Return, or a digit key opens an editor in place. */
    void setTypable(bool typable);
    bool isTypable() const noexcept { return typable_; }
    void setOrientation(Orientation orientation);
    Orientation getOrientation() const noexcept { return orientation_; }

    /** The range the field walks and its default (Cmd-click). Without it there is no range fill, no limit and no reset.
     */
    void setRange(int minimum, int maximum, int defaultValue);
    void setValue(int value);
    int getValue() const noexcept { return value_; }
    /** The value as the parameter's text; also what a screen reader reads as the value. */
    void setValueText(const juce::String& text);
    juce::String getValueText() const { return valueText_; }
    /** The widest text the field will show (for example "128"), so getIdealWidth fits it. */
    void setWidestValueText(const juce::String& text);
    /** Width that fits the widest value between the arrows, at `forHeight` (0: the field's own height). */
    int getIdealWidth(int forHeight = 0) const;

    Zone zoneAt(juce::Point<int> local) const;
    juce::Rectangle<int> zoneBounds(Zone zone) const;
    bool isRepeating() const noexcept { return holdZone_ != Zone::none; }
    bool isEditing() const noexcept { return editing_; }
    juce::TextEditor& getTextEditor() noexcept { return editor_; }
    /** Opens the editor over the number. */
    void showEditor(const juce::String& initialText = {});

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    juce::MouseCursor getMouseCursor() override;
    void focusLost(FocusChangeType cause) override;
    void focusGained(FocusChangeType cause) override;
    void focusOfChildComponentChanged(FocusChangeType cause) override;
    void enablementChanged() override;
    void visibilityChanged() override;
    juce::String getTooltip() override;

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class ValueInterface;
    class StateProbe;

    void timerCallback() override;
    bool applyStep(int delta);
    void step(int delta);
    void beginGesture();
    void endGesture();
    void startHold(Zone zone);
    void stopHold();
    void commitEditor();
    void closeEditor();
    int arrowSize() const;
    bool atLimit(Zone arrow) const;
    float cornerRadius() const;

    juce::String name_;
    juce::String valueText_;
    juce::String widestText_;
    Orientation orientation_ = Orientation::horizontal;
    bool typable_ = false;
    bool ranged_ = false;
    int min_ = 0, max_ = 0, default_ = 0, value_ = 0;

    Zone hoverZone_ = Zone::none;
    Zone pressedZone_ = Zone::none;
    Zone holdZone_ = Zone::none;
    double holdStartMs_ = 0.0;
    bool dragging_ = false;
    bool gestureOpen_ = false;
    bool blocked_ = false;
    float dragLastY_ = 0.0f;
    double dragRemainder_ = 0.0;

    std::unique_ptr<StateProbe> probe_;
    juce::TextEditor editor_;
    bool editing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardStepper)
};

} // namespace synth::ui
