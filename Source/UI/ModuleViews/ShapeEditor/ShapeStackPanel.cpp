// ShapeStackPanel.cpp -- the Stack harmonics popover's controls and layout.
#include "ShapeStackPanel.h"
#include "PitchGrid.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kStackPad = 10;
constexpr int kStackGap = 8;
constexpr int kStackRowHeight = 24;
constexpr int kStackLabelWidth = 96;
constexpr int kStackButtonHeight = 28; // the footer buttons: a little taller than the rows, wide enough to read
constexpr int kStackButtonWidth = 80;
constexpr int kStackPillHeight = 18; // segment text is height - 6: 12 pt, the size of the grid rows' labels
constexpr int kStackFirstNote = 24;  // C1
constexpr int kStackLastNote = 95;   // B6
constexpr int kStackMinCount = 2;
constexpr int kStackMaxCount = 16;
constexpr float kStackSteps[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f};

int stackNoteId(int midi) { return 1000 + midi; }

void styleStackLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(theme::AppLookAndFeel::uiFont(11.0f));
    label.setInterceptsMouseClicks(false, false);
    label.setAccessible(false); // each control carries its own name
}
} // namespace

ShapeStackPanel::ShapeStackPanel(const ringhelpers::StackOptions& initial)
    : width_("Width", juce::StringArray{"Narrow", "Medium", "Wide"}) {
    setTitle("Stack harmonics panel");
    title_.setInterceptsMouseClicks(false, false);
    title_.setAccessible(false);
    title_.setFont(theme::AppLookAndFeel::uiSemiBoldFont(13.0f));
    styleStackLabel(rootLabel_, "Root");
    styleStackLabel(countLabel_, "Harmonics");
    styleStackLabel(stepLabel_, "Each quieter by");
    styleStackLabel(widthLabel_, "Width");

    for (int m = kStackFirstNote; m <= kStackLastNote; ++m)
        root_.addItem(pitchgrid::noteName(m) + " (" + pitchgrid::formatHz(pitchgrid::noteHz(m)) + ")", stackNoteId(m));
    root_.setSelectedId(
        stackNoteId(juce::jlimit(kStackFirstNote, kStackLastNote, (int)std::lround(pitchgrid::midiOf(initial.rootHz)))),
        juce::dontSendNotification);
    root_.setTitle("Root");
    root_.setTooltip("Root: the note whose harmonics get a peak each");

    for (int n = kStackMinCount; n <= kStackMaxCount; ++n)
        count_.addItem(juce::String(n), n);
    count_.setSelectedId(juce::jlimit(kStackMinCount, kStackMaxCount, initial.count), juce::dontSendNotification);
    count_.setTitle("Harmonics");
    count_.setTooltip("Harmonics: how many peaks, the root and its multiples. Peaks above the filter's range are left "
                      "out");

    int stepId = 1;
    int chosen = 3;
    for (size_t i = 0; i < std::size(kStackSteps); ++i) {
        step_.addItem(juce::String(kStackSteps[i]) + " dB", stepId + (int)i);
        if (std::abs(kStackSteps[i] - initial.stepDb) < 1.0e-3f)
            chosen = stepId + (int)i;
    }
    step_.setSelectedId(chosen, juce::dontSendNotification);
    step_.setTitle("Each quieter by");
    step_.setTooltip("Each quieter by: how much lower every harmonic sits than the one before (0 dB: all equal)");

    int widthIndex = 1;
    for (size_t i = 0; i < std::size(ringhelpers::kStackWidthOctaves); ++i)
        if (std::abs(ringhelpers::kStackWidthOctaves[i] - initial.halfWidthOctaves) < 1.0e-6)
            widthIndex = (int)i;
    width_.setSelectedIndex(widthIndex, juce::dontSendNotification);
    width_.setTooltip("Width: how wide each peak is (Narrow rings the most selectively)");

    cancel_.setTitle("Cancel");
    cancel_.setTooltip("Cancel: close without changing the shape (Esc)");
    cancel_.onClick = [this] { closeHostingWindow(*this); };
    stack_.setTitle("Stack");
    stack_.setTooltip("Stack: replace the selected shape's curve with these peaks. One undo step (Return)");
    stack_.onClick = [this] {
        if (onStack)
            onStack(getOptions());
        closeHostingWindow(*this);
    };

    int order = 1;
    for (juce::Component* c :
         std::initializer_list<juce::Component*>{&root_, &count_, &step_, &width_, &cancel_, &stack_}) {
        c->setExplicitFocusOrder(order++);
        c->setWantsKeyboardFocus(true);
        addAndMakeVisible(*c);
    }
    for (auto* l : {&title_, &rootLabel_, &countLabel_, &stepLabel_, &widthLabel_})
        addAndMakeVisible(*l);
    setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
    setSize(kWidth, kHeight);
    applyColours();
}

ShapeStackPanel::~ShapeStackPanel() = default;

ringhelpers::StackOptions ShapeStackPanel::getOptions() const {
    ringhelpers::StackOptions o;
    o.rootHz = pitchgrid::noteHz(root_.getSelectedId() - 1000);
    o.count = count_.getSelectedId();
    o.stepDb = kStackSteps[juce::jlimit(0, (int)std::size(kStackSteps) - 1, step_.getSelectedId() - 1)];
    o.halfWidthOctaves = ringhelpers::kStackWidthOctaves[juce::jlimit(0, 2, width_.getSelectedIndex())];
    return o;
}

void ShapeStackPanel::applyColours() {
    const auto& c = theme::themeOf(*this).colors;
    title_.setColour(juce::Label::textColourId, c.textPrimary);
    for (auto* l : {&rootLabel_, &countLabel_, &stepLabel_, &widthLabel_})
        l->setColour(juce::Label::textColourId, c.textMuted);
    stack_.setColour(juce::TextButton::buttonColourId, c.accent);
    stack_.setColour(juce::TextButton::buttonOnColourId, c.accent);
    stack_.setColour(juce::TextButton::textColourOffId, c.bg0);
    stack_.setColour(juce::TextButton::textColourOnId, c.bg0);
}

void ShapeStackPanel::lookAndFeelChanged() { applyColours(); }

void ShapeStackPanel::paint(juce::Graphics& g) { g.fillAll(theme::themeOf(*this).colors.surfaceHi); }

void ShapeStackPanel::resized() {
    auto area = getLocalBounds().reduced(kStackPad);
    title_.setBounds(area.removeFromTop(18));
    area.removeFromTop(kStackGap);
    const auto row = [&](juce::Label& label, juce::Component& control) {
        auto r = area.removeFromTop(kStackRowHeight);
        label.setBounds(r.removeFromLeft(kStackLabelWidth));
        // The segmented pills are as short as the toolbar's (their text scales with the height), centred in the row.
        control.setBounds(&control == &width_ ? r.withSizeKeepingCentre(r.getWidth(), kStackPillHeight) : r);
        area.removeFromTop(kStackGap);
    };
    row(rootLabel_, root_);
    row(countLabel_, count_);
    row(stepLabel_, step_);
    row(widthLabel_, width_);
    auto buttons = area.removeFromTop(kStackButtonHeight);
    stack_.setBounds(buttons.removeFromRight(kStackButtonWidth));
    buttons.removeFromRight(kStackGap);
    cancel_.setBounds(buttons.removeFromRight(kStackButtonWidth));
}

bool ShapeStackPanel::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey)
        return closeHostingWindow(*this);
    if (key == juce::KeyPress::returnKey) {
        stack_.triggerClick();
        return true;
    }
    return false;
}

void ShapeStackPanel::parentHierarchyChanged() { reveal_.startIfInCallout(); }

} // namespace synth::ui
