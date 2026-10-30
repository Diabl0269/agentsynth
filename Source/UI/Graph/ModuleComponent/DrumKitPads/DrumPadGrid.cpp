// DrumPadGrid.cpp -- the pad grid: geometry, the pad faces, the sliding selection ring, the mouse and keys, and a
// sound dropped on one pad.
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadGrid.h"
#include "Modules/SamplerModule.h"
#include "UI/DrumMachine/DrumLanes.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace synth::ui {

using synth::drums::Pad;
using synth::drums::PadKind;

namespace {

constexpr float kCorner = 5.0f;
constexpr float kDot = 8.0f;
constexpr int kInset = 6;
constexpr float kNameFont = 12.0f;

juce::String kindWords(const DrumKitModule& kit, int index) {
    switch (kit.pad(index).kind) {
    case PadKind::File:
        return "audio file";
    case PadKind::Instrument:
        return "instrument";
    case PadKind::Kit:
        return kit.padPlaysRecording(index) ? "kit sound" : "built-in sound";
    case PadKind::BuiltIn:
        break;
    }
    return "built-in sound";
}

// The pitch the pad's colour follows: the drum it plays, or its own note when it plays none.
int colourPitch(const Pad& pad) { return pad.sound >= 0 ? pad.sound : pad.note; }

// The file mark: one period of a wave.
void paintWaveMark(juce::Graphics& g, juce::Rectangle<float> r) {
    juce::Path wave;
    wave.startNewSubPath(r.getX(), r.getCentreY());
    for (int i = 1; i <= 12; ++i) {
        const float t = (float)i / 12.0f;
        wave.lineTo(r.getX() + t * r.getWidth(),
                    r.getCentreY() - std::sin(t * juce::MathConstants<float>::twoPi) * r.getHeight() * 0.5f);
    }
    g.strokePath(wave, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace

// One pad to a screen reader: a button named after the pad. The grid paints it and takes its mouse and keys, so the
// grid stays one Tab stop.
class DrumPadGrid::Cell : public juce::Component {
public:
    Cell(DrumPadGrid& grid, int index)
        : grid_(grid)
        , index_(index) {
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
    }
    void paint(juce::Graphics& g) override { grid_.paintPad(g, index_, getLocalBounds()); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(
            *this, juce::AccessibilityRole::button,
            juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,
                                                   [this] { grid_.pick(index_, true); }));
    }

private:
    DrumPadGrid& grid_;
    int index_;
};

int DrumPadGrid::heightFor(int count) {
    const int rows = (juce::jlimit(1, synth::drums::kMaxPads, count) + kColumns - 1) / kColumns;
    const int pad = count > kTallPadLimit ? kShortPad : kTallPad;
    return rows * pad + (rows - 1) * kGap;
}

DrumPadGrid::DrumPadGrid(DrumKitModule& kit)
    : kit_(kit) {
    setComponentID("drumPads");
    setTitle("Drum pads");
    setWantsKeyboardFocus(true);
    for (int i = 0; i < synth::drums::kMaxPads; ++i) {
        cells_[(size_t)i] = std::make_unique<Cell>(*this, i);
        addChildComponent(*cells_[(size_t)i]);
    }
    refresh();
}

DrumPadGrid::~DrumPadGrid() {
    slide_.stop();
    ringSlide_.stop();
    flash_.stop();
}

juce::Component& DrumPadGrid::padCell(int index) {
    return *cells_[(size_t)juce::jlimit(0, synth::drums::kMaxPads - 1, index)];
}

bool DrumPadGrid::isPadVisible(int index) const noexcept { return slotOf(index) >= 0; }

// The place pad `index` has among the pads drawn, or -1 while it is not shown.
int DrumPadGrid::slotOf(int pad) const noexcept {
    const auto it = std::lower_bound(order_.begin(), order_.end(), pad);
    return it != order_.end() && *it == pad ? (int)(it - order_.begin()) : -1;
}

void DrumPadGrid::refresh() {
    const int count = kit_.padCount();
    const bool countChanged = count != count_;
    count_ = count;
    std::array<juce::Rectangle<int>, synth::drums::kMaxPads> before;
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        before[(size_t)i] = cells_[(size_t)i]->isVisible() ? cells_[(size_t)i]->getBounds() : juce::Rectangle<int>();
    auto shown = kit_.shownPads();
    const bool orderChanged = shown != order_;
    order_ = std::move(shown);
    for (int i = 0; i < synth::drums::kMaxPads; ++i) {
        auto& cell = *cells_[(size_t)i];
        cell.setVisible(isPadVisible(i));
        if (isPadVisible(i))
            cell.setTitle(padTitle(i));
    }
    if (picked_ >= count_)
        pick(count_ - 1, false);
    if (countChanged || (orderChanged && !laidOut_)) {
        resized();
    } else if (orderChanged) {
        const bool animate = FadeVisibility::canAnimateIn(this) && getWidth() > 0;
        if (animate)
            startSlide(before);
        else
            resized();
    }
    laidOut_ = true;
    repaint();
}

juce::Rectangle<int> DrumPadGrid::padBounds(int index) const {
    const int slot = slotOf(index);
    if (slot < 0)
        return {};
    const int padH = order_.size() > (size_t)kTallPadLimit ? kShortPad : kTallPad;
    const int padW = (getWidth() - (kColumns - 1) * kGap) / kColumns;
    const int row = slot / kColumns;
    const int col = slot % kColumns;
    // The last column takes the pixels the division leaves, so the grid's right edge lines up with the card's.
    const int x = col * (padW + kGap);
    const int w = col == kColumns - 1 ? getWidth() - x : padW;
    return {x, row * (padH + kGap), w, padH};
}

int DrumPadGrid::padAt(juce::Point<int> p) const {
    for (const int pad : order_)
        if (padBounds(pad).contains(p))
            return pad;
    return -1;
}

juce::String DrumPadGrid::padTitle(int index) const {
    return kit_.pad(index).name + " pad, " + kindWords(kit_, index) + ", " + juce::String(slotOf(index) + 1) + " of " +
           juce::String(visibleCount());
}

juce::String DrumPadGrid::padTooltip(int index) const {
    return kit_.pad(index).name + ": click to play, Shift+click to pick without sound, right-click for its sound";
}

juce::String DrumPadGrid::getTooltip() {
    const int under = padAt(getMouseXYRelative());
    return under >= 0 ? padTooltip(under) : "Drum pads: click a pad to play it, Shift+click to pick it without sound";
}

bool DrumPadGrid::isFlashing(int index) const { return index == flashing_; }

void DrumPadGrid::layoutCells() {
    for (int i = 0; i < synth::drums::kMaxPads; ++i) {
        cells_[(size_t)i]->setAlpha(1.0f);
        cells_[(size_t)i]->setBounds(padBounds(i));
    }
}

void DrumPadGrid::resized() {
    if (sliding_ && getWidth() == slideWidth_) { // only the height changed (a row went): the pads keep sliding
        repaint();
        return;
    }
    slide_.stop();
    sliding_ = false;
    layoutCells();
    ringSlide_.stop();
    ringRect_ = padBounds(picked_).toFloat();
    repaint();
}

// ---- Pads leaving and coming back ----

// A pad that went shrinks over 110 ms (ease-in) while the pads after it slide up over 160 ms; one that came back
// grows in from its centre. Under Reduce Motion the pad fades in place over 80 ms and the others move at the end;
// with Animations Off (or off screen) refresh() lays out at once and never gets here.
void DrumPadGrid::startSlide(const std::array<juce::Rectangle<int>, synth::drums::kMaxPads>& before) {
    slide_.stop();
    slideReduced_ = prefersReducedMotion();
    bool any = false;
    for (int i = 0; i < synth::drums::kMaxPads; ++i) {
        const auto from = before[(size_t)i].toFloat();
        const auto to = padBounds(i).toFloat();
        moves_[(size_t)i] = {from, to};
        if (from.isEmpty() && to.isEmpty())
            continue;
        any = true;
        if (to.isEmpty())
            cells_[(size_t)i]->setVisible(true); // leaving: it shrinks away before it goes
    }
    if (!any) {
        resized();
        return;
    }
    sliding_ = true;
    slideWidth_ = getWidth();
    ringSlide_.stop();
    if (!isPadVisible(picked_))
        ringRect_ = {};
    slide_.start(
        updater_, slideReduced_ ? kReducedMs : kSlideMs, [](float t) { return t; }, [this](float t) { stepSlide(t); },
        [this] { finishSlide(); });
}

void DrumPadGrid::stepSlide(float t) {
    for (int i = 0; i < synth::drums::kMaxPads; ++i) {
        const auto [from, to] = moves_[(size_t)i];
        if (from.isEmpty() && to.isEmpty())
            continue;
        auto& cell = *cells_[(size_t)i];
        const auto scaled = [](juce::Rectangle<float> r, float k) {
            return r.withSizeKeepingCentre(r.getWidth() * k, r.getHeight() * k);
        };
        juce::Rectangle<float> rect;
        float alpha = 1.0f;
        if (to.isEmpty()) { // leaving
            const float shrink = easeInCubic(std::min(1.0f, t * (float)(kSlideMs / kShrinkMs)));
            rect = slideReduced_ ? from : scaled(from, 1.0f - shrink);
            alpha = slideReduced_ ? 1.0f - t : 1.0f;
        } else if (from.isEmpty()) { // coming back
            rect = slideReduced_ ? to : scaled(to, easeOutCubic(t));
            alpha = slideReduced_ ? t : 1.0f;
        } else {
            const float k = slideReduced_ ? 0.0f : easeOutCubic(t);
            rect = {from.getX() + (to.getX() - from.getX()) * k, from.getY() + (to.getY() - from.getY()) * k,
                    from.getWidth() + (to.getWidth() - from.getWidth()) * k,
                    from.getHeight() + (to.getHeight() - from.getHeight()) * k};
        }
        cell.setAlpha(alpha);
        cell.setBounds(rect.toNearestInt());
    }
    if (isPadVisible(picked_))
        ringRect_ = cells_[(size_t)picked_]->getBounds().toFloat();
    repaint();
}

void DrumPadGrid::finishSlide() {
    sliding_ = false;
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        cells_[(size_t)i]->setVisible(isPadVisible(i));
    layoutCells();
    ringRect_ = padBounds(picked_).toFloat();
    repaint();
}

// ---- Picking and playing ----

void DrumPadGrid::pick(int index, bool play) {
    index = juce::jlimit(0, std::max(0, count_ - 1), index);
    picked_ = index;
    if (play) {
        kit_.auditionPad(index);
        auditioned_ = index;
        // The flash is feedback that the click sounded; with nothing animating (off screen, Animations Off) the sound
        // is the feedback.
        if (FadeVisibility::canAnimateIn(this)) {
            flashing_ = index;
            flash_.start(
                updater_, kFlashMs, [](float t) { return t; }, [](float) {},
                [this] {
                    flashing_ = -1;
                    repaint();
                });
        }
    }
    moveRingTo(index, true);
    if (onPicked)
        onPicked(index, play);
}

// The ring slides to the new pad; under Reduce Motion or Animations Off it moves at once.
void DrumPadGrid::moveRingTo(int index, bool animate) {
    const auto target = padBounds(index).toFloat();
    if (!animate || ringRect_.isEmpty() || target.isEmpty() || prefersReducedMotion() ||
        !FadeVisibility::canAnimateIn(this)) {
        ringSlide_.stop();
        ringRect_ = target;
        repaint();
        return;
    }
    const auto from = ringRect_;
    ringSlide_.start(updater_, kRingSlideMs, easeOutCubic, [this, from, target](float t) {
        ringRect_ = {from.getX() + (target.getX() - from.getX()) * t, from.getY() + (target.getY() - from.getY()) * t,
                     from.getWidth() + (target.getWidth() - from.getWidth()) * t,
                     from.getHeight() + (target.getHeight() - from.getHeight()) * t};
        repaint();
    });
}

void DrumPadGrid::mouseDown(const juce::MouseEvent& e) {
    const int index = padAt(e.getPosition());
    if (index < 0)
        return;
    if (e.mods.isPopupMenu()) {
        pick(index, false);
        if (onContextMenu)
            onContextMenu(index, false);
        return;
    }
    pick(index, !e.mods.isShiftDown());
}

bool DrumPadGrid::showContextMenuForKeyboardFocus() {
    if (count_ <= 0 || !onContextMenu)
        return false;
    onContextMenu(picked_, true);
    return true;
}

// ---- A sound dropped on a pad ----

// JUCE finds a drop target by hit-testing from the window down, then asks it with the drag's starting details (not
// the pointer's position here). So the gaps stop hit-testing while a sound is dragged: over a gap the drop goes to the
// card and on to the canvas, as it did before pads took sounds, and over a pad the grid takes it. The Sounds view
// ends its drag state on the release before the container drops, so the container's own drag counts too.
bool DrumPadGrid::hitTest(int x, int y) {
    if (isSoundDragLive())
        return padAt({x, y}) >= 0;
    return juce::Component::hitTest(x, y);
}

// Every container up the tree: the canvas has one of its own, and the Sounds view drags through the window's.
bool DrumPadGrid::isSoundDragLive() const {
    if (SoundDragState::isActive())
        return true;
    for (auto* c = getParentComponent(); c != nullptr; c = c->getParentComponent())
        if (auto* container = dynamic_cast<juce::DragAndDropContainer*>(c);
            container != nullptr && container->isDragAndDropActive() &&
            isSoundPayload(container->getCurrentDragDescription()))
            return true;
    return false;
}

// One audio file, from the Sounds view or the Finder, like a Sampler card's drop.
bool DrumPadGrid::isInterestedInFileDrag(const juce::StringArray& files) {
    return files.size() == 1 && SamplerModule::isSupportedAudioFile(juce::File(files[0]));
}

void DrumPadGrid::setDropTarget(int index) {
    if (index == dropTarget_)
        return;
    dropTarget_ = index;
    repaint();
    if (onDropTargetChanged)
        onDropTargetChanged();
}

void DrumPadGrid::fileDragEnter(const juce::StringArray&, int x, int y) { setDropTarget(padAt({x, y})); }

void DrumPadGrid::fileDragMove(const juce::StringArray&, int x, int y) { setDropTarget(padAt({x, y})); }

void DrumPadGrid::fileDragExit(const juce::StringArray&) { setDropTarget(-1); }

void DrumPadGrid::filesDropped(const juce::StringArray& files, int x, int y) {
    const int index = padAt({x, y});
    setDropTarget(-1);
    if (index < 0 || files.isEmpty())
        return;
    pick(index, false);
    if (onFileDropped)
        onFileDropped(index, juce::File(files[0]));
}

// Left/Right step one pad, Up/Down one row, Home/End jump to the ends: all pick without a sound. Space and Return play
// the picked pad and H flips its Shown switch. Every other key, and any key with Cmd, Ctrl or Alt, passes on to the
// card and the app's shortcuts. The steps count the pads drawn, so a hidden pad is skipped.
bool DrumPadGrid::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    if (key.getTextCharacter() == 'h' || key.getTextCharacter() == 'H') {
        if (onToggleShown)
            onToggleShown(picked_);
        return true;
    }
    return pickKey(key);
}

bool DrumPadGrid::pickKey(const juce::KeyPress& key) {
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey) {
        pick(picked_, true);
        return true;
    }
    // The place of the picked pad among the pads drawn; a hidden one sits between its neighbours.
    const bool drawn = isPadVisible(picked_);
    const int here =
        drawn ? slotOf(picked_) : (int)(std::lower_bound(order_.begin(), order_.end(), picked_) - order_.begin());
    int slot = -1;
    if (code == juce::KeyPress::leftKey)
        slot = here - 1;
    else if (code == juce::KeyPress::rightKey)
        slot = drawn ? here + 1 : here;
    else if (code == juce::KeyPress::upKey)
        slot = here - kColumns;
    else if (code == juce::KeyPress::downKey)
        slot = drawn ? here + kColumns : here + kColumns - 1;
    else if (code == juce::KeyPress::homeKey)
        slot = 0;
    else if (code == juce::KeyPress::endKey)
        slot = visibleCount() - 1;
    else
        return false;
    if (visibleCount() == 0)
        return true;
    const int target = order_[(size_t)juce::jlimit(0, visibleCount() - 1, slot)];
    if (target != picked_)
        pick(target, false);
    return true;
}

// ---- Painting ----

void DrumPadGrid::paintPad(juce::Graphics& g, int index, juce::Rectangle<int> area) const {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto& pad = kit_.pad(index);
    const auto r = area.toFloat().reduced(0.5f);
    g.setColour(isFlashing(index) ? c.surfaceHi.overlaidWith(c.accent.withAlpha(0.35f)) : c.surfaceHi);
    g.fillRoundedRectangle(r, kCorner);
    g.setColour(c.border);
    g.drawRoundedRectangle(r, kCorner, 1.0f);

    const auto top = area.reduced(kInset).removeFromTop(10).toFloat();
    g.setColour(drumLaneForPitch(colourPitch(pad)).colour);
    g.fillEllipse(top.getX(), top.getCentreY() - kDot * 0.5f, kDot, kDot);
    g.setColour(c.accent);
    if (pad.kind == PadKind::File)
        paintWaveMark(g, top.withLeft(top.getRight() - 12.0f).reduced(0.0f, 1.0f));
    else if (pad.kind == PadKind::Instrument)
        g.drawRect(top.withLeft(top.getRight() - 9.0f).withSizeKeepingCentre(9.0f, 9.0f), 1.4f);

    g.setColour(c.textPrimary);
    g.setFont(synth::theme::AppLookAndFeel::uiFont(kNameFont));
    g.drawText(pad.name, area.reduced(kInset, 0).removeFromBottom(kInset + 14).removeFromTop(14),
               juce::Justification::centredLeft, true);
}

// The picked pad's accent ring (sliding between pads) and, while the grid holds keyboard focus, the focus ring
// outside it.
void DrumPadGrid::paintOverChildren(juce::Graphics& g) {
    if (count_ <= 0)
        return;
    const auto& c = synth::theme::themeOf(*this).colors;
    if (!ringRect_.isEmpty()) { // none while the picked pad is hidden
        g.setColour(c.accent);
        g.drawRoundedRectangle(ringRect_.reduced(1.0f), kCorner, 2.0f);
    }
    paintDropOutlines(g);
    if (hasKeyboardFocus(false))
        paintFocusRingAlways(g, getLocalBounds().toFloat(), *this, kCorner);
}

// While a sound is dragged every pad gets a faint outline; the pad under the pointer gets the accent one.
void DrumPadGrid::paintDropOutlines(juce::Graphics& g) const {
    if (!isShowingSoundOutline())
        return;
    const auto& c = synth::theme::themeOf(*this).colors;
    for (int i = 0; i < count_; ++i) {
        const bool target = i == dropTarget_;
        g.setColour(target ? c.accent : c.accent.withAlpha(0.35f));
        g.drawRoundedRectangle(padBounds(i).toFloat().reduced(target ? 1.5f : 1.0f), kCorner, target ? 2.5f : 1.0f);
    }
}

// ---- Screen reader and test mode ----

std::unique_ptr<juce::AccessibilityHandler> DrumPadGrid::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

void DrumPadGrid::addTestModeState(juce::DynamicObject& into) const {
    into.setProperty("padCount", count_);
    into.setProperty("visiblePads", visibleCount());
    into.setProperty("pickedPad", picked_ + 1);
    // auditionedPad is the last pad this grid asked to play; lastPlayedPad is set by the audio thread, so it stays 0
    // in a test-mode instance, which has no audio device.
    into.setProperty("auditionedPad", auditioned_ + 1);
    into.setProperty("lastPlayedPad", kit_.lastPlayedPad() + 1);
    const auto& pad = kit_.pad(picked_);
    into.setProperty("pickedName", pad.name);
    into.setProperty("pickedKind", kindWords(kit_, picked_));
    into.setProperty("pickedSound", pad.sound);
    into.setProperty("soundOutline", isShowingSoundOutline());
    into.setProperty("dropTargetPad", dropTarget_ + 1);
}

} // namespace synth::ui
