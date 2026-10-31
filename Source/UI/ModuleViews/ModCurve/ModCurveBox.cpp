#include "ModCurveBox.h"

#include "ModCurveChip.h"
#include "UI/Graph/CardLayoutEditor/PresetNamePrompt.h"
#include "UI/Graph/ModDot/ModDotPalette.h"

namespace synth::ui {

namespace {
constexpr int kPad = 8;
constexpr int kTitleH = 18;
constexpr int kReadoutH = 14;
constexpr int kEditorSize = 190;
constexpr int kStripW = 38;
constexpr int kRowH = 22;
} // namespace

ModCurveBox::ModCurveBox(std::shared_ptr<ModCurveEditTarget> target, ModCurvePresetStore store)
    : target_(std::move(target))
    , store_(std::move(store))
    , view_(target_)
    , builtIns_(ModCurvePresetStore::builtIns()) {
    setTitle("Curve");
    setComponentID("modCurveBox");
    title_ = target_->title();
    addAndMakeVisible(view_);
    addAndMakeVisible(minHandle_);
    addAndMakeVisible(maxHandle_);
    view_.setComponentID("modCurveEditor");

    for (const auto& entry : builtIns_) {
        auto chip = std::make_unique<ModCurvePresetButton>(entry.name);
        chip->setTitle(entry.name + " curve");
        chip->setTooltip("Use the " + entry.name + " curve");
        chip->setClickingTogglesState(false);
        chip->onClick = [this, entry] { applyPreset(entry); };
        addAndMakeVisible(*chip);
        chips_.push_back(std::move(chip));
    }
    savedButton_.setTitle("Saved curves");
    savedButton_.setTooltip("Your saved curves");
    savedButton_.onClick = [this] { showSavedMenu(); };
    saveButton_.setTitle("Save curve as preset...");
    saveButton_.setTooltip("Save curve as preset...");
    saveButton_.onClick = [this] { saveCurrentAsPreset(); };
    enlargeButton_.setTitle("Enlarge curve editor");
    enlargeButton_.setTooltip("Open a bigger curve editor");
    enlargeButton_.onClick = [this] {
        if (onEnlarge)
            onEnlarge();
    };
    for (auto* b : {&savedButton_, &saveButton_, &enlargeButton_})
        addAndMakeVisible(*b);

    for (const bool isMax : {false, true}) {
        auto& handle = isMax ? maxHandle_ : minHandle_;
        handle.onBegin = [this] { beginRangeEdit(); };
        handle.onChange = [this, isMax](float v) { changeRange(isMax, v); };
        handle.onEnd = [this] { target_->commitGesture(); };
    }

    view_.onTargetGone = [this] {
        if (onClose)
            onClose();
    };
    changeHandle_ = target_->watchChanges([this] { refresh(); });
    restoreHandle_ = target_->watchRestore([this] { refresh(); });
    refresh();
    setSize(kWidth, kHeight);
}

void ModCurveBox::refresh() {
    const auto curve = target_->read();
    if (!curve)
        return;
    minHandle_.setValue(curve->rangeMin);
    maxHandle_.setValue(curve->rangeMax);
    readout_ = "Range " + juce::String(juce::roundToInt(curve->rangeMin * 100.0f)) + "% to " +
               juce::String(juce::roundToInt(curve->rangeMax * 100.0f)) + "%";
    for (size_t i = 0; i < chips_.size(); ++i)
        chips_[i]->setToggleState(builtIns_[i].curve == *curve, juce::dontSendNotification);
    repaint();
}

void ModCurveBox::applyPreset(const ModCurvePresetStore::Entry& entry) { target_->setOnce(entry.curve); }

bool ModCurveBox::applySavedCurve(const juce::String& name) {
    for (const auto& e : store_.userPresets())
        if (e.name == name) {
            target_->setOnce(e.curve);
            return true;
        }
    return false;
}

void ModCurveBox::beginRangeEdit() { target_->beginGesture(); }

void ModCurveBox::changeRange(bool isMax, float value) {
    auto curve = target_->read();
    if (!curve)
        return;
    // The handles never cross: a crossed range would be swapped when loaded, so it would not sound as it was saved.
    if (isMax)
        curve->rangeMax = juce::jmax(value, curve->rangeMin);
    else
        curve->rangeMin = juce::jmin(value, curve->rangeMax);
    target_->apply(*curve);
    refresh(); // a stopped handle goes back to where the range really is, even when nothing changed
}

void ModCurveBox::showSavedMenu() {
    juce::PopupMenu menu;
    const auto saved = store_.userPresets();
    if (saved.empty())
        menu.addItem(1, "No saved curves yet", false);
    for (size_t i = 0; i < saved.size(); ++i)
        menu.addItem((int)i + 2, saved[i].name);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&savedButton_),
                       [this, safe = juce::Component::SafePointer<ModCurveBox>(this), saved](int result) {
                           if (safe != nullptr && result >= 2)
                               applySavedCurve(saved[(size_t)result - 2].name);
                       });
}

void ModCurveBox::saveCurrentAsPreset() {
    const auto curve = target_->read();
    if (!curve)
        return;
    auto onName = [this, safe = juce::Component::SafePointer<ModCurveBox>(this),
                   curve = *curve](const juce::String& name) {
        if (safe != nullptr)
            store_.save(name, curve);
    };
    if (namePrompt)
        namePrompt(std::move(onName));
    else
        promptForPresetName(std::move(onName));
}

void ModCurveBox::focusFirstControl() { chips_.front()->grabKeyboardFocus(); }

void ModCurveBox::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    g.setColour(p.text);
    g.setFont(p.mono(12.0f, juce::Font::bold));
    g.drawText(title_, kPad, kPad, kWidth - 2 * kPad, kTitleH, juce::Justification::centredLeft, true);
    g.setColour(p.muted);
    g.setFont(p.mono(11.0f));
    const int readoutY = kPad + kTitleH;
    g.drawText(readout_, kPad + kStripW + 2, readoutY, kEditorSize, kReadoutH, juce::Justification::centredLeft, true);
    // Each range track is named right above itself.
    g.setFont(p.mono(9.0f));
    g.drawText("min", minHandle_.getX() - 1, readoutY, 18, kReadoutH, juce::Justification::centred, false);
    g.drawText("max", maxHandle_.getX() - 1, readoutY, 18, kReadoutH, juce::Justification::centred, false);
}

void ModCurveBox::resized() {
    const int editorY = kPad + kTitleH + kReadoutH + 4;
    minHandle_.setBounds(kPad, editorY, 16, kEditorSize);
    maxHandle_.setBounds(kPad + 18, editorY, 16, kEditorSize);
    view_.setBounds(kPad + kStripW + 2, editorY, kEditorSize, kEditorSize);
    const int chipY = editorY + kEditorSize + 8;
    const int gap = 4;
    const int chipW = (kWidth - 2 * kPad - 3 * gap) / 4;
    for (size_t i = 0; i < chips_.size(); ++i)
        chips_[i]->setBounds(kPad + (int)i * (chipW + gap), chipY, chipW, kRowH);
    const int rowY = chipY + kRowH + 6;
    savedButton_.setBounds(kPad, rowY, 64, kRowH);
    saveButton_.setBounds(kPad + 68, rowY, 70, kRowH);
    enlargeButton_.setBounds(kPad + 142, rowY, kWidth - 2 * kPad - 142, kRowH);
}

bool ModCurveBox::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && onClose) {
        onClose();
        return true;
    }
    return false;
}

} // namespace synth::ui
