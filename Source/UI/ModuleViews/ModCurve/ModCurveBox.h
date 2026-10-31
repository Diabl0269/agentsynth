#pragma once

// The small curve box: a title, the curve with its points and middle bend handle, two range handles on the left edge,
// preset chips (Linear, Exp, Log, S-curve), a menu of the user's saved curves, "Save curve as preset..." and Enlarge.
// It edits the attenuverter behind a ModCurveEditTarget, the same curve the enlarged window and every chip show.

#include "ModCurveEditorView.h"
#include "ModCurvePresetStore.h"
#include "ModCurveRangeHandle.h"
#include <array>
#include <functional>
#include <memory>

namespace synth::ui {

class ModCurveBox final : public juce::Component {
public:
    static constexpr int kWidth = 244;
    static constexpr int kHeight = 296;

    ModCurveBox(std::shared_ptr<ModCurveEditTarget> target, ModCurvePresetStore store);

    /** Runs when the user asks for the enlarged window. */
    std::function<void()> onEnlarge;
    /** Runs when the box should go away: Escape, or its attenuverter vanished. */
    std::function<void()> onClose;
    /** Asks for a preset name (a seam for tests); the default opens the app's name prompt. */
    std::function<void(std::function<void(const juce::String&)>)> namePrompt;

    ModCurveEditorView& view() noexcept { return view_; }
    ModCurveRangeHandle& minHandle() noexcept { return minHandle_; }
    ModCurveRangeHandle& maxHandle() noexcept { return maxHandle_; }
    juce::TextButton& presetChip(int index) { return *chips_.at((size_t)index); }
    juce::TextButton& savedButton() noexcept { return savedButton_; }
    juce::TextButton& saveButton() noexcept { return saveButton_; }
    juce::TextButton& enlargeButton() noexcept { return enlargeButton_; }
    /** The saved curves as the menu lists them, after the built-ins; a chip index selects a built-in. */
    std::vector<ModCurvePresetStore::Entry> savedCurves() const { return store_.userPresets(); }
    /** Applies a user preset by name as one undo step. False for an unknown name. */
    bool applySavedCurve(const juce::String& name);
    /** Re-reads the curve into the handles, chips and readout. */
    void refresh();
    /** Closes the box (onClose) when its attenuverter has vanished; cheap enough for a 30 Hz tick. */
    void tick() { view_.refreshFromTarget(); }
    /** Gives the keyboard to the first control. */
    void focusFirstControl();

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void applyPreset(const ModCurvePresetStore::Entry& entry);
    void beginRangeEdit();
    void changeRange(bool isMax, float value);
    void showSavedMenu();
    void saveCurrentAsPreset();

    std::shared_ptr<ModCurveEditTarget> target_;
    ModCurvePresetStore store_;
    ModCurveEditorView view_;
    ModCurveRangeHandle minHandle_{"Curve minimum"};
    ModCurveRangeHandle maxHandle_{"Curve maximum"};
    std::vector<ModCurvePresetStore::Entry> builtIns_;
    std::vector<std::unique_ptr<juce::TextButton>> chips_;
    juce::TextButton savedButton_{"Mine"};
    juce::TextButton saveButton_{"Save..."};
    juce::TextButton enlargeButton_{"Enlarge"};
    juce::String title_;
    juce::String readout_;
    std::shared_ptr<void> changeHandle_;
    std::shared_ptr<void> restoreHandle_;
};

} // namespace synth::ui
