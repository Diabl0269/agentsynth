#pragma once

// The enlarged curve editor: a resizable window titled "Curve: <source> to <destination>" with the big editor, the
// preset chips, a hint, Reset and Done. It edits the same curve as the small box.

#include "ModCurveEditorView.h"
#include "ModCurvePresetStore.h"
#include <functional>
#include <memory>

namespace synth::ui {

class ModCurveWindowContent final : public juce::Component {
public:
    explicit ModCurveWindowContent(std::shared_ptr<ModCurveEditTarget> target);

    ModCurveEditorView& view() noexcept { return view_; }
    juce::TextButton& presetChip(int index) { return *chips_.at((size_t)index); }
    juce::TextButton& resetButton() noexcept { return resetButton_; }
    juce::TextButton& doneButton() noexcept { return doneButton_; }
    juce::String windowTitle() const { return target_->title(); }
    static juce::String hintText();
    void focusFirstControl();

    /** Runs for Done, Escape, or a vanished attenuverter; the default closes the hosting window. */
    std::function<void()> onClose;
    /** Offered every key the window does not use, so the app's shortcuts still work from it. */
    std::function<bool(const juce::KeyPress&)> onAppShortcut;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void close();
    void syncChips();
    std::shared_ptr<ModCurveEditTarget> target_;
    ModCurveEditorView view_;
    std::vector<ModCurvePresetStore::Entry> builtIns_;
    std::vector<std::unique_ptr<juce::TextButton>> chips_;
    juce::TextButton resetButton_{"Reset"};
    juce::TextButton doneButton_{"Done"};
    std::shared_ptr<void> changeHandle_;
    std::shared_ptr<void> restoreHandle_;
};

namespace test_hooks {
/** When set, openModCurveWindow hands its content here instead of opening a window. */
std::function<void(std::unique_ptr<ModCurveWindowContent>)>& modCurveWindowHookForTest();
} // namespace test_hooks

/** Opens the enlarged editor, growing out of `anchor`. Null when a test hook took the content. */
juce::DialogWindow* openModCurveWindow(std::shared_ptr<ModCurveEditTarget> target, juce::Component* anchor);

} // namespace synth::ui
