// PerNoteBar.cpp -- the per-note settings row: wiring each control to the adapter as live, undoable edits, and
// the key row's fade.
#include "PerNoteBar.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
using Mode = synth::CustomFilterPerNote::Mode;

constexpr int kLabelWidth = 150;
constexpr int kModeWidth = 300;
constexpr int kEndLabelWidth = 48;
constexpr int kSliderWidth = 150;
constexpr int kOctaveButtonWidth = 28;
constexpr int kKeyStripWidth = 420;

juce::StringArray modeLabels() {
    juce::StringArray out;
    for (int i = 0; i < synth::CustomFilterPerNote::kNumModes; ++i)
        out.add(synth::CustomFilterPerNote::modeLabel(static_cast<Mode>(i)));
    return out;
}

/** "Hard" at 0, otherwise the glide time a new note takes to reach its shape. */
juce::String smoothnessText(double value) {
    if (value <= 0.0005)
        return "Hard";
    const double ms = value * synth::CustomFilterPerNote::kMaxGlideSeconds * 1000.0;
    return juce::String(juce::roundToInt(ms)) + " ms glide";
}
} // namespace

PerNoteBar::PerNoteBar(ShapeListAdapter& adapter)
    : adapter_(adapter)
    , mode_("Notes move the cursor", modeLabels()) {
    setTitle("Per-note settings");
    setWantsKeyboardFocus(false);

    mode_.setTooltip("Notes move the cursor: in Poly, each note can sit at its own place on the map, chosen by "
                     "hand, by pitch or at random (Off: every note shares the cursor)");
    mode_.setExplicitFocusOrder(1);
    mode_.onChange = [this](int index) {
        write([index](CustomFilterPerNote& p) { p.mode = static_cast<Mode>(index); }, true);
        showKeyRow(index == (int)Mode::ByHand);
    };
    addAndMakeVisible(mode_);

    smoothness_.setSliderStyle(juce::Slider::LinearHorizontal);
    smoothness_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    smoothness_.setRange(0.0, 1.0, 0.01); // an arrow key steps 5 ms of glide
    smoothness_.textFromValueFunction = smoothnessText;
    smoothness_.setTitle("Smoothness");
    smoothness_.setWantsKeyboardFocus(true);
    smoothness_.setTooltip("Smoothness: how a new note reaches its shape, from a hard jump to a 500 ms glide");
    smoothness_.setExplicitFocusOrder(2);
    smoothness_.onDragStart = [this] {
        dragging_ = true;
        adapter_.beginEdit();
    };
    smoothness_.onValueChange = [this] {
        const float value = (float)smoothness_.getValue();
        write([value](CustomFilterPerNote& p) { p.smoothness = value; }, !dragging_);
    };
    smoothness_.onDragEnd = [this] {
        dragging_ = false;
        adapter_.endEdit();
        if (onEdited)
            onEdited();
    };
    addAndMakeVisible(smoothness_);

    octaveDown_.setTitle("Octave down");
    octaveDown_.setTooltip("Octave down: show the keys an octave lower (Page Down on the keys)");
    octaveDown_.setExplicitFocusOrder(3);
    octaveDown_.onClick = [this] { keys_.shiftOctave(-1); };
    keys_.setExplicitFocusOrder(4);
    keys_.onKeyChanged = [this](int note, int shape) {
        write([note, shape](CustomFilterPerNote& p) { p.keys[(size_t)note] = (int8_t)shape; }, true);
    };
    octaveUp_.setTitle("Octave up");
    octaveUp_.setTooltip("Octave up: show the keys an octave higher (Page Up on the keys)");
    octaveUp_.setExplicitFocusOrder(5);
    octaveUp_.onClick = [this] { keys_.shiftOctave(1); };
    for (juce::Component* c : {(juce::Component*)&octaveDown_, (juce::Component*)&keys_, (juce::Component*)&octaveUp_})
        addChildComponent(c);
    keyRowFade_ =
        std::make_unique<FadeVisibility>(std::initializer_list<juce::Component*>{&octaveDown_, &keys_, &octaveUp_});
}

PerNoteBar::~PerNoteBar() = default;

/** A key-row change from a refresh (undo, a preset) lands at once on the first layout and fades after it. */
void PerNoteBar::refresh(const std::vector<ShapeItem>& shapes) {
    const auto p = adapter_.getPerNote();
    mode_.setSelectedIndex((int)p.mode, juce::dontSendNotification);
    if (!dragging_)
        smoothness_.setValue(p.smoothness, juce::dontSendNotification);
    juce::StringArray names;
    for (const auto& s : shapes)
        names.add(s.name.isEmpty() ? juce::String("unnamed") : s.name);
    keys_.setKeys(p.keys, names);
    showKeyRow(p.mode == Mode::ByHand);
}

void PerNoteBar::showKeyRow(bool shown) {
    if (!laidOut_)
        keyRowFade_->snapTo(shown);
    else
        keyRowFade_->setShown(shown);
}

/** `undoStep` false means a slider drag is open and will record one step when it ends. */
void PerNoteBar::write(const std::function<void(CustomFilterPerNote&)>& change, bool undoStep) {
    if (undoStep)
        adapter_.beginEdit();
    auto p = adapter_.getPerNote();
    change(p);
    adapter_.setPerNote(p);
    if (undoStep)
        adapter_.endEdit();
    if (onEdited)
        onEdited();
}

void PerNoteBar::resized() {
    auto area = getLocalBounds();
    auto row = area.removeFromTop(kRowHeight);
    labelArea_ = row.removeFromLeft(kLabelWidth);
    mode_.setBounds(row.removeFromLeft(kModeWidth).withSizeKeepingCentre(kModeWidth, 26));
    row.removeFromLeft(16);
    hardArea_ = row.removeFromLeft(kEndLabelWidth);
    smoothness_.setBounds(row.removeFromLeft(kSliderWidth));
    smoothArea_ = row.removeFromLeft(kEndLabelWidth + 8);

    area.removeFromTop(kGap);
    auto keyRow = area.removeFromTop(kKeyRowHeight).withTrimmedLeft(kLabelWidth);
    octaveDown_.setBounds(keyRow.removeFromLeft(kOctaveButtonWidth).withSizeKeepingCentre(kOctaveButtonWidth, 26));
    keyRow.removeFromLeft(6);
    keys_.setBounds(keyRow.removeFromLeft(juce::jmin(kKeyStripWidth, keyRow.getWidth() - kOctaveButtonWidth - 6)));
    keyRow.removeFromLeft(6);
    octaveUp_.setBounds(keyRow.removeFromLeft(kOctaveButtonWidth).withSizeKeepingCentre(kOctaveButtonWidth, 26));
    laidOut_ = true;
}

void PerNoteBar::paint(juce::Graphics& g) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.setColour(colors.textPrimary);
    g.drawText("Notes move the cursor", labelArea_, juce::Justification::centredLeft, true);
    g.setColour(colors.textMuted);
    g.drawText("Hard", hardArea_, juce::Justification::centredRight, true);
    g.drawText("Smooth", smoothArea_.withTrimmedLeft(8), juce::Justification::centredLeft, true);
}

} // namespace synth::ui
