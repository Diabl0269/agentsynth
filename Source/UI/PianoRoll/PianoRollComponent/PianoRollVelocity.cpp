// PianoRollComponent — the velocity strip's wiring and its toolbar controls: the Host the
// PianoRollVelocityLane child reads its sticks through and commits back through, the strip's layout
// band and remembered visibility, the header's exact-value box, and Humanize. The strip itself
// (painting, hit-testing, gestures) is PianoRollVelocityLane (UI/PianoRoll/VelocityLane/); it never
// sees this class. See docs/timeline/piano-roll.md#velocity-strip.

#include "PianoRollComponent.h"

#include "AppUndoManager.h"
#include "PianoRollInternal.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/PianoRoll/VelocityLane/PianoRollVelocityLane.h"
#include "UI/PianoRoll/VelocityLane/VelocityLaneMath.h"
#include "UI/PianoRoll/VelocityLane/VelocityLaneSlide.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

using namespace synth::ui::detail;

namespace {
// The group frame: 2 px of padding inside its outline, chips and box 14 px tall inside an 18 px
// frame (the header is 20 px, its last row is the toolbar's bottom rule).
constexpr int kGroupPad = 2;
constexpr int kGroupInsetY = 1;
constexpr int kGroupContentInsetY = 3;
constexpr int kVelocityChipWidth = 60;
constexpr int kHumanizeChipWidth = 60;
constexpr int kCaptionWidth = 22;
constexpr int kValueBoxWidth = 36;
} // namespace

// Called once from the constructor. The lane's Host reads the notes through effectiveGeometryFor —
// the SAME function paintNote reads — so a stick and its note can never disagree mid-gesture: the
// strip's own preview (velocityPreview_), a Ctrl-drag scrub on the notes and a move drag all show
// up in both at once. The sticks' colour comes from resolveNoteColourFor for the same reason.
void PianoRollComponent::initVelocityControls() {
    velocityLane_ = std::make_unique<PianoRollVelocityLane>();
    velocityLaneHeight_ = PianoRollVelocityLane::kDefaultHeight;
    addAndMakeVisible(*velocityLane_);

    PianoRollVelocityLane::Host host;
    host.sticks = [this] {
        std::vector<PianoRollVelocityLane::Stick> sticks;
        const auto* clip = (doc_ != nullptr && clipId_.isValid()) ? doc_->getClip(clipId_) : nullptr;
        if (clip == nullptr)
            return sticks;
        const int laneX = velocityLane_->getX();
        for (const auto& note : clip->notes) {
            const auto geom = effectiveGeometryFor(note);
            const bool selected = selection_.contains(note.id);
            const int x = (int)std::llround(beatToX(clip->startBeat + geom.startBeat)) - laneX;
            const auto colour = resolveNoteColourFor(geom.pitch, geom.velocity, selected, note.muted).fill;
            sticks.push_back({note.id, x, geom.velocity, selected, colour});
        }
        return sticks;
    };
    host.gutterWidth = [this] { return leftGutterWidth(); };
    host.isDrawToolActive = [this] { return activeTool_ == EditTool::Draw; };
    host.onPreview = [this](const std::map<synth::NoteId, int>& preview) {
        velocityPreview_ = preview;
        repaint(); // the notes recolour live, not just the sticks
    };
    host.onCommit = [this](const std::vector<std::pair<synth::NoteId, int>>& changes) {
        velocityPreview_.clear();
        commitVelocities(changes);
    };
    host.onCancel = [this] {
        if (velocityPreview_.empty())
            return;
        velocityPreview_.clear();
        repaint();
    };
    host.onResizeRequest = [this](int desired) { onVelocityLaneResized(desired); };
    host.onResizeCommitted = [this](int desired) { onVelocityLaneResizeCommitted(desired); };
    velocityLane_->setHost(std::move(host));

    addAndMakeVisible(velocityBox_);
    velocityBox_.setComponentID("pianoRollVelocityBox");
    velocityBox_.setTitle("Velocity of selected notes");
    velocityBox_.setDescription("Type a velocity from 1 to 127 and press Return to set it on the selected notes, or on "
                                "every note in the clip when none is selected");
    velocityBox_.setTooltip("Velocity of the selected notes. Type 1 to 127 and press Return");
    velocityBox_.setInputRestrictions(3, "0123456789");
    velocityBox_.setJustification(juce::Justification::centred);
    velocityBox_.setSelectAllWhenFocused(true);
    velocityBox_.setIndents(2, 0);
    velocityBox_.setFont(
        juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
    velocityBox_.onReturnKey = [this] {
        applyVelocityValueText(velocityBox_.getText());
        if (isShowing())
            grabKeyboardFocus(); // typing is done: the roll's own keys work again
    };
    velocityBox_.onEscapeKey = [this] {
        syncVelocityValueBox();
        if (isShowing())
            grabKeyboardFocus();
    };
    velocityBox_.onFocusLost = [this] { syncVelocityValueBox(); }; // an unapplied edit reverts

    // Every selection change, from any path (a click, a marquee, an arrow key, a test reaching the
    // model directly), keeps the box showing the selection's common velocity.
    selection_.onChange = [this] {
        syncVelocityValueBox();
        refreshAccessibilityValue();
        repaint(); // the focused note's ring follows the selection
    };
    syncVelocityValueBox();
}

// The remembered height while shown, re-clamped to [kMinHeight, half the canvas band] at every
// layout (so a roll that got shorter never keeps a strip that swallows the grid), and never more
// than the band has room for at all (a tiny test roll).
// Mid-slide it is round(progress * full): the grid gives up only what the strip has risen so far.
int PianoRollComponent::velocityLaneHeightPx() const noexcept {
    return VelocityLaneSlide::revealedHeight(velocityLaneSlide_.progress(), velocityLaneFullHeightPx());
}

// The strip's own height, whatever the slide is showing: the lane component always keeps this size.
int PianoRollComponent::velocityLaneFullHeightPx() const noexcept {
    const int band = getHeight() - canvasTop();
    const int clamped = velocitylane::clampLaneHeight(velocityLaneHeight_, band, PianoRollVelocityLane::kMinHeight);
    return juce::jlimit(0, clamped, band);
}

int PianoRollComponent::getVelocityLaneHeight() const noexcept { return velocityLaneHeight_; }

// Stores `height` (floored at kMinHeight; the upper bound is layout's, since the roll may not have
// its size yet when the properties file is restored) and re-lays out; a no-op when unchanged.
void PianoRollComponent::setVelocityLaneHeight(int height) {
    height = std::max(PianoRollVelocityLane::kMinHeight, height);
    if (height == velocityLaneHeight_)
        return;
    velocityLaneHeight_ = height;
    resized();
    repaint();
}

int PianoRollComponent::clampedVelocityLaneHeight(int desiredHeight) const noexcept {
    return velocitylane::clampLaneHeight(desiredHeight, getHeight() - canvasTop(), PianoRollVelocityLane::kMinHeight);
}

void PianoRollComponent::onVelocityLaneResized(int desiredHeight) {
    setVelocityLaneHeight(clampedVelocityLaneHeight(desiredHeight));
}

// Release of the top-edge drag: remember the height it settled on (and only when the gesture moved).
void PianoRollComponent::onVelocityLaneResizeCommitted(int desiredHeight) {
    setVelocityLaneHeight(clampedVelocityLaneHeight(desiredHeight));
    if (propertiesFile_ == nullptr)
        return;
    propertiesFile_->setValue(velocityLaneHeightKey(), velocityLaneHeight_);
    propertiesFile_->saveIfNeeded();
}

// Called from resized() right after the Scale-filter chip is carved: the Velocity toggle, the
// Humanize action and the value box (captioned "Set") continue the chip row inside ONE rounded
// frame, so the three velocity controls read as one group. The strip is carved from the canvas
// band's BOTTOM before the scale panel, keys column and grid are laid out from what is left, so all
// three stop above it.
void PianoRollComponent::layoutVelocityControls(juce::Rectangle<int>& header, juce::Rectangle<int>& canvas) {
    header.removeFromLeft(4);
    const int groupWidth =
        kGroupPad + kVelocityChipWidth + 2 + kHumanizeChipWidth + 2 + kCaptionWidth + kValueBoxWidth + kGroupPad;
    auto group = header.removeFromLeft(groupWidth);
    velocityGroupBounds_ = group.reduced(0, kGroupInsetY);
    auto inner = velocityGroupBounds_.reduced(kGroupPad, kGroupContentInsetY - kGroupInsetY);
    velocityChipBounds_ = inner.removeFromLeft(kVelocityChipWidth);
    inner.removeFromLeft(2);
    humanizeChipBounds_ = inner.removeFromLeft(kHumanizeChipWidth);
    inner.removeFromLeft(2);
    velocityCaptionBounds_ = inner.removeFromLeft(kCaptionWidth);
    velocityBox_.setBounds(inner.removeFromLeft(kValueBoxWidth));

    // The lane keeps its FULL height and its top sits at the grid's new bottom, so mid-slide it
    // reaches below the roll's bottom edge and the parent clips it: the strip rises with its
    // content revealed, never squashed. It stays visible for the whole slide, hidden only once a
    // close has finished (progress 0).
    canvas.removeFromBottom(velocityLaneHeightPx());
    velocityLane_->setBounds(canvas.getX(), canvas.getBottom(), canvas.getWidth(), velocityLaneFullHeightPx());
    velocityLane_->setVisible(velocityLaneVisible_ || velocityLaneSlide_.progress() > 0.0f);
}

// `velocityLaneVisible_` is the LOGICAL target (what the chip paints lit and what persists); the
// slide's progress is the visual state. Retargeting starts from the current progress, with the
// scale panel's duration and easing (see VelocityLaneSlide::start).
void PianoRollComponent::setVelocityLaneVisible(bool visible, bool animate) {
    if (velocityLaneVisible_ == visible)
        return;
    if (!visible)
        clearVelocityPreview(); // hiding mid-gesture commits nothing
    velocityLaneVisible_ = visible;
    syncHeaderChipStates();
    if (propertiesFile_ != nullptr) {
        propertiesFile_->setValue(velocityLaneVisibleKey(), visible);
        propertiesFile_->saveIfNeeded();
    }
    const auto relayout = [this] {
        resized();
        repaint();
    };
    velocityLaneSlide_.start(*this, visible ? 1.0f : 0.0f, animate, kScalePanelAnimMs, relayout, relayout);
}

bool PianoRollComponent::isVelocityLaneVisible() const noexcept { return velocityLaneVisible_; }
void PianoRollComponent::toggleVelocityLane() { setVelocityLaneVisible(!velocityLaneVisible_); }

void PianoRollComponent::clearVelocityPreview() {
    if (velocityLane_ != nullptr)
        velocityLane_->cancelGesture();
    if (!velocityPreview_.empty()) {
        velocityPreview_.clear();
        repaint();
    }
}

// Exactly the Ctrl-drag velocity scrub's commit (PianoRollMouse.cpp's mouseUp): one
// recordTimelineChange around every setNoteVelocity, so however many notes a strip gesture, a typed
// value or Humanize touches, it is ONE undo step. An empty list writes nothing and records nothing.
void PianoRollComponent::commitVelocities(const std::vector<std::pair<synth::NoteId, int>>& targets) {
    if (doc_ == nullptr || targets.empty()) {
        repaint();
        return;
    }
    auto mutate = [this, targets] {
        for (const auto& [id, velocity] : targets)
            doc_->setNoteVelocity(id, velocitylane::clampVelocity(velocity));
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
    syncVelocityValueBox();
    repaint();
}

// The selection, or every note in the open clip when nothing is selected — the same
// selection-else-all rule Quantise follows.
std::vector<synth::NoteId> PianoRollComponent::velocityTargetIds() const {
    if (!selection_.isEmpty())
        return selection_.getSelected();
    std::vector<synth::NoteId> ids;
    if (const auto* clip = (doc_ != nullptr && clipId_.isValid()) ? doc_->getClip(clipId_) : nullptr)
        for (const auto& note : clip->notes)
            ids.push_back(note.id);
    return ids;
}

// Blank with nothing selected, the value when every selected note shares one, an em dash when they
// differ. Never sends a change notification, so it cannot feed back into an edit.
void PianoRollComponent::syncVelocityValueBox() {
    juce::String text;
    if (doc_ != nullptr && clipId_.isValid()) {
        std::optional<int> common;
        bool mixed = false;
        for (const auto id : selection_.getSelected()) {
            const auto* note = doc_->getNote(id);
            if (note == nullptr)
                continue;
            if (!common)
                common = note->velocity;
            else if (*common != note->velocity)
                mixed = true;
        }
        if (mixed)
            text = juce::String::fromUTF8("\xE2\x80\x94");
        else if (common)
            text = juce::String(*common);
    }
    if (velocityBox_.getText() != text)
        velocityBox_.setText(text, juce::dontSendNotification);
}

bool PianoRollComponent::applyVelocityValueText(const juce::String& text) {
    const auto trimmed = text.trim();
    const bool numeric = trimmed.isNotEmpty() && trimmed.length() <= 3 && trimmed.containsOnly("0123456789");
    const int value = numeric ? trimmed.getIntValue() : 0;
    if (value < velocitylane::kMinVelocity || value > velocitylane::kMaxVelocity || doc_ == nullptr ||
        !clipId_.isValid()) {
        syncVelocityValueBox(); // rejected: show the real value again
        return false;
    }
    std::vector<std::pair<synth::NoteId, int>> targets;
    for (const auto id : velocityTargetIds())
        if (const auto* note = doc_->getNote(id); note != nullptr && note->velocity != value)
            targets.emplace_back(id, value);
    commitVelocities(targets);
    syncVelocityValueBox();
    return true;
}

// Offsets are drawn from humanizeRandom_ in the targets' ascending-id order, so a seeded source
// (setHumanizeRandomSeed) reproduces the same result.
void PianoRollComponent::humanizeVelocities(int range) {
    if (doc_ == nullptr || !clipId_.isValid())
        return;
    std::vector<std::pair<synth::NoteId, int>> targets;
    for (const auto id : velocityTargetIds()) {
        const auto* note = doc_->getNote(id);
        if (note == nullptr)
            continue;
        const int next = velocitylane::humanizedVelocity(note->velocity, range, humanizeRandom_);
        if (next != note->velocity)
            targets.emplace_back(id, next);
    }
    commitVelocities(targets);
}

void PianoRollComponent::setHumanizeRandomSeed(juce::int64 seed) { humanizeRandom_.setSeed(seed); }

// The menu's item ids ARE the ranges, so the result is the argument. Async (a mouse-down is still
// unwinding) and guarded by a SafePointer, since the roll can go away while the menu is open.
void PianoRollComponent::showHumanizeMenu() {
    juce::PopupMenu menu;
    const auto plusMinus = juce::String::fromUTF8("\xC2\xB1");
    for (const int range : {5, 10, 20})
        menu.addItem(range, "Humanize " + plusMinus + juce::String(range));
    juce::Component::SafePointer<PianoRollComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(humanizeChipBounds_)),
                       [safe](int result) {
                           if (safe != nullptr && result > 0)
                               safe->humanizeVelocities(result);
                       });
}

juce::String PianoRollComponent::velocityTooltipText() const {
    const auto hint = shortcutHintFor(shortcuts_, "pianoRollToggleVelocityLane", velocityLaneToggleKey());
    juce::String text = "Velocity strip";
    if (hint.isNotEmpty())
        text += " (" + hint + ")";
    text += juce::String::fromUTF8(" \xE2\x80\x94 show or hide the velocity sticks under the notes");
    return text;
}

//==============================================================================
// ---- Simple accessors (see PianoRollComponent.h for each contract) ----
PianoRollVelocityLane& PianoRollComponent::getVelocityLane() noexcept { return *velocityLane_; }
juce::TextEditor& PianoRollComponent::getVelocityValueBox() noexcept { return velocityBox_; }
juce::Rectangle<int> PianoRollComponent::getVelocityChipBounds() const noexcept { return velocityChipBounds_; }
juce::Rectangle<int> PianoRollComponent::getHumanizeChipBounds() const noexcept { return humanizeChipBounds_; }
juce::Rectangle<int> PianoRollComponent::getVelocityGroupBounds() const noexcept { return velocityGroupBounds_; }
juce::Rectangle<int> PianoRollComponent::getVelocityCaptionBounds() const noexcept { return velocityCaptionBounds_; }
synth::ui::VelocityLaneSlide& PianoRollComponent::velocityLaneSlideForTest() noexcept { return velocityLaneSlide_; }
void PianoRollComponent::setVelocityLaneSlideProgressForTest(float progress) {
    velocityLaneSlide_.setProgress(progress);
    resized();
}

} // namespace synth::ui
