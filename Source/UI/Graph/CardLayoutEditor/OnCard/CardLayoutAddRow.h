#pragma once

// One control in the Add control panel: its name with the letters the search matched in the accent colour
// and semi-bold, a wash when it is the chosen row. A click picks it; a press that moves past a few pixels
// is a drag instead, reported with screen positions so the editor can place the control where it lands.
// The panel's search field keeps keyboard focus, so a row is a named button but not a Tab stop.
// docs/layout/module-card-layout.md#editing-a-layout.

#include "OnCardAddControlModel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class CardLayoutAddRow final : public juce::Button {
public:
    static constexpr int kHeight = 28;

    enum class DragPhase { Begin, Move, End };

    explicit CardLayoutAddRow(AddableControl control);

    const AddableControl& getControl() const noexcept { return control_; }
    void setQuery(const juce::String& query);
    void setSelected(bool selected);
    bool isSelected() const noexcept { return selected_; }

    /** The click, as the mouse or an assistive technology's press makes it. */
    std::function<void()> onPick;
    std::function<void(DragPhase, juce::Point<int> screenPosition)> onDrag;

    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    AddableControl control_;
    juce::String query_;
    bool selected_ = false;
    bool dragging_ = false;
    juce::Point<int> pressAt_; ///< Screen pixels, where the press landed.
};

} // namespace synth::ui
