#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * One row of PluginKnobPickerComponent's list: a checkbox, the parameter's display name, and --
 * only while checked -- a drag handle (reordering is scoped to the checked group; an unchecked row
 * has no order of its own) and a label text field (commits on focus-lost/Return, empty = the
 * parameter's own name). Pure UI: it holds no CardLayout/CardSlot of its own, only what it needs to
 * draw and to report a gesture back to the owning PluginKnobPickerComponent, which decides what any
 * of it means (docs/control/plugin-card-layout.md#choosing-knobs).
 */
class PluginKnobPickerRow final : public juce::Component {
public:
    PluginKnobPickerRow(juce::String paramId, juce::String displayName);
    ~PluginKnobPickerRow() override = default;

    const juce::String& getParamId() const noexcept { return paramId_; }

    /** Message thread. Does not fire onToggled. */
    void setChecked(bool checked);
    bool isChecked() const noexcept { return checkbox_.getToggleState(); }

    /** Message thread. Does not fire onLabelCommitted. Empty = the parameter's own name (shown as
     *  placeholder text, not typed into the field). */
    void setLabelText(const juce::String& text);
    juce::String getLabelText() const { return labelEditor_.getText(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    /** The checkbox was ticked/unticked by a click on THIS row. */
    std::function<void(bool checked)> onToggled;
    /** The label field committed (focus lost or Return) with new text (already trimmed). */
    std::function<void(const juce::String& text)> onLabelCommitted;
    /** A drag on the handle started/moved/ended. `deltaY` in updateDrag is relative to the drag's own
     *  start point, in this row's parent's coordinate space -- what the owner needs to find the
     *  target slot among the checked rows. Only fired while checked() (the row hides its handle
     *  otherwise, so the mouse events driving these never originate on an unchecked row). */
    std::function<void()> onDragStarted;
    std::function<void(int deltaY)> onDragUpdated;
    std::function<void()> onDragEnded;

    // ---- Test seams: drive the real controls, the MacroPortConfigDialog idiom -------------------
    // Sets the checkbox's visible state directly and calls onToggled itself, rather than relying on
    // juce::Button::setToggleState's own notification semantics to reach checkbox_.onClick.
    void triggerToggleForTest() {
        const bool newState = !checkbox_.getToggleState();
        checkbox_.setToggleState(newState, juce::dontSendNotification);
        if (onToggled)
            onToggled(newState);
    }
    void setLabelTextForTest(const juce::String& text) { labelEditor_.setText(text, false); }
    void commitLabelForTest() { commitLabel(); }

    static constexpr int kRowHeight = 26;

private:
    void commitLabel();

    juce::String paramId_;
    juce::ToggleButton checkbox_;
    juce::Label nameLabel_;
    juce::TextEditor labelEditor_;
    juce::Rectangle<int> dragHandleBounds_; // hit area for the checked-only drag handle; painted in paint()

    bool dragging_ = false;
    juce::Point<int> dragStartScreenPos_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginKnobPickerRow)
};

} // namespace synth::ui
