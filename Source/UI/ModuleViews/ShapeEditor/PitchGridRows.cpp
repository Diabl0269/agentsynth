// PitchGridRows.cpp -- the Filter grid's two rows: building, laying out, remembering and following the song key
// and the played note.
#include "PitchGridRows.h"
#include "Music/SongKey.h"
#include "ShapeWindowPrefs.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kCaptionGap = 6; // between a caption and the control it names; label widths include it
constexpr int kRowsGridLabelWidth = 34 + kCaptionGap;
constexpr int kKindWidth = 232;
constexpr int kPillGap = 4;
constexpr int kGroupGap = 14;
constexpr int kLevelLabelWidth = 38 + kCaptionGap;
constexpr int kLevelWidth = 150;
// The first row without its Snap button.
constexpr int kRow1WithoutSnap =
    kRowsGridLabelWidth + kKindWidth + kGroupGap + kLevelLabelWidth + kLevelWidth + kPillGap;
constexpr int kStepsLabelWidth = 42 + kCaptionGap;
constexpr int kStepsWidth = 286;
constexpr int kKeyLabelWidth = 34 + kCaptionGap;
constexpr int kKeyWidth = 240;
constexpr int kRootLabelWidth = 38 + kCaptionGap;
constexpr int kRootWidth = 170;
// The Root box lists C1 to B6. Until a note is picked the root is the song key's (A2, 110 Hz, with no song key);
// Playing note falls back to A2 before the first note is heard.
constexpr int kFirstRootNote = 24;
constexpr int kLastRootNote = 95;
constexpr int kDefaultRootNote = 45;
// "Song key root" sits in the octave of A2, where a bass note's harmonics ring.
constexpr int kSongRootOctaveBase = 36;
constexpr const char* kNoScale = "none";

void styleLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(12.0f)));
    label.setJustificationType(juce::Justification::centredLeft);
    label.setInterceptsMouseClicks(false, false);
    label.setAccessible(false); // each control carries its own name
}

int indexOfStep(int steps) {
    const auto& counts = pitchgrid::stepCounts();
    const auto it = std::find(counts.begin(), counts.end(), steps);
    return it == counts.end() ? 0 : (int)(it - counts.begin());
}

int indexOfLevel(float db) {
    const auto& steps = pitchgrid::levelSteps();
    for (size_t i = 0; i < steps.size(); ++i)
        if (std::abs(steps[i] - db) < 1.0e-3f)
            return (int)i;
    return 0;
}
} // namespace

PitchGridRows::PitchGridRows(ShapeListAdapter& adapter)
    : adapter_(adapter)
    , kind_("Grid", pitchgrid::kindNames())
    , level_("Level", pitchgrid::levelNames())
    , snapMode_("shapeSnapMode", shapewindowprefs::snapModeKey(adapter.getPreferenceKind()))
    , steps_("Steps", pitchgrid::stepNames()) {
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(false, true); // the free end of the second row belongs to the owner's toggles
    styleLabel(gridLabel_, "Grid");
    styleLabel(levelLabel_, "Level");
    styleLabel(stepsLabel_, "Steps");
    styleLabel(keyLabel_, "Key");
    styleLabel(rootLabel_, "Root");

    kind_.setTooltip("Grid: what the curve snaps to across frequency: octave steps, notes or the harmonics of a note "
                     "(Off: free)");
    level_.setTooltip("Level: snap levels to this many dB (Off: free)");
    steps_.setTooltip("Steps: how fine the octave lines are (1/12 is one semitone)");
    snap_.setTitle("Snap");
    refreshSnapTooltips({});
    snap_.setLayout(ToolbarButtonLayout::Inline);
    snap_.setClickingTogglesState(true);
    snap_.setWantsKeyboardFocus(true);
    key_.setTooltip("Key: the notes the Notes grid snaps to; the other notes are dotted. Set Song Key: Shift+Cmd+K");
    root_.setTitle("Root");
    root_.addItem("Playing note", kRootPlaying);
    root_.addItem("Song key root", kRootSongKey);
    root_.addSeparator();
    for (int m = kFirstRootNote; m <= kLastRootNote; ++m)
        root_.addItem(pitchgrid::noteName(m) + " (" + pitchgrid::formatHz(pitchgrid::noteHz(m)) + ")", rootIdOf(m));

    int order = 1;
    for (juce::Component* c :
         std::initializer_list<juce::Component*>{&kind_, &level_, &snap_, &snapMode_, &steps_, &key_, &root_}) {
        c->setExplicitFocusOrder(order++);
        addAndMakeVisible(*c);
    }
    for (auto* label : {&gridLabel_, &levelLabel_, &stepsLabel_, &keyLabel_, &rootLabel_})
        addAndMakeVisible(*label);
    stepsFade_ = std::make_unique<FadeVisibility>(std::initializer_list<juce::Component*>{&stepsLabel_, &steps_});
    keyFade_ = std::make_unique<FadeVisibility>(std::initializer_list<juce::Component*>{&keyLabel_, &key_});
    rootFade_ = std::make_unique<FadeVisibility>(std::initializer_list<juce::Component*>{&rootLabel_, &root_});

    load();
    showChoice();
    lookAndFeelChanged();
    stepsFade_->snapTo(choice_.kind == pitchgrid::Kind::octaves);
    keyFade_->snapTo(choice_.kind == pitchgrid::Kind::notes);
    rootFade_->snapTo(choice_.kind == pitchgrid::Kind::harmonics);

    kind_.onChange = [this](int index) {
        choice_.kind = (pitchgrid::Kind)index;
        changed(true);
    };
    steps_.onChange = [this](int index) {
        choice_.stepsPerOctave = pitchgrid::stepCounts()[(size_t)index];
        changed(true);
    };
    level_.onChange = [this](int index) {
        choice_.levelDb = pitchgrid::levelSteps()[(size_t)index];
        changed(true);
    };
    snap_.onClick = [this] {
        choice_.snap = snap_.getToggleState();
        changed(true);
    };
    snapMode_.onModeChanged = [this](SnapMode mode) {
        if (mode == choice_.snapMode)
            return;
        choice_.snapMode = mode;
        changed(false); // the picker has already saved the mode
    };
    key_.onFollow = [this] {
        followSongKey_ = true;
        refreshKeyField();
        changed(true);
    };
    key_.onPickOwn = [this](std::optional<MusicalScale> key) {
        followSongKey_ = false;
        ownKey_ = key;
        refreshKeyField();
        changed(true);
    };
    root_.onChange = [this] {
        glide_.stop();
        glideHz_.reset();
        updateRootTooltip();
        changed(true);
    };
}

PitchGridRows::~PitchGridRows() = default;

// The new keys win; without them the old one-row choice ("1/3 oct") becomes Octaves at that step with 6 dB levels.
void PitchGridRows::load() {
    auto* props = adapter_.getApplicationProperties();
    const auto kind = adapter_.getPreferenceKind();
    using namespace shapewindowprefs;
    const auto savedKind = loadText(props, gridKindKey(kind));
    if (savedKind.isEmpty()) {
        if (const auto legacy = pitchgrid::fromLegacyName(loadText(props, gridKey(kind))))
            choice_ = *legacy;
    } else {
        choice_.kind = (pitchgrid::Kind)juce::jmax(0, pitchgrid::kindNames().indexOf(savedKind));
        const int steps = pitchgrid::stepNames().indexOf(loadText(props, gridStepsKey(kind)));
        choice_.stepsPerOctave = pitchgrid::stepCounts()[(size_t)juce::jmax(0, steps)];
        const int level = pitchgrid::levelNames().indexOf(loadText(props, gridLevelKey(kind)));
        choice_.levelDb = pitchgrid::levelSteps()[(size_t)juce::jmax(0, level)];
    }
    choice_.snap = loadText(props, snapKey(kind)) != "0";
    snapMode_.setApplicationProperties(props); // before its handler is set: restoring changes nothing to announce
    choice_.snapMode = snapMode_.getMode();

    const auto savedKey = loadText(props, gridOwnKeyKey(kind));
    followSongKey_ = savedKey.isEmpty();
    if (!followSongKey_ && savedKey != kNoScale)
        ownKey_ = synth::keyFromVar(juce::JSON::parse(savedKey));
    songKey_ = adapter_.getSongKey();
    const int rootId = loadText(props, gridRootKey(kind)).getIntValue();
    root_.setSelectedId(root_.indexOfItemId(rootId) >= 0 ? rootId : kRootSongKey, juce::dontSendNotification);
    playedHz_ = adapter_.getPlayedNoteHz();
    refreshKeyField();
    updateRootTooltip();
}

void PitchGridRows::save() const {
    auto* props = adapter_.getApplicationProperties();
    const auto kind = adapter_.getPreferenceKind();
    using namespace shapewindowprefs;
    saveText(props, gridKindKey(kind), pitchgrid::kindNames()[(int)choice_.kind]);
    saveText(props, gridStepsKey(kind), pitchgrid::stepNames()[indexOfStep(choice_.stepsPerOctave)]);
    saveText(props, gridLevelKey(kind), pitchgrid::levelNames()[indexOfLevel(choice_.levelDb)]);
    saveText(props, snapKey(kind), choice_.snap ? juce::String() : juce::String("0"));
    saveText(props, gridOwnKeyKey(kind),
             followSongKey_        ? juce::String()
             : ownKey_.has_value() ? juce::JSON::toString(synth::keyToVar(*ownKey_), true)
                                   : juce::String(kNoScale));
    saveText(props, gridRootKey(kind), juce::String(root_.getSelectedId()));
    saveText(props, gridKey(kind), {}); // the old one-row choice has been carried over
}

void PitchGridRows::showChoice() {
    kind_.setSelectedIndex((int)choice_.kind, juce::dontSendNotification);
    steps_.setSelectedIndex(indexOfStep(choice_.stepsPerOctave), juce::dontSendNotification);
    level_.setSelectedIndex(indexOfLevel(choice_.levelDb), juce::dontSendNotification);
    snap_.setToggleState(choice_.snap, juce::dontSendNotification);
    snap_.setEnabled(choice_.kind != pitchgrid::Kind::off || choice_.levelDb > 0.0f); // else no line to land on
    snapMode_.setEnabled(snap_.isEnabled());
    if (snapMode_.getMode() != choice_.snapMode)
        snapMode_.choose(choice_.snapMode);
}

// The second row shows what the kind needs, fading one group out and the next in where they share the row.
void PitchGridRows::changed(bool remember) {
    showChoice();
    stepsFade_->setShown(choice_.kind == pitchgrid::Kind::octaves);
    keyFade_->setShown(choice_.kind == pitchgrid::Kind::notes);
    rootFade_->setShown(choice_.kind == pitchgrid::Kind::harmonics);
    if (remember)
        save();
    if (onGridChanged)
        onGridChanged();
}

void PitchGridRows::setChoice(const pitchgrid::Choice& choice) {
    choice_ = choice;
    changed(true);
}

void PitchGridRows::setRootId(int id) {
    if (root_.indexOfItemId(id) >= 0)
        root_.setSelectedId(id, juce::sendNotificationSync);
}

std::optional<MusicalScale> PitchGridRows::activeKey() const { return followSongKey_ ? songKey_ : ownKey_; }

// Playing note: the last pitch the Filter heard, holding the last one between notes (and A2 before the first).
double PitchGridRows::rootHz() const {
    const int id = root_.getSelectedId();
    if (id == kRootPlaying)
        return playedHz_ > 0.0f ? (double)playedHz_ : pitchgrid::noteHz(kDefaultRootNote);
    if (id == kRootSongKey)
        return pitchgrid::noteHz(kSongRootOctaveBase + (songKey_.has_value() ? songKey_->rootPitchClass : 9));
    return pitchgrid::noteHz(juce::jmax(kFirstRootNote, id - kRootNoteBase));
}

std::optional<CurveEditorComponent::CurveGrid> PitchGridRows::grid() const {
    return pitchgrid::gridFor(choice_, {activeKey(), shownRootHz()});
}

void PitchGridRows::refreshSnapTooltips(const WindowSnapKey& key) {
    snap_.setTooltip(key.tooltip(SnapToggleButton::tooltipLead() +
                                 ": points land on the grid's lines and the level lines. Off: the lines stay as a "
                                 "guide and points move freely. Shift while dragging is always free"));
    snapMode_.setTooltipDecorator([key](const juce::String& text) { return key.tooltip(text); });
}

juce::String PitchGridRows::flipSnaps(bool& refused) {
    refused = false;
    if (!snap_.isEnabled()) {
        refused = true;
        return "Snap has no lines to land on: choose a Grid or a Level first";
    }
    choice_.snap = !choice_.snap;
    changed(true);
    return WindowSnapKey::message(choice_.snap);
}

void PitchGridRows::poll() {
    bool moved = false;
    const auto songKey = adapter_.getSongKey();
    if (!synth::sameKey(songKey, songKey_)) {
        songKey_ = songKey;
        refreshKeyField();
        moved = (choice_.kind == pitchgrid::Kind::notes && followSongKey_) ||
                (choice_.kind == pitchgrid::Kind::harmonics && root_.getSelectedId() == kRootSongKey);
    }
    const float played = adapter_.getPlayedNoteHz();
    if (played > 0.0f && played != playedHz_) {
        const double before = playedHz_ > 0.0f ? shownRootHz() : 0.0; // the first note ever heard just appears
        playedHz_ = played;
        if (choice_.kind == pitchgrid::Kind::harmonics && root_.getSelectedId() == kRootPlaying) {
            if (prefersReducedMotion() || before <= 0.0 || before == (double)played) {
                glide_.stop();
                glideHz_.reset();
                moved = true;
            } else {
                glideTo(before, (double)played); // fires onGridChanged itself on every frame
            }
        }
    }
    if (moved && onGridChanged)
        onGridChanged();
}

// The line slides along the log axis, so it moves in even musical steps.
void PitchGridRows::glideTo(double fromHz, double toHz) {
    glideHz_ = fromHz;
    glide_.start(
        glideUpdater_, kGlideMs, easeOutCubic,
        [this, fromHz, toHz](float t) {
            glideHz_ = fromHz * std::pow(toHz / fromHz, (double)t);
            if (onGridChanged)
                onGridChanged();
        },
        [this] {
            glideHz_.reset();
            if (onGridChanged)
                onGridChanged();
        });
}

void PitchGridRows::refreshKeyField() { key_.setState(followSongKey_, songKey_, ownKey_); }

// The Root box says what Playing note needs: the Filter's Pitch input, like the per-note modes.
void PitchGridRows::updateRootTooltip() {
    juce::String tip = "Root: harmonics are counted from this note. Playing note follows each note the Filter plays";
    if (root_.getSelectedId() == kRootPlaying && adapter_.pitchInput() != PitchInput::Connected)
        tip += " (needs a Pitch cable)";
    root_.setTooltip(tip);
    root_.setDescription(root_.getText());
}

void PitchGridRows::lookAndFeelChanged() {
    const auto muted = synth::theme::themeOf(*this).colors.textMuted;
    for (auto* label : {&gridLabel_, &levelLabel_, &stepsLabel_, &keyLabel_, &rootLabel_})
        label->setColour(juce::Label::textColourId, muted);
}

void PitchGridRows::resized() {
    auto row1 = getLocalBounds().removeFromTop(kRowHeight);
    auto row2 = getLocalBounds().withTrimmedTop(kRowHeight + kRowGap).withHeight(kRowHeight);
    // The caption "Snap" shows only while the button with it fits the row; otherwise it is icon only (its tooltip
    // starts with the name).
    snap_.setLabel("Snap", false);
    const int pickerWidth = kPillGap / 2 + snapMode_.getFixedWidth();
    if (kRow1WithoutSnap + snap_.getPreferredWidthFor("Snap") + pickerWidth <= getWidth())
        snap_.setButtonText("Snap");
    gridLabel_.setBounds(row1.removeFromLeft(kRowsGridLabelWidth).withTrimmedRight(kCaptionGap));
    kind_.setBounds(row1.removeFromLeft(kKindWidth));
    row1.removeFromLeft(kGroupGap);
    levelLabel_.setBounds(row1.removeFromLeft(kLevelLabelWidth).withTrimmedRight(kCaptionGap));
    level_.setBounds(row1.removeFromLeft(kLevelWidth));
    row1.removeFromLeft(kPillGap);
    snap_.setBounds(row1.removeFromLeft(snap_.getPreferredWidth()));
    row1.removeFromLeft(kPillGap / 2);
    snapMode_.setBounds(row1.removeFromLeft(snapMode_.getFixedWidth()));

    auto steps = row2;
    stepsLabel_.setBounds(steps.removeFromLeft(kStepsLabelWidth).withTrimmedRight(kCaptionGap));
    steps_.setBounds(steps.removeFromLeft(kStepsWidth));
    auto key = row2;
    keyLabel_.setBounds(key.removeFromLeft(kKeyLabelWidth).withTrimmedRight(kCaptionGap));
    key_.setBounds(key.removeFromLeft(kKeyWidth));
    auto root = row2;
    rootLabel_.setBounds(root.removeFromLeft(kRootLabelWidth).withTrimmedRight(kCaptionGap));
    root_.setBounds(root.removeFromLeft(kRootWidth));
}

} // namespace synth::ui
