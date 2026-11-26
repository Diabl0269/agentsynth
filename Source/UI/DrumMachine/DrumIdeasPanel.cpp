// DrumIdeasPanel: the controls and layout of the Ideas popover, and the takes it generates and plays.

#include "DrumIdeasPanel.h"
#include "UI/Controls/FineDrag/FineDragSlider.h"
#include "UI/Layout/FocusStepWithin.h"

#include "DrumLanes.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/Layout/ButtonStateEase.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

/** The Ideas panel's keys: bare Up/Down step to the neighbouring control (a panel of controls takes Up/Down, a bar
 * takes Left/Right: docs/development/accessibility.md#key-rings), stopping at both ends. Escape is not claimed: it
 * reaches the popover, which closes it. A slider gives its Up/Down up here and keeps Left/Right. */
class DrumIdeasKeys : public juce::KeyListener {
public:
    explicit DrumIdeasKeys(juce::Component& root)
        : root_(root) {}

    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override {
        if (origin == nullptr || key.getModifiers().isAnyModifierKeyDown())
            return false;
        const bool up = key.isKeyCode(juce::KeyPress::upKey);
        if (!up && !key.isKeyCode(juce::KeyPress::downKey))
            return false;
        lastStep_ = neighbourStop(root_, origin, up ? -1 : 1);
        if (lastStep_ != nullptr)
            lastStep_->grabKeyboardFocus();
        return true;
    }
    /** Where the last Up or Down asked focus to go (null: nowhere, an end of the panel); a test reads it because focus
     *  calls do nothing without a native window. */
    juce::Component* getLastStepForTest() const noexcept { return lastStep_; }

private:
    juce::Component& root_;
    juce::Component* lastStep_ = nullptr;
};

namespace {

constexpr int kPad = 10;
constexpr int kGap = 6;
constexpr int kRowHeight = 22;
constexpr int kLabelHeight = 16;
constexpr int kTakeHeight = 54;
constexpr int kHintHeight = 28; // two lines of the 10.5 px hint
constexpr int kStarterColumns = 4;
constexpr float kLabelFont = 10.5f;
constexpr int kMaxMiniSteps = 64;
constexpr float kLitWashAlpha = 0.2f;

} // namespace

//==============================================================================
DrumStarterChip::DrumStarterChip(const juce::String& name)
    : juce::TextButton(name)
    , baseTitle_(name + " starter") {
    setTitle(baseTitle_);
    setToggleable(true); // the screen reader hears a toggle; a press never turns it off (clicking does not toggle)
}

void DrumStarterChip::setSelected(bool selected) {
    setToggleState(selected, juce::dontSendNotification);
    setTitle(selected ? baseTitle_ + ", selected" : baseTitle_);
}

// Off it is the plain button; lit it takes a 20% accent wash, an accent border and semibold text, all eased by the
// shared button ease (which follows Reduce Motion).
void DrumStarterChip::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto& theme = theme::themeOf(*this);
    const auto& c = theme.colors;
    const float radius = theme.metrics.pillRadius;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto amounts = ButtonStateEase::update(*this, over && isEnabled(), down && isEnabled());
    const auto fill = ButtonStateEase::blend(amounts, [&](bool on, bool hot, bool pressed) {
        auto base = findColour(juce::TextButton::buttonColourId);
        if (on)
            base = base.overlaidWith(c.accent.withAlpha(kLitWashAlpha));
        return pressed ? base.darker(0.2f) : (hot ? base.brighter(0.12f) : base);
    });
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(ButtonStateEase::mix(c.border, c.accent, amounts.on));
    g.drawRoundedRectangle(bounds, radius, theme.metrics.borderWidth);
    const auto font = getLookAndFeel().getTextButtonFont(*this, getHeight());
    g.setFont(getToggleState() ? theme::AppLookAndFeel::uiSemiBoldFont(font.getHeight()) : font);
    g.setColour(ButtonStateEase::mix(findColour(juce::TextButton::textColourOffId), c.textPrimary, amounts.on));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
    paintFocusRing(g, getLocalBounds().toFloat(), *this, radius);
}

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
    for (auto* label :
         {&title_, &startersLabel_, &fillsLabel_, &amountLabel_, &amountHint_, &subtleLabel_, &wildLabel_}) {
        label->setInterceptsMouseClicks(false, false);
        label->setWantsKeyboardFocus(false);
        label->setFont(theme::AppLookAndFeel::uiFont(kLabelFont));
        addAndMakeVisible(*label);
    }
    title_.setFont(theme::AppLookAndFeel::uiSemiBoldFont(13.0f));
    for (auto* label : {&startersLabel_, &fillsLabel_, &amountLabel_})
        label->setFont(theme::AppLookAndFeel::uiSemiBoldFont(11.5f));
    wildLabel_.setJustificationType(juce::Justification::centredRight);
    amountHint_.setJustificationType(juce::Justification::topLeft);
    amountHint_.setMinimumHorizontalScale(1.0f);

    for (int i = 0; i < drumideas::kStyleCount; ++i) {
        const auto style = drumideas::styleAt(i);
        const auto name = drumideas::styleName(style);
        auto& button = starters_[(size_t)i];
        button = std::make_unique<DrumStarterChip>(name);
        button->setTooltip("Fill this clip with a " + name +
                           " pattern, repeated across the clip. The chosen style stays "
                           "lit and the takes lean toward it");
        button->onClick = [this, i, style] {
            setSelectedStyle(i);
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

    amount_ = std::make_unique<synth::ui::FineDragSlider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    amount_->setRange(0.0, 100.0, 1.0);
    amount_->setValue(40.0, juce::dontSendNotification);
    amount_->setTitle("Amount");
    amount_->setTooltip("How much changes: Subtle moves a hit or two, Wild changes a lot. Used by Make 3 takes and "
                        "Randomize");
    amount_->setWantsKeyboardFocus(true); // a slider without a text box is not a Tab stop by default
    addAndMakeVisible(*amount_);

    makeTakes_.setTitle("Make 3 takes");
    makeTakes_.setTooltip("Make three variations of this clip to hear and pick from, leaning toward the lit style");
    makeTakes_.onClick = [this] { makeTakes(); };
    addAndMakeVisible(makeTakes_);
    randomize_.setTitle("Randomize");
    randomize_.setTooltip("Fill this clip with a new random pattern right away: a low amount gives few hits, a high "
                          "amount many. Each press is one undo step");
    randomize_.onClick = [this] {
        if (onRandomize)
            onRandomize(amount_->getValue() / 100.0, keep_.getToggleState(), nextSeed());
    };
    addAndMakeVisible(randomize_);

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
                        " in this clip: it replaces the pattern, playback keeps going, and "
                        "one undo brings the old pattern back");
        pick.onClick = [this, i] {
            if (!takes_[(size_t)i].empty() && onPick)
                onPick(takes_[(size_t)i], i);
        };
        addAndMakeVisible(pick);
    }

    keep_.setTitle("Keep kick and snare");
    keep_.setTooltip("Keep the kick and snare hits that are there when you press Randomize, so it changes only the "
                     "other drums");
    setTogglePillStyle(keep_, true);
    addAndMakeVisible(keep_);

    audition_.onHit = [this](int pitch, int velocity) {
        if (onAudition)
            onAudition(pitch, velocity);
    };
    audition_.onFinished = [this] {
        if (playingTake_ >= 0)
            takeGrids_[(size_t)playingTake_]->setPlaying(false);
        playingTake_ = -1;
    };

    keys_ = std::make_unique<DrumIdeasKeys>(*this);
    for (auto* stop : focusableDescendants(*this))
        stop->addKeyListener(keys_.get());

    applyColours();
    if (context_)
        setSelectedStyle(context_().drumStyle);
    setSize(kWidth, kHeight);
}

void DrumIdeasPanel::setSelectedStyle(int style) {
    selectedStyle_ = style >= 0 && style < drumideas::kStyleCount ? style : -1;
    for (int i = 0; i < drumideas::kStyleCount; ++i)
        starters_[(size_t)i]->setSelected(i == selectedStyle_);
}

// Undo and redo may have changed the clip's genre with its notes: the lit chip follows the clip.
void DrumIdeasPanel::appUndoneOrRedone() {
    if (context_)
        setSelectedStyle(context_().drumStyle);
}

juce::Component* DrumIdeasPanel::getLastKeyStepForTest() const noexcept { return keys_->getLastStepForTest(); }

bool DrumIdeasPanel::pressKeyForTest(const juce::KeyPress& key, juce::Component* origin) {
    return keys_->keyPressed(key, origin);
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
    const double amount = amount_->getValue() / 100.0;
    for (int i = 0; i < kTakes; ++i) {
        auto variation =
            drumideas::vary(ctx.notes, ctx.grid, amount, nextSeed(),
                            ctx.drumStyle >= 0 ? std::optional(drumideas::styleAt(ctx.drumStyle)) : std::nullopt);
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
    for (auto* label : {&startersLabel_, &fillsLabel_, &amountLabel_})
        label->setColour(juce::Label::textColourId, c.textPrimary);
    for (auto* label : {&amountHint_, &subtleLabel_, &wildLabel_})
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
    for (const int y : dividerY_)
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
    dividerY_[1] = area.getY() - kGap;

    amountLabel_.setBounds(area.removeFromTop(kLabelHeight));
    amountHint_.setBounds(area.removeFromTop(kHintHeight));
    area.removeFromTop(kGap);
    auto amountRow = area.removeFromTop(kRowHeight);
    subtleLabel_.setBounds(amountRow.removeFromLeft(44));
    wildLabel_.setBounds(amountRow.removeFromRight(30));
    amount_->setBounds(amountRow);
    area.removeFromTop(kGap);
    auto buttonsRow = area.removeFromTop(kRowHeight);
    const int halfWidth = (buttonsRow.getWidth() - kGap) / 2;
    makeTakes_.setBounds(buttonsRow.removeFromLeft(halfWidth));
    buttonsRow.removeFromLeft(kGap);
    randomize_.setBounds(buttonsRow);
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
    keep_.setBounds(
        area.removeFromTop(kRowHeight).removeFromLeft(theme::AppLookAndFeel::togglePillWidth(keep_.getButtonText())));
}

void DrumIdeasPanel::notifyClosed() {
    if (closedNotified_)
        return;
    closedNotified_ = true;
    if (onClosed)
        onClosed();
}

// Escape: the box fades out; the owner gets the focus back at once.
void DrumIdeasPanel::onPopoverCancel() {
    audition_.stop();
    notifyClosed();
}

} // namespace synth::ui
