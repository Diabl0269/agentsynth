#include "ModCurveWindow.h"

#include "ModCurveChip.h"
#include "UI/Graph/ModDot/ModDotPalette.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorAppShortcuts.h"

namespace synth::ui {

namespace {
constexpr int kPad = 12;
constexpr int kRowH = 26;
constexpr int kHintH = 18;
constexpr float kInStartScale = 0.82f;
constexpr float kOutEndScale = 0.9f;
constexpr double kInMs = 220.0;
constexpr double kOutMs = 150.0;

popup_motion::Style motionStyle(juce::Component::SafePointer<juce::Component> anchor,
                                juce::Component::SafePointer<juce::DialogWindow> window) {
    popup_motion::Style style;
    style.inSlidePx = 0.0f;
    style.outSlidePx = 0.0f;
    style.overshoot = true;
    style.inMs = kInMs;
    style.outMs = kOutMs;
    style.alphaInFraction = 0.6f;
    style.inStartScale = kInStartScale;
    style.outEndScale = kOutEndScale;
    const auto centre = [anchor, window]() -> juce::Point<int> {
        if (anchor != nullptr)
            return anchor->localAreaToGlobal(anchor->getLocalBounds()).getCentre();
        return window != nullptr ? window->getScreenBounds().getCentre() : juce::Point<int>();
    };
    style.anchor = centre;
    style.body = [window]() -> juce::Component* { return window != nullptr ? window->getContentComponent() : nullptr; };
    style.bodyPivot = [centre, window]() -> juce::Point<float> {
        if (window == nullptr)
            return {};
        return (centre() - window->getContentComponent()->getScreenPosition()).toFloat();
    };
    return style;
}
} // namespace

juce::String ModCurveWindowContent::hintText() {
    return "Click the line to add a point. Drag to move. Right-click to remove.";
}

ModCurveWindowContent::ModCurveWindowContent(std::shared_ptr<ModCurveEditTarget> target)
    : target_(std::move(target))
    , view_(target_)
    , builtIns_(ModCurvePresetStore::builtIns()) {
    setTitle(target_->title());
    setComponentID("modCurveWindow");
    addAndMakeVisible(view_);
    for (const auto& entry : builtIns_) {
        auto chip = std::make_unique<ModCurvePresetButton>(entry.name);
        chip->setTitle(entry.name + " curve");
        chip->setTooltip("Use the " + entry.name + " curve");
        chip->onClick = [this, entry] { target_->setOnce(entry.curve); };
        addAndMakeVisible(*chip);
        chips_.push_back(std::move(chip));
    }
    resetButton_.setTitle("Reset curve");
    resetButton_.setTooltip("Back to the straight line and the full range");
    resetButton_.onClick = [this] { target_->setOnce(synth::ModCurve::linear()); };
    doneButton_.setTitle("Done");
    doneButton_.setTooltip("Close the curve editor");
    doneButton_.onClick = [this] { close(); };
    addAndMakeVisible(resetButton_);
    addAndMakeVisible(doneButton_);
    view_.onTargetGone = [this] { close(); };
    changeHandle_ = target_->watchChanges([this] { syncChips(); });
    restoreHandle_ = target_->watchRestore([this] { syncChips(); });
    syncChips();
    setSize(520, 440);
}

void ModCurveWindowContent::syncChips() {
    const auto curve = target_->read();
    if (!curve)
        return;
    for (size_t i = 0; i < chips_.size(); ++i)
        chips_[i]->setToggleState(builtIns_[i].curve == *curve, juce::dontSendNotification);
}

void ModCurveWindowContent::close() {
    if (onClose)
        onClose();
    else
        closeHostingWindow(*this);
}

void ModCurveWindowContent::focusFirstControl() { chips_.front()->grabKeyboardFocus(); }

void ModCurveWindowContent::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    g.fillAll(p.panel);
    g.setColour(p.muted);
    g.setFont(p.mono(12.0f));
    g.drawText(hintText(), kPad, getHeight() - kPad - kRowH - 6 - kHintH, getWidth() - 2 * kPad, kHintH,
               juce::Justification::centredLeft, true);
}

void ModCurveWindowContent::resized() {
    auto area = getLocalBounds().reduced(kPad);
    auto bottom = area.removeFromBottom(kRowH);
    area.removeFromBottom(6 + kHintH);
    auto top = area.removeFromTop(kRowH);
    area.removeFromTop(8);
    view_.setBounds(area);
    const int chipW = 80;
    for (size_t i = 0; i < chips_.size(); ++i)
        chips_[i]->setBounds(top.removeFromLeft(chipW));
    doneButton_.setBounds(bottom.removeFromRight(90));
    bottom.removeFromRight(8);
    resetButton_.setBounds(bottom.removeFromRight(90));
}

bool ModCurveWindowContent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        close();
        return true;
    }
    return onAppShortcut && onAppShortcut(key);
}

namespace test_hooks {
std::function<void(std::unique_ptr<ModCurveWindowContent>)>& modCurveWindowHookForTest() {
    static std::function<void(std::unique_ptr<ModCurveWindowContent>)> hook;
    return hook;
}
} // namespace test_hooks

juce::DialogWindow* openModCurveWindow(std::shared_ptr<ModCurveEditTarget> target, juce::Component* anchor) {
    auto content = std::make_unique<ModCurveWindowContent>(std::move(target));
    if (auto& hook = test_hooks::modCurveWindowHookForTest()) {
        hook(std::move(content));
        return nullptr;
    }
    content->onAppShortcut = makeAppShortcutRelay(anchor);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = content->windowTitle();
    options.content.setOwned(content.release());
    options.componentToCentreAround = anchor != nullptr ? anchor->getTopLevelComponent() : nullptr;
    options.useNativeTitleBar = true;
    options.resizable = true;
    auto* dialog = PopupMotion::launchDialog(
        options, [anchor](juce::DialogWindow& window) { return motionStyle(anchor, &window); });
    if (dialog == nullptr)
        return nullptr;
    juce::MessageManager::callAsync([window = juce::Component::SafePointer<juce::DialogWindow>(dialog)] {
        if (window == nullptr)
            return;
        window->toFront(true);
        if (auto* content = dynamic_cast<ModCurveWindowContent*>(window->getContentComponent()))
            content->focusFirstControl();
    });
    return dialog;
}

} // namespace synth::ui
