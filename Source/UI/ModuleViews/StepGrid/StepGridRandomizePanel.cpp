// StepGridRandomizePanel.cpp -- the Randomize popover's controls and layout.
#include "StepGridRandomizePanel.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kPad = 10;
constexpr int kGap = 8;
constexpr int kLabelHeight = 14;
constexpr float kLabelFont = 10.5f;

} // namespace

StepGridRandomizePanel::StepGridRandomizePanel(const sequencer_ops::RandomizeSettings& initial,
                                               const MusicalScale& scale, int middleCOctave)
    : settings_(initial) {
    setTitle("Randomize in scale");
    for (auto* label : {&title_, &rangeLabel_, &densityLabel_, &densityValue_}) {
        label->setInterceptsMouseClicks(false, false);
        label->setWantsKeyboardFocus(false);
        addAndMakeVisible(*label);
    }
    title_.setFont(theme::AppLookAndFeel::uiSemiBoldFont(13.0f));
    for (auto* label : {&rangeLabel_, &densityLabel_, &densityValue_})
        label->setFont(theme::AppLookAndFeel::uiFont(kLabelFont));
    densityValue_.setJustificationType(juce::Justification::centredRight);

    range_.setMiddleCOctave(middleCOctave);
    range_.setScale(&scale);
    range_.setRange(settings_.minPitch, settings_.maxPitch);
    range_.onChange = [this](int lo, int hi) {
        settings_.minPitch = lo;
        settings_.maxPitch = hi;
        settingsChanged();
    };
    addAndMakeVisible(range_);

    density_.setRange(0.0, 100.0, 1.0);
    density_.setValue(std::round(settings_.density * 100.0f), juce::dontSendNotification);
    density_.setTitle("Steps that play");
    density_.setTooltip("How many steps play, as a share of the pattern. Ignored while Keep my rhythm is on");
    density_.onValueChange = [this] {
        settings_.density = (float)density_.getValue() / 100.0f;
        settingsChanged();
    };
    addAndMakeVisible(density_);

    keepRhythm_.setTitle("Keep my rhythm");
    keepRhythm_.setTooltip("Keep which steps play and their gate; only the notes change");
    keepRhythm_.setClickingTogglesState(true);
    keepRhythm_.setToggleState(settings_.keepRhythm, juce::dontSendNotification);
    setTogglePillStyle(keepRhythm_, true);
    keepRhythm_.onClick = [this] {
        settings_.keepRhythm = keepRhythm_.getToggleState();
        settingsChanged();
    };
    addAndMakeVisible(keepRhythm_);

    roll_.setTitle("Roll");
    roll_.setTooltip("Roll new notes. Each roll is one undo step, so you can roll again until you like it");
    roll_.onClick = [this] {
        if (onRoll)
            onRoll(settings_);
    };
    addAndMakeVisible(roll_);

    densityValue_.setText(juce::String(juce::roundToInt(settings_.density * 100.0f)) + "%", juce::dontSendNotification);
    setSize(kWidth, 206);
    applyColours();
}

StepGridRandomizePanel::~StepGridRandomizePanel() = default;

void StepGridRandomizePanel::settingsChanged() {
    densityValue_.setText(juce::String(juce::roundToInt(settings_.density * 100.0f)) + "%", juce::dontSendNotification);
    if (onSettingsChanged)
        onSettingsChanged(settings_);
}

void StepGridRandomizePanel::applyColours() {
    const auto& c = theme::themeOf(*this).colors;
    title_.setColour(juce::Label::textColourId, c.textPrimary);
    for (auto* label : {&rangeLabel_, &densityLabel_, &densityValue_})
        label->setColour(juce::Label::textColourId, c.textMuted);
    roll_.setColour(juce::TextButton::buttonColourId, c.accent);
    roll_.setColour(juce::TextButton::buttonOnColourId, c.accent);
    roll_.setColour(juce::TextButton::textColourOffId, c.bg0);
    roll_.setColour(juce::TextButton::textColourOnId, c.bg0);
}

void StepGridRandomizePanel::lookAndFeelChanged() { applyColours(); }

void StepGridRandomizePanel::paint(juce::Graphics& g) { g.fillAll(theme::themeOf(*this).colors.surfaceHi); }

void StepGridRandomizePanel::resized() {
    auto area = getLocalBounds().reduced(kPad);
    title_.setBounds(area.removeFromTop(18));
    area.removeFromTop(kGap);
    rangeLabel_.setBounds(area.removeFromTop(kLabelHeight));
    area.removeFromTop(4);
    range_.setBounds(area.removeFromTop(NoteRangePicker::kPreferredHeight));
    area.removeFromTop(kGap);
    auto densityRow = area.removeFromTop(kLabelHeight);
    densityValue_.setBounds(densityRow.removeFromRight(40));
    densityLabel_.setBounds(densityRow);
    density_.setBounds(area.removeFromTop(18));
    area.removeFromTop(kGap);
    const int pillWidth = theme::AppLookAndFeel::togglePillWidth(keepRhythm_.getButtonText());
    keepRhythm_.setBounds(area.removeFromTop(theme::AppLookAndFeel::kTogglePillHeight).removeFromLeft(pillWidth));
    area.removeFromTop(kGap);
    roll_.setBounds(area.removeFromTop(24).removeFromRight(70));
}

bool StepGridRandomizePanel::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey)
        return closeHostingWindow(*this);
    return false;
}

void StepGridRandomizePanel::parentHierarchyChanged() { reveal_.startIfInCallout(); }

} // namespace synth::ui
