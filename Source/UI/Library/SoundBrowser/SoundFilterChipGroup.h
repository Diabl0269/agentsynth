#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/** A wrapped row of toggle chips that is ONE Tab stop: Left and Right move across the chips, Home and End jump, Space
 *  or Return turns the focused chip on or off. A screen reader hears the focused chip ("Drums filter, on"). */
class SoundFilterChipGroup
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kChipHeight = 24;
    static constexpr int kGap = 6;

    struct Item {
        juce::String id;
        juce::String label;
        bool on = false;
    };

    /** `groupName` names the group to a screen reader ("Kind"); `tooltipNoun` completes "Show only ..." in tooltips. */
    SoundFilterChipGroup(const juce::String& groupName, const juce::String& tooltipNoun);

    void setItems(std::vector<Item> items);
    void setOn(const juce::String& id, bool on);
    bool isOn(const juce::String& id) const;
    /** Called when the user turns a chip on or off, never by setOn. */
    std::function<void(const juce::String& id, bool on)> onToggled;

    /** The height the chips need when laid out in `width`. */
    int heightForWidth(int width) const;
    int getFocusedIndex() const { return focused_; }
    void setFocusedIndex(int index);
    juce::Rectangle<int> getChipBounds(int index) const;
    int getNumItems() const { return (int)items_.size(); }
    juce::String getAccessibilityValueText() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    int chipWidth(const Item& item) const;
    std::vector<juce::Rectangle<int>> layoutChips(int width) const;
    int indexAt(juce::Point<int> p) const;
    void toggle(int index);
    juce::String spokenChip(int index) const;

    juce::String groupName_, noun_;
    std::vector<Item> items_;
    std::vector<juce::Rectangle<int>> bounds_;
    int focused_ = 0;
    int hovered_ = -1;
};

} // namespace synth::ui
