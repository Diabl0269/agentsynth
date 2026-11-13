#include "ModCurveWindow.h"

#include "ModCurveChip.h"
#include "UI/Graph/ModDot/ModDotPalette.h"
#include "UI/Layout/AppShortcutRelay.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/ModuleViews/CurveEditor/CurveGridNoteLength.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kWindowPad = 12;
constexpr int kWindowRowH = 26;
constexpr int kHintH = 32;
constexpr int kGridLabelW = 34;
constexpr int kGridW = 70;
constexpr int kBottomGap = 8;
constexpr int kChipGap = 6;
constexpr int kLabelGap = 6; // the design system's space unit, a caption to its control
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
    return "Double-click to add a point; double-click a point to remove it. Drag a point to move it, or right-click it "
           "for more. Drag the ring on a sloping segment to bend it. Snap rounds points to the grid; hold Shift to "
           "place them freely.";
}

int ModCurveWindowContent::gridDivisions(const juce::String& itemText) {
    return itemText.fromLastOccurrenceOf("/", false, false).getIntValue(); // "1/8" and an old "Grid 1/8" alike
}

ModCurveWindowContent::ModCurveWindowContent(std::shared_ptr<ModCurveEditTarget> target)
    : target_(std::move(target))
    , view_(target_)
    , builtIns_(ModCurvePresetStore::builtIns()) {
    setTitle(target_->title());
    setComponentID("modCurveWindow");
    addAndMakeVisible(view_);
    view_.editor().setComponentID("modCurveWindowEditor");
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
    snapButton_.setComponentID("modCurveSnap");
    snapButton_.setLayout(ToolbarButtonLayout::Inline);
    snapButton_.setLabel("Snap", true);
    snapButton_.setClickingTogglesState(true);
    snapButton_.setWantsKeyboardFocus(true);
    snapButton_.setTooltip(SnapToggleButton::tooltipLead() +
                           ": points land on the grid lines (hold Shift to place a point freely)");
    snapButton_.onClick = [this] { applyGrid(); };
    gridLabel_.setText("Grid", juce::dontSendNotification);
    gridLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    gridLabel_.setJustificationType(juce::Justification::centredRight);
    gridLabel_.setInterceptsMouseClicks(false, false);
    gridLabel_.setAccessible(false); // the box carries its own name
    gridLabel_.setWantsKeyboardFocus(false);
    gridBox_.setComponentID("modCurveGrid");
    gridBox_.setTitle("Curve grid");
    gridBox_.setTooltip("Grid: how many cells the input and output range are cut into, for the grid lines and Snap");
    gridBox_.setChoices({"1/2", "1/4", "1/8", "1/16", "1/32"});
    gridBox_.setKindUnavailableTip(synth::NoteKind::triplet, "No triplet grid: the cells are straight divisions");
    gridBox_.setKindUnavailableTip(synth::NoteKind::dotted, "No dotted grid: the cells are straight divisions");
    gridBox_.setCurrentName("1/8", juce::dontSendNotification);
    gridBox_.onChange = [this] { applyGrid(); };
    addAndMakeVisible(resetButton_);
    addAndMakeVisible(doneButton_);
    addAndMakeVisible(snapButton_);
    lookAndFeelChanged();
    addAndMakeVisible(gridLabel_);
    addAndMakeVisible(gridBox_);
    applyGrid();
    view_.onTargetGone = [this] { close(); };
    changeHandle_ = target_->watchChanges([this] { syncChips(); });
    restoreHandle_ = target_->watchRestore([this] { syncChips(); });
    syncChips();
    setSize(520, 440);
}

void ModCurveWindowContent::applyGrid() {
    auto& editor = view_.editor();
    editor.setGrid(curvegrid::gridForDivisions(gridDivisions(gridBox_.getCurrentName())));
    editor.setSnapToGrid(snapButton_.getToggleState());
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
    g.drawFittedText(hintText(), kWindowPad, getHeight() - kWindowPad - kWindowRowH - 6 - kHintH,
                     getWidth() - 2 * kWindowPad, kHintH, juce::Justification::centredLeft, 2);
}

void ModCurveWindowContent::lookAndFeelChanged() {
    gridLabel_.setColour(juce::Label::textColourId, synth::theme::themeOf(*this).colors.textMuted);
}

void ModCurveWindowContent::resized() {
    auto area = getLocalBounds().reduced(kWindowPad);
    auto bottom = area.removeFromBottom(kWindowRowH);
    area.removeFromBottom(6 + kHintH);
    auto top = area.removeFromTop(kWindowRowH);
    area.removeFromTop(8);
    view_.setBounds(area);
    const int chipW = 80;
    for (size_t i = 0; i < chips_.size(); ++i) {
        chips_[i]->setBounds(top.removeFromLeft(chipW));
        top.removeFromLeft(kChipGap);
    }
    doneButton_.setBounds(bottom.removeFromRight(90));
    bottom.removeFromRight(8);
    resetButton_.setBounds(bottom.removeFromRight(90));
    snapButton_.setBounds(bottom.removeFromLeft(snapButton_.getPreferredWidth()));
    bottom.removeFromLeft(kBottomGap * 2);
    gridLabel_.setBounds(bottom.removeFromLeft(kGridLabelW));
    bottom.removeFromLeft(kLabelGap);
    gridBox_.setBounds(bottom.removeFromLeft(kGridW));
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
    dialog->setResizeLimits(ModCurveWindowContent::kMinWidth, ModCurveWindowContent::kMinHeight, 4000, 4000);
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
