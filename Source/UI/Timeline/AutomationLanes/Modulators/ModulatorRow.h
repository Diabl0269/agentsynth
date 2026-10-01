#pragma once

#include "UI/Timeline/AutomationLanes/Modulators/ModulatorInfo.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

struct TrackHeaderHost;

// The header-column half of one modulator row under an automation lane: a tag in the mod-wire
// colour, the source card's title, and -- for an LFO -- its shape, rate, sync and the routing's depth
// plus a menu (show on canvas, remove). Any other source is read-only apart from its depth.
//
// Holds no parameter state: every control mirrors the live processor through the host
// (refreshValues), and every edit goes back through TrackHeaderHost::setNodeParameter, the same undo
// path the canvas knobs use. Message thread only. "Remove modulator" destroys this row before the
// call returns (the owner prunes it on the graph-changed refresh).
class ModulatorRow : public juce::Component {
public:
    // Three lines (title; shape and rate; sync and depth) at 100% row zoom, scaled like a lane row. The
    // header column is ~190 px, too narrow for the app's combos and bars side by side on two lines.
    static constexpr int kBaseHeight = 54;
    static constexpr int kIndent = 28; // one step further in than a lane header
    static constexpr int kShowOnCanvasMenuId = 1;
    static constexpr int kRemoveMenuId = 2;

    /** `host` may be null (every control then stays inert). `parameterName` words the controls' names. */
    ModulatorRow(const ModulatorInfo& info, TrackHeaderHost* host, const juce::String& parameterName);

    const ModulatorInfo& getInfo() const noexcept { return info_; }
    /** Same routing (same key()), possibly re-titled or re-coloured. */
    void setInfo(const ModulatorInfo& info, const juce::String& parameterName);
    /** Re-reads every control from the live processor; a control mid-drag is left alone. */
    void refreshValues();

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    juce::ComboBox& getShapeCombo() noexcept { return shape_; }
    juce::ComboBox& getSyncRateCombo() noexcept { return syncRate_; }
    juce::Slider& getRateSlider() noexcept { return rateHz_; }
    juce::ToggleButton& getSyncToggle() noexcept { return sync_; }
    juce::Slider& getDepthSlider() noexcept { return depth_; }
    juce::Button& getMenuButton() noexcept { return menuButton_; }

    /** The menu as the button shows it; ids are the k*MenuId constants. */
    juce::PopupMenu buildMenu() const;
    /** Applies a menu choice; may destroy this row (see the class comment). */
    void applyMenuChoice(int menuId);

private:
    class MenuButton : public juce::Button {
    public:
        MenuButton();
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    };

    void initLfoControls();
    void initDepthControl();
    void applyNames();
    void showMenu();
    void edit(const juce::String& uuid, const juce::String& paramId, float value, ParameterEditPhase phase);
    void wireDragEdits(juce::Slider& slider, const juce::String& uuid, const juce::String& paramId,
                       std::function<float(double)> toParameter);
    void layoutLfoControls(juce::Rectangle<int> shapeLine, juce::Rectangle<int> depthLine);
    void layoutDepth(juce::Rectangle<int> line);

    ModulatorInfo info_;
    TrackHeaderHost* host_ = nullptr;
    juce::String parameterName_;

    juce::ComboBox shape_;
    juce::ComboBox syncRate_;
    juce::Slider rateHz_;
    juce::ToggleButton sync_;
    juce::Slider depth_;
    MenuButton menuButton_;
    juce::Slider* dragging_ = nullptr; // the slider mid-gesture, which refreshValues() leaves alone
    juce::Rectangle<int> tagArea_;
    juce::Rectangle<int> titleArea_;
    juce::Rectangle<int> rateTextArea_;  // the Hz value, beside its bar (never over it: the bar has a cap)
    juce::Rectangle<int> depthTextArea_; // the depth percentage, beside its bar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorRow)
};

} // namespace synth::ui
