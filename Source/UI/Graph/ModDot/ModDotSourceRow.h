#pragma once

// One source of the mod dot's sources page: swatch, name, amount bar, signed amount (click to type), "show in
// timeline" and remove. Pure view: it reports what the user did and the page applies it to the graph.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "KnobModSources.h"
#include "ModDotAmountBar.h"
#include "ModDotGlyphButton.h"
#include "ModDotMotion.h"
#include "UI/Layout/NavigationSearchField.h"
#include <functional>
#include <memory>

namespace synth::ui {

class ModDotSourceRow final : public juce::Component {
public:
    static constexpr int kHeight = 34;

    ModDotSourceRow(const KnobModSource& source, const juce::String& paramName);
    ~ModDotSourceRow() override;

    juce::AudioProcessorGraph::NodeID attenuverterId() const noexcept { return source_.attenuverterId; }
    const KnobModSource& source() const noexcept { return source_; }
    /** Re-reads name and amount from the graph; a bar mid-drag is left alone. */
    void update(const KnobModSource& source);

    void setSelected(bool selected);
    bool isSelected() const noexcept { return selected_; }

    // What the user did.
    std::function<void(ModDotSourceRow&)> onSelect;
    std::function<void(ModDotSourceRow&)> onGestureBegin;
    std::function<void(ModDotSourceRow&, float amount)> onAmountDragged; // live, between begin and end
    std::function<void(ModDotSourceRow&)> onGestureEnd;
    std::function<void(ModDotSourceRow&, float amount)> onAmountTyped; // one undo step
    std::function<void(ModDotSourceRow&)> onShowInTimeline;
    std::function<void(ModDotSourceRow&)> onRemove;
    /** Up/Down on any control of the row: the page moves focus to the neighbouring row. */
    std::function<void(ModDotSourceRow&, int step)> onNavigate;

    ModDotAmountBar& bar() noexcept { return bar_; }
    juce::Button& amountButton() noexcept;
    juce::Button& timelineButton() noexcept { return timelineButton_; }
    juce::Button& removeButton() noexcept { return removeButton_; }
    bool isEditingAmount() const noexcept { return editor_ != nullptr; }
    juce::TextEditor* amountEditor() noexcept { return editor_.get(); }
    juce::Colour swatchColour() const;

    void beginAmountEdit();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class AmountButton;
    void endAmountEdit(bool apply);
    void applyNames();

    KnobModSource source_;
    juce::String paramName_;
    bool selected_ = false;
    ModDotHoverFade hover_;
    ModDotAmountBar bar_;
    std::unique_ptr<AmountButton> amountButton_;
    ModDotGlyphButton timelineButton_;
    ModDotGlyphButton removeButton_;
    std::unique_ptr<NavigationSearchField> editor_;
    std::unique_ptr<NavigationSearchField> retired_;
    juce::Rectangle<int> nameArea_;
    juce::Rectangle<int> swatchArea_;
};

} // namespace synth::ui
