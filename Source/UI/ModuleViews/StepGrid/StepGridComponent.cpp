// StepGridComponent.cpp -- the step grid's construction, the lanes it shows (saved on the module), paging, the 15 Hz
// sync with the module, the hit test and the screen-reader text. Painting is StepGridPaint.cpp, editing
// StepGridEdit.cpp, keys StepGridKeys.cpp and every animation StepGridMotion.cpp.
#include "StepGridComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"
#include <cmath>

namespace synth::ui {

using namespace stepgrid;

namespace {

std::uint16_t lanesOf(const SequencerModuleBase& module) {
    const auto& state = module.getCardViewState();
    return sanitizeLanes(state.stepGridLanes.value_or(kDefaultSequencerLanes), module.isPoly());
}

} // namespace

int StepGridComponent::preferredHeightFor(SequencerModuleBase& module) {
    return viewHeightFor(lanesOf(module), module.isPoly());
}

StepGridComponent::StepGridComponent(SequencerModuleBase& module)
    : module_(module)
    , stepsParam_(dynamic_cast<juce::AudioParameterInt*>(findParameterByID(&module, sequencer_ids::kSteps)))
    , runParam_(dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&module, sequencer_ids::kRun)))
    , lanes_(lanesOf(module))
    , revealTarget_(lanes_)
    , lastSeen_(module.getPattern()) {
    setTitle("Step grid");
    setTooltip("Step grid: click a pad to turn a step on or off, drag in a lane to draw values. Arrow keys move, "
               "Up and Down change the value, Space turns the step on or off");
    setWantsKeyboardFocus(true);
    reveal_ = revealsOf(lanes_);
    seenGeneration_ = module.patternGeneration();

    juce::StringArray names;
    for (const auto lane : availableLanes(module.isPoly()))
        names.add(laneName(lane));
    chips_.setLanes(names);
    const auto available = availableLanes(module.isPoly());
    for (int i = 0; i < (int)available.size(); ++i)
        chips_.setShown(i, (lanes_ & sequencerLaneBit(available[(size_t)i])) != 0);
    chips_.onToggle = [this](int index, bool shown) {
        const auto lane = availableLanes(module_.isPoly())[(size_t)index];
        setLanes((std::uint16_t)(shown ? (lanes_ | sequencerLaneBit(lane)) : (lanes_ & ~sequencerLaneBit(lane))));
    };
    pages_.onViewPage = [this](int page) { setViewedPage(page); };
    pages_.onFollow = [this](bool follow) { follow_ = follow; };
    tools_.onTool = [this](StepGridTools::Tool tool) { applyTool(tool); };
    addAndMakeVisible(tools_);
    addAndMakeVisible(pages_);
    addAndMakeVisible(chips_);

    focusLane_ = laneShown(SequencerLane::Pitch) ? SequencerLane::Pitch : SequencerLane::Velocity;
    rebuildScale();
    syncSettings();
    seenActive_ = module.currentActiveStep.load();
    seenRunning_ = runParam_ != nullptr && runParam_->get();
    updatePages();
    updateToolAvailability();
}

StepGridComponent::~StepGridComponent() = default;

stepgrid::StepGridLayout StepGridComponent::currentLayout() const { return computeLayout(reveal_, getWidth()); }

int StepGridComponent::currentLayoutHeight() const {
    return computeLayout(settling_ ? revealsOf(revealTarget_) : reveal_, getWidth()).total;
}

void StepGridComponent::resized() { layoutParts(); }

void StepGridComponent::layoutParts() {
    const auto layout = currentLayout();
    tools_.setBounds(layout.tools);
    pages_.setBounds(layout.strip);
    chips_.setBounds(layout.chips.withTrimmedLeft(kGutterWidth));
    repaint();
}

double StepGridComponent::columnWidth() const {
    return std::max(1.0, (double)(getWidth() - kGutterWidth - kRightMargin) / (double)kColumns);
}

double StepGridComponent::columnLeft(int step) const {
    return (double)kGutterWidth + ((double)step - (double)slidePos_ * kColumns) * columnWidth();
}

int StepGridComponent::stepAtX(int x) const {
    if (x < kGutterWidth)
        return -1;
    const int step = (int)std::floor((double)slidePos_ * kColumns + (double)(x - kGutterWidth) / columnWidth());
    return step >= 0 && step < kMaxSequencerSteps ? step : -1;
}

StepGridComponent::Hit StepGridComponent::hitTest(juce::Point<int> p) const {
    const auto layout = currentLayout();
    const int step = stepAtX(p.x);
    Hit hit;
    if (step < 0)
        return hit;
    hit.step = step;
    if (layout.pads.contains(p)) {
        hit.kind = Hit::Kind::Pad;
        return hit;
    }
    for (int i = 0; i < kSequencerLaneCount; ++i) {
        const auto lane = (SequencerLane)i;
        if (laneShown(lane) && layout.visibleLane(lane).contains(p)) {
            hit.kind = Hit::Kind::Lane;
            hit.lane = lane;
            return hit;
        }
    }
    return hit;
}

juce::Rectangle<int> StepGridComponent::cellBounds(SequencerLane lane, int step) const {
    const auto rect = currentLayout().visibleLane(lane);
    if (rect.isEmpty())
        return {};
    const int left = juce::roundToInt(columnLeft(step));
    return {left, rect.getY(), juce::roundToInt(columnLeft(step + 1)) - left, rect.getHeight()};
}

juce::Rectangle<int> StepGridComponent::padBounds(int step) const {
    const auto row = currentLayout().pads;
    const int left = juce::roundToInt(columnLeft(step));
    return {left, row.getY(), juce::roundToInt(columnLeft(step + 1)) - left, row.getHeight()};
}

// The scale the module plays through: the song key while it follows, else its own Key and Scale. Everything the grid
// draws, snaps, picks chords from or fits to reads this one scale.
void StepGridComponent::rebuildScale() { scale_ = module_.resolvedScale(songKey_); }

int StepGridComponent::playedPitchOf(int step) const {
    const int written = module_.getPattern().steps[(size_t)juce::jlimit(0, kMaxSequencerSteps - 1, step)].pitch;
    return scale_.snapPitch(juce::jlimit(0, 127, written + transpose_));
}

void StepGridComponent::refreshPitchRange() {
    if (gesture_.active && gesture_.frozenRange.has_value()) {
        pitchRange_ = *gesture_.frozenRange;
        return;
    }
    const auto& pattern = module_.getPattern();
    std::vector<int> pitches;
    for (int i = 0; i < std::min(stepCount(), kMaxSequencerSteps); ++i) {
        pitches.push_back(playedPitchOf(i));
        pitches.push_back(pattern.steps[(size_t)i].pitch);
    }
    pitchRange_ = fitPitchRange(pitches);
}

int StepGridComponent::runningStep() const {
    return runParam_ != nullptr && runParam_->get() ? module_.currentActiveStep.load() : -1;
}

// The parameters the grid draws against: Steps (dimming, pages), Transpose, and the scale it plays through (its own Key
// and Scale, or the song key while it follows), which sets the played pitch.
void StepGridComponent::syncSettings() {
    auto* transpose = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(&module_, sequencer_ids::kTranspose));
    const int steps = stepCount();
    const int t = transpose != nullptr ? transpose->get() : 0;
    const auto resolved = module_.resolvedScale(songKey_);
    const int r = resolved.rootPitchClass;
    const auto mask = resolved.mask;
    const bool scaleChanged = r != seenRoot_ || mask != seenMask_;
    if (steps == seenSteps_ && t == seenTranspose_ && !scaleChanged)
        return;
    if (steps != seenSteps_ && seenSteps_ >= 0)
        startDim(seenSteps_);
    seenSteps_ = steps;
    seenTranspose_ = t;
    seenRoot_ = r;
    seenMask_ = mask;
    transpose_ = t;
    if (scaleChanged)
        rebuildScale();
    refreshPitchRange();
    updatePages();
    repaint();
}

// A pattern change that did not come from a drag here (undo, redo, a preset, the AI): the bars glide to it.
void StepGridComponent::syncPattern() {
    const int generation = module_.patternGeneration();
    if (generation == seenGeneration_ || gesture_.active)
        return;
    const auto from = lastSeen_;
    lastSeen_ = module_.getPattern();
    seenGeneration_ = generation;
    if (lastSeen_.customMask != seenMask_)
        syncSettings();
    refreshPitchRange();
    startGlide(from);
    repaint();
}

void StepGridComponent::poll() {
    syncPattern();
    syncSettings();
    updateToolAvailability();
    const bool running = runParam_ != nullptr && runParam_->get();
    const int active = module_.currentActiveStep.load();
    if (active == seenActive_ && running == seenRunning_)
        return;
    seenActive_ = active;
    seenRunning_ = running;
    if (running && active < stepCount()) {
        pages_.pulse(active / kColumns);
        if (follow_)
            setViewedPage(active / kColumns);
    }
    updatePages();
    repaint();
}

void StepGridComponent::updatePages() {
    const int steps = stepCount();
    const int count = pageCount(steps);
    if (page_ >= count)
        setViewedPage(count - 1);
    const int active = runningStep();
    pages_.setPages(count, page_, active >= 0 && active < steps ? active / kColumns : -1, steps);
}

void StepGridComponent::setViewedPage(int page) {
    page = juce::jlimit(0, pageCount(stepCount()) - 1, page);
    if (page == page_ && !pageAnim_.isRunning())
        return;
    page_ = page;
    startPageSlide((float)page);
    updatePages();
}

void StepGridComponent::setFollowing(bool follow) {
    follow_ = follow;
    pages_.getFollowPill().setToggleState(follow, juce::dontSendNotification);
}

// Shows `mask`, remembers it on the module (an unchanged default is stored as no state) and folds lanes in or out.
void StepGridComponent::setLanes(std::uint16_t mask) {
    mask = sanitizeLanes(mask, module_.isPoly());
    if (mask == lanes_)
        return;
    const auto from = lanes_;
    lanes_ = mask;
    auto state = module_.getCardViewState();
    state.stepGridLanes = mask == kDefaultSequencerLanes ? std::nullopt : std::optional<std::uint16_t>(mask);
    module_.setCardViewState(state);
    const auto available = availableLanes(module_.isPoly());
    for (int i = 0; i < (int)available.size(); ++i)
        chips_.setShown(i, (mask & sequencerLaneBit(available[(size_t)i])) != 0);
    if (!laneShown(focusLane_))
        focusLane_ = laneShown(SequencerLane::Pitch) ? SequencerLane::Pitch : SequencerLane::Velocity;
    startRevealMotion(from, mask);
}

juce::String StepGridComponent::describeFocus() const {
    const int steps = stepCount();
    const int step = focusStep_;
    const auto& entry = module_.getPattern().steps[(size_t)step];
    auto text = "Step " + juce::String(step + 1) + " of " + juce::String(steps) + ", " + noteName(playedPitchOf(step));
    if (module_.isPoly() && entry.chord != SequencerChord::Unison)
        text << ", chord " << labelOf(entry.chord, playedPitchOf(step), step).name;
    text << (entry.on ? ", on" : ", off");
    if (focusLane_ != SequencerLane::Pitch)
        text << ", " << laneValueText(focusLane_, entry);
    if (step >= steps)
        text << ", past the last step";
    return text;
}

std::unique_ptr<juce::AccessibilityHandler> StepGridComponent::createAccessibilityHandler() {
    return makeValueTextHandler(*this, [this] { return describeFocus(); });
}

void StepGridComponent::focusGained(FocusChangeType) { repaint(); }
void StepGridComponent::focusLost(FocusChangeType) { repaint(); }

} // namespace synth::ui
