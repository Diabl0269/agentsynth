// DrumIdeasPanel: the controls and layout of the Ideas popover, and the takes it generates and plays.

#include "DrumIdeasPanel.h"

#include "DrumLanes.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

constexpr int kPad = 10;
constexpr int kGap = 6;
constexpr int kRowHeight = 22;
constexpr int kLabelHeight = 16;
constexpr int kTakeHeight = 54;
constexpr int kStarterColumns = 4;
constexpr float kLabelFont = 10.5f;
constexpr int kMaxMiniSteps = 64;

} // namespace

//==============================================================================
DrumMiniGrid::DrumMiniGrid()
    : juce::Button({}) {
    setClickingTogglesState(false);
    setWantsKeyboardFocus(true);
}

void DrumMiniGrid::setNotes(std::vector<synth::MidiNote> notes, const drumideas::Grid& grid) {
    notes_ = std::move(notes);
    grid_ = grid;
    repaint();
}

void DrumMiniGrid::setPlaying(bool playing) {
    playing_ = playing;
    repaint();
}

// A row per drum of the default kit, a cell per step (at most the first 64): the picture of the take, not an editor.
void DrumMiniGrid::paintButton(juce::Graphics& g, bool over, bool) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto area = getLocalBounds().toFloat();
    g.setColour(theme.colors.surface);
    g.fillRoundedRectangle(area, theme.metrics.cornerRadiusSmall);
    const auto& lanes = defaultDrumLanes();
    const int steps =
        std::min(kMaxMiniSteps, std::max(1, static_cast<int>(std::ceil(grid_.lengthBeats / grid_.stepBeats))));
    const auto inner = area.reduced(3.0f);
    const float cellW = inner.getWidth() / static_cast<float>(steps);
    const float cellH = inner.getHeight() / static_cast<float>(lanes.size());
    for (const auto& note : notes_) {
        const auto it =
            std::find_if(lanes.begin(), lanes.end(), [&note](const auto& l) { return l.pitch == note.pitch; });
        const int step = static_cast<int>(std::floor(note.startBeat / grid_.stepBeats + 1.0e-6));
        if (it == lanes.end() || step < 0 || step >= steps)
            continue;
        const int row = static_cast<int>(it - lanes.begin());
        g.setColour(it->colour.withAlpha(0.35f + 0.65f * static_cast<float>(note.velocity) / 127.0f));
        g.fillRect(juce::Rectangle<float>(inner.getX() + step * cellW, inner.getY() + row * cellH,
                                          std::max(1.0f, cellW - 0.5f), std::max(1.0f, cellH - 0.5f)));
    }
    if (notes_.empty()) {
        g.setColour(theme.colors.textMuted);
        g.setFont(theme::AppLookAndFeel::uiFont(kLabelFont));
        g.drawText("Make 3 takes", getLocalBounds(), juce::Justification::centred, true);
    }
    g.setColour(playing_ || over ? theme.colors.accent : theme.colors.border);
    g.drawRoundedRectangle(area.reduced(0.5f), theme.metrics.cornerRadiusSmall, playing_ ? 1.5f : 1.0f);
    if (hasKeyboardFocus(false))
        paintFocusRing(g, area, *this, theme.metrics.cornerRadiusSmall);
}

//==============================================================================
void DrumTakeAudition::start(const std::vector<synth::MidiNote>& notes, double lengthBeats, double secondsPerBeat) {
    stop();
    notes_ = notes;
    std::stable_sort(notes_.begin(), notes_.end(),
                     [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    spb_ = secondsPerBeat > 0.0 ? secondsPerBeat : 0.5;
    totalSeconds_ = lengthBeats * spb_;
    next_ = 0;
    startedMs_ = juce::Time::getMillisecondCounterHiRes();
    advanceTo(0.0);
    if (totalSeconds_ > 0.0 && isTimerRunning() == false)
        startTimer(kTickMs);
}

void DrumTakeAudition::stop() {
    const bool was = isTimerRunning();
    stopTimer();
    next_ = notes_.size();
    if (was && onFinished)
        onFinished();
}

void DrumTakeAudition::advanceTo(double elapsedSeconds) {
    while (next_ < notes_.size() && notes_[next_].startBeat * spb_ <= elapsedSeconds + 1.0e-9) {
        if (onHit)
            onHit(notes_[next_].pitch, notes_[next_].velocity);
        ++next_;
    }
}

void DrumTakeAudition::timerCallback() {
    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - startedMs_) / 1000.0;
    advanceTo(elapsed);
    if (elapsed >= totalSeconds_)
        stop(); // one pass, then silence
}

//==============================================================================
DrumIdeasPanel::DrumIdeasPanel(std::function<DrumIdeasContext()> context)
    : context_(std::move(context)) {
    setTitle("Ideas");
    setComponentID("drumIdeasPanel");
    for (auto* label : {&title_, &startersLabel_, &fillsLabel_, &varyLabel_, &subtleLabel_, &wildLabel_,
                        &randomizeLabel_, &sparseLabel_, &busyLabel_}) {
        label->setInterceptsMouseClicks(false, false);
        label->setWantsKeyboardFocus(false);
        label->setFont(theme::AppLookAndFeel::uiFont(kLabelFont));
        addAndMakeVisible(*label);
    }
    title_.setFont(theme::AppLookAndFeel::uiSemiBoldFont(13.0f));
    for (auto* label : {&startersLabel_, &fillsLabel_, &varyLabel_, &randomizeLabel_})
        label->setFont(theme::AppLookAndFeel::uiSemiBoldFont(11.5f));
    wildLabel_.setJustificationType(juce::Justification::centredRight);
    busyLabel_.setJustificationType(juce::Justification::centredRight);

    for (int i = 0; i < drumideas::kStyleCount; ++i) {
        const auto style = drumideas::styleAt(i);
        const auto name = drumideas::styleName(style);
        auto& button = starters_[(size_t)i];
        button = std::make_unique<juce::TextButton>(name);
        button->setTitle(name + " starter");
        button->setTooltip("Fill this clip with a " + name + " pattern, repeated across the clip");
        button->onClick = [this, style] {
            if (onStarter)
                onStarter(style);
        };
        addAndMakeVisible(*button);
    }

    for (const auto fill : {drumideas::Fill::SnareRoll, drumideas::Fill::TomFill}) {
        auto& button = fill == drumideas::Fill::SnareRoll ? snareRoll_ : tomFill_;
        const bool roll = fill == drumideas::Fill::SnareRoll;
        button.setTitle(drumideas::fillName(fill));
        button.setTooltip(juce::String("Replace the last ") + (roll ? "beat" : "two beats") +
                          " of the last bar (kicks stay) with " +
                          (roll ? "a rising snare roll" : "a tom fill, high to low") +
                          ". One undo step. Turn on Every 4th bar to fill bars 4, 8, 12 and so on");
        button.onClick = [this, fill] {
            if (onFill)
                onFill(fill, everyFourth_.getToggleState());
        };
        addAndMakeVisible(button);
    }
    everyFourth_.setTitle("Every 4th bar");
    everyFourth_.setTooltip("Fill bars 4, 8, 12 and so on instead of only the last bar (the last bar when the clip is "
                            "shorter than 4 bars)");
    setTogglePillStyle(everyFourth_, true);
    addAndMakeVisible(everyFourth_);

    amount_.setRange(0.0, 100.0, 1.0);
    amount_.setValue(40.0, juce::dontSendNotification);
    amount_.setTitle("Amount");
    amount_.setTooltip("How much each take changes: Subtle moves a hit or two, Wild changes a lot");
    amount_.setWantsKeyboardFocus(true); // a slider without a text box is not a Tab stop by default
    addAndMakeVisible(amount_);

    makeTakes_.setTitle("Make 3 takes");
    makeTakes_.setTooltip("Make three variations of this clip to hear and pick from");
    makeTakes_.onClick = [this] { makeTakes(); };
    addAndMakeVisible(makeTakes_);

    for (int i = 0; i < kTakes; ++i) {
        const auto number = juce::String(i + 1);
        takeGrids_[(size_t)i] = std::make_unique<DrumMiniGrid>();
        auto& grid = *takeGrids_[(size_t)i];
        grid.setTitle("Take " + number);
        grid.setTooltip("Play take " + number + " once");
        grid.onClick = [this, i] { playTake(i); };
        addAndMakeVisible(grid);
        picks_[(size_t)i] = std::make_unique<juce::TextButton>("Pick");
        auto& pick = *picks_[(size_t)i];
        pick.setTitle("Pick take " + number);
        pick.setTooltip("Use take " + number +
                        ": add it as a new clip after this one (one undo step), or replace this "
                        "clip when Replace this clip is on");
        pick.onClick = [this, i] {
            if (!takes_[(size_t)i].empty() && onPick)
                onPick(takes_[(size_t)i], replace_.getToggleState(), i);
        };
        addAndMakeVisible(pick);
    }
    replace_.setTitle("Replace this clip");
    replace_.setTooltip("Pick puts the take into this clip instead of adding a new clip after it");
    setTogglePillStyle(replace_, true);
    addAndMakeVisible(replace_);

    density_.setRange(0.0, 100.0, 1.0);
    density_.setValue(50.0, juce::dontSendNotification);
    density_.setTitle("Density");
    density_.setTooltip("How many hits the new pattern has, from Sparse to Busy");
    density_.setWantsKeyboardFocus(true); // a slider without a text box is not a Tab stop by default
    addAndMakeVisible(density_);
    keep_.setTitle("Keep kick and snare");
    keep_.setTooltip("Keep the kick and snare hits that are there and randomize only the other drums");
    setTogglePillStyle(keep_, true);
    addAndMakeVisible(keep_);
    randomize_.setTitle("Randomize");
    randomize_.setTooltip("Fill this clip with a new random pattern. Each press is one undo step");
    randomize_.onClick = [this] {
        if (onRandomize)
            onRandomize(density_.getValue() / 100.0, keep_.getToggleState(), nextSeed());
    };
    addAndMakeVisible(randomize_);

    audition_.onHit = [this](int pitch, int velocity) {
        if (onAudition)
            onAudition(pitch, velocity);
    };
    audition_.onFinished = [this] {
        if (playingTake_ >= 0)
            takeGrids_[(size_t)playingTake_]->setPlaying(false);
        playingTake_ = -1;
    };

    applyColours();
    setSize(kWidth, kHeight);
}

DrumIdeasPanel::~DrumIdeasPanel() {
    audition_.stop();
    notifyClosed();
}

int DrumIdeasPanel::nextSeed() {
    if (seeded_)
        return seed_++;
    return static_cast<int>(juce::Time::currentTimeMillis() & 0x7fffffff) ^ (seed_++ * 7919);
}

void DrumIdeasPanel::makeTakes() {
    audition_.stop();
    const auto ctx = context_ ? context_() : DrumIdeasContext{};
    const double amount = amount_.getValue() / 100.0;
    for (int i = 0; i < kTakes; ++i) {
        auto variation = drumideas::vary(ctx.notes, ctx.grid, amount, nextSeed());
        takes_[(size_t)i] = variation.notes;
        takeGrids_[(size_t)i]->setNotes(std::move(variation.notes), ctx.grid);
    }
}

// Plays the take once, in time; pressing it again (or another take) stops the pass.
void DrumIdeasPanel::playTake(int index) {
    const bool same = playingTake_ == index && audition_.isPlaying();
    audition_.stop();
    if (same || takes_[(size_t)index].empty())
        return;
    const auto ctx = context_ ? context_() : DrumIdeasContext{};
    playingTake_ = index;
    takeGrids_[(size_t)index]->setPlaying(true);
    audition_.start(takes_[(size_t)index], ctx.grid.lengthBeats, ctx.secondsPerBeat);
}

void DrumIdeasPanel::applyColours() {
    const auto& c = theme::themeOf(*this).colors;
    title_.setColour(juce::Label::textColourId, c.textPrimary);
    for (auto* label : {&startersLabel_, &fillsLabel_, &varyLabel_, &randomizeLabel_})
        label->setColour(juce::Label::textColourId, c.textPrimary);
    for (auto* label : {&subtleLabel_, &wildLabel_, &sparseLabel_, &busyLabel_})
        label->setColour(juce::Label::textColourId, c.textMuted);
    for (auto* button : {&makeTakes_, &randomize_}) {
        button->setColour(juce::TextButton::buttonColourId, c.accent);
        button->setColour(juce::TextButton::textColourOffId, c.bg0);
    }
}

void DrumIdeasPanel::lookAndFeelChanged() { applyColours(); }

void DrumIdeasPanel::paint(juce::Graphics& g) {
    const auto& c = theme::themeOf(*this).colors;
    g.fillAll(c.surfaceHi);
    g.setColour(c.border);
    for (const int y : {dividerY_[0], dividerY_[1], dividerY_[2]})
        if (y > 0)
            g.drawHorizontalLine(y, static_cast<float>(kPad), static_cast<float>(getWidth() - kPad));
}

void DrumIdeasPanel::resized() {
    auto area = getLocalBounds().reduced(kPad);
    title_.setBounds(area.removeFromTop(18));
    area.removeFromTop(kGap);

    startersLabel_.setBounds(area.removeFromTop(kLabelHeight));
    const int buttonWidth = (area.getWidth() - (kStarterColumns - 1) * kGap) / kStarterColumns;
    for (int row = 0; row < drumideas::kStyleCount / kStarterColumns; ++row) {
        auto strip = area.removeFromTop(kRowHeight);
        for (int col = 0; col < kStarterColumns; ++col) {
            starters_[(size_t)(row * kStarterColumns + col)]->setBounds(strip.removeFromLeft(buttonWidth));
            strip.removeFromLeft(kGap);
        }
        area.removeFromTop(kGap);
    }
    area.removeFromTop(kGap);
    dividerY_[0] = area.getY() - kGap;

    fillsLabel_.setBounds(area.removeFromTop(kLabelHeight));
    auto fillsRow = area.removeFromTop(kRowHeight);
    snareRoll_.setBounds(fillsRow.removeFromLeft(buttonWidth));
    fillsRow.removeFromLeft(kGap);
    tomFill_.setBounds(fillsRow.removeFromLeft(buttonWidth));
    fillsRow.removeFromLeft(kGap);
    everyFourth_.setBounds(
        fillsRow.removeFromLeft(theme::AppLookAndFeel::togglePillWidth(everyFourth_.getButtonText())));
    area.removeFromTop(kGap);
    area.removeFromTop(kGap);
    dividerY_[2] = area.getY() - kGap;

    varyLabel_.setBounds(area.removeFromTop(kLabelHeight));
    auto amountRow = area.removeFromTop(kRowHeight);
    makeTakes_.setBounds(amountRow.removeFromRight(96));
    amountRow.removeFromRight(kGap);
    subtleLabel_.setBounds(amountRow.removeFromLeft(44));
    wildLabel_.setBounds(amountRow.removeFromRight(30));
    amount_.setBounds(amountRow);
    area.removeFromTop(kGap);
    auto takesRow = area.removeFromTop(kTakeHeight);
    const int takeWidth = (area.getWidth() - (kTakes - 1) * kGap) / kTakes;
    auto picksRow = area.removeFromTop(kRowHeight + kGap).withTrimmedTop(kGap);
    for (int i = 0; i < kTakes; ++i) {
        takeGrids_[(size_t)i]->setBounds(takesRow.removeFromLeft(takeWidth));
        picks_[(size_t)i]->setBounds(picksRow.removeFromLeft(takeWidth));
        takesRow.removeFromLeft(kGap);
        picksRow.removeFromLeft(kGap);
    }
    area.removeFromTop(kGap);
    replace_.setBounds(area.removeFromTop(kRowHeight)
                           .removeFromLeft(theme::AppLookAndFeel::togglePillWidth(replace_.getButtonText())));
    area.removeFromTop(kGap);
    dividerY_[1] = area.getY();
    area.removeFromTop(kGap);

    randomizeLabel_.setBounds(area.removeFromTop(kLabelHeight));
    auto densityRow = area.removeFromTop(kRowHeight);
    sparseLabel_.setBounds(densityRow.removeFromLeft(44));
    busyLabel_.setBounds(densityRow.removeFromRight(30));
    density_.setBounds(densityRow);
    area.removeFromTop(kGap);
    auto lastRow = area.removeFromTop(kRowHeight);
    randomize_.setBounds(lastRow.removeFromRight(96));
    keep_.setBounds(lastRow.removeFromLeft(theme::AppLookAndFeel::togglePillWidth(keep_.getButtonText())));
}

void DrumIdeasPanel::notifyClosed() {
    if (closedNotified_)
        return;
    closedNotified_ = true;
    if (onClosed)
        onClosed();
}

bool DrumIdeasPanel::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey)
        return false;
    audition_.stop();
    closeHostingWindow(*this); // fades the popup out; a headless run has none
    notifyClosed();            // either way the owner gets the focus back at once
    return true;
}

void DrumIdeasPanel::parentHierarchyChanged() { reveal_.startIfInCallout(); }

} // namespace synth::ui
