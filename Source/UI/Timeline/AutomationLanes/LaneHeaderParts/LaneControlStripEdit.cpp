// Concern: the lane header control strip's edit mode -- entering and leaving it, the tray that slides down under the
// header, showing/hiding a control (click, Space, drag between header and tray), reordering (drag sideways, Alt+Left/
// Right) and the keys of the mode.
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlStrip.h"

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kDragThreshold = 4; // px the pointer must travel before a press on a chip is a drag
} // namespace

LaneControlTray::LaneControlTray(LaneControlStrip& strip)
    : strip_(strip) {
    setComponentID("laneControlTray");
    done_.setTitle("Done editing controls");
    done_.setTooltip("Leave edit mode (Esc)");
    done_.setMouseClickGrabsKeyboardFocus(false);
    done_.setWantsKeyboardFocus(true);
    done_.onClick = [this] { strip_.exitEdit(); };
    addAndMakeVisible(done_);
    setInterceptsMouseClicks(true, true);
}

juce::Rectangle<int> LaneControlTray::chipArea() const {
    return {kPad, 0, juce::jmax(0, getWidth() - 3 * kPad - kDoneWidth), LaneControlStrip::kTrayHeight};
}

void LaneControlTray::resized() {
    done_.setBounds(getWidth() - kPad - kDoneWidth, (LaneControlStrip::kTrayHeight - kDoneHeight) / 2, kDoneWidth,
                    kDoneHeight);
}

// Drawn at its full height whatever the slide has revealed: the bounds clip it.
void LaneControlTray::paint(juce::Graphics& g) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    const auto shape = juce::Rectangle<float>(0.0f, 0.0f, (float)getWidth(),
                                              (float)(strip_.trayRows() * LaneControlStrip::kTrayHeight));
    juce::Path path;
    path.addRoundedRectangle(shape.getX(), shape.getY(), shape.getWidth(), shape.getHeight(), 6.0f, 6.0f, false, false,
                             true, true);
    g.setColour(colours.bg1);
    g.fillPath(path);
    g.setColour(colours.border.interpolatedWith(colours.accent, 0.55f));
    juce::Path ring;
    ring.addRoundedRectangle(shape.getX() + 1.0f, shape.getY(), shape.getWidth() - 2.0f, shape.getHeight() - 1.0f, 5.0f,
                             5.0f, false, false, true, true);
    g.strokePath(ring, juce::PathStrokeType(1.0f));
}

// Esc anywhere in the tray (the Done button passes it up) leaves edit mode.
bool LaneControlTray::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        strip_.exitEdit();
        return true;
    }
    return false;
}

// ---- Entering and leaving ----

juce::Component* LaneControlStrip::dragLayer() const {
    auto* owner = getParentComponent();
    return owner != nullptr ? owner->getParentComponent() : nullptr;
}

void LaneControlStrip::enterEdit() {
    if (editing_)
        return;
    closeRow();
    if (set_ != nullptr)
        set_->claimEditing(this);
    editing_ = true;
    if (tray_ == nullptr) {
        tray_ = std::make_unique<LaneControlTray>(*this);
        tray_->setAlpha(0.0f);
    }
    if (auto* host = dragLayer(); host != nullptr && tray_->getParentComponent() != host)
        host->addChildComponent(*tray_);
    more_->setPencil(true);
    more_->setEditing(true);
    placeEditChips();
    animateNextLayout_ = true;
    if (onPreferredWidthChanged)
        onPreferredWidthChanged();
    layoutChips(true);
    animateNextLayout_ = false;
    animateTray(true);
    if (auto order = editOrder(); !order.empty())
        order.front()->grabKeyboardFocus();
    else
        tray_->getDoneButton().grabKeyboardFocus();
}

void LaneControlStrip::exitEdit() {
    if (!editing_)
        return;
    if (dragging_ && pressed_ != nullptr) {
        pressed_->setAlpha(1.0f); // an interrupted drag changes nothing
        ghost_.reset();
    }
    dragging_ = false;
    dragGapAt_.reset();
    pressed_ = nullptr;
    editing_ = false;
    if (set_ != nullptr)
        set_->releaseEditing(this);
    more_->setPencil(false);
    more_->setEditing(false);
    for (auto& [control, chip] : chips_) {
        chip->setEditing(false);
        chip->setDimmed(false);
    }
    animateNextLayout_ = true;
    if (onPreferredWidthChanged)
        onPreferredWidthChanged();
    layoutChips(true);
    animateNextLayout_ = false;
    animateTray(false);
}

// In edit mode the hidden controls sit in the tray, dimmed; every shown one is the strip's.
void LaneControlStrip::placeEditChips() {
    if (tray_ == nullptr)
        return;
    for (auto& [control, chip] : chips_) {
        if (std::find(shown_.begin(), shown_.end(), control) != shown_.end())
            continue;
        if (chip->getParentComponent() != tray_.get())
            tray_->addChildComponent(*chip);
        chip->setEditing(editing_);
        chip->setDimmed(true);
        chip->setVisible(true);
    }
}

void LaneControlStrip::layoutTrayChips() {
    if (tray_ == nullptr)
        return;
    std::vector<LaneControlChip*> inTray;
    for (const auto control : laneControlCatalogue(kind_)) {
        auto* chip = chipFor(control);
        if (chip->getParentComponent() == tray_.get() && !(dragging_ && chip == pressed_))
            inTray.push_back(chip);
    }
    const int perFirst = trayChipsPerRow(true);
    const int perOther = trayChipsPerRow(false);
    const int inset = (kTrayHeight - chipSize_) / 2;
    for (int i = 0; i < (int)inTray.size(); ++i) {
        const int row = i < perFirst ? 0 : 1 + (i - perFirst) / perOther;
        const int col = i < perFirst ? i : (i - perFirst) % perOther;
        inTray[(size_t)i]->setBounds(LaneControlTray::kPad + col * LaneControlTray::kPitch, row * kTrayHeight + inset,
                                     chipSize_, chipSize_);
    }
    if (const int rows = trayRows(); rows != laidRows_) { // the tray grows or shrinks with the hidden count
        laidRows_ = rows;
        placeTray(ownerBounds_);
        if (onTrayExtentChanged)
            onTrayExtentChanged();
    }
}

// How many chips one tray row holds: the first row also holds Done.
int LaneControlStrip::trayChipsPerRow(bool firstRow) const {
    const int width = ownerBounds_.getWidth() - 2 * LaneControlTray::kPad -
                      (firstRow ? LaneControlTray::kPad + LaneControlTray::kDoneWidth : 0);
    return juce::jmax(1, (width + LaneControlTray::kPitch - chipSize_) / LaneControlTray::kPitch);
}

// One 34 px row, and another for each time the hidden chips (and Done) no longer fit the rows so far.
int LaneControlStrip::trayRows() const {
    const int hidden = (int)laneControlCatalogue(kind_).size() - (int)shown_.size();
    const int first = trayChipsPerRow(true);
    if (ownerBounds_.getWidth() <= 0 || hidden <= first)
        return 1;
    const int other = trayChipsPerRow(false);
    return 1 + (hidden - first + other - 1) / other;
}

int LaneControlStrip::trayExtent() const {
    return (int)std::lround((double)(trayRows() * kTrayHeight + kTrayGap) * (double)trayOpen_);
}

void LaneControlStrip::placeTray(juce::Rectangle<int> ownerBounds) {
    const bool widthChanged = ownerBounds.getWidth() != ownerBounds_.getWidth();
    ownerBounds_ = ownerBounds;
    if (tray_ == nullptr)
        return;
    if (widthChanged && (editing_ || trayOpen_ > 0.0f))
        layoutTrayChips(); // the rows depend on the width
    laidRows_ = trayRows();
    const int height = (int)std::lround((double)(laidRows_ * kTrayHeight) * (double)trayOpen_);
    tray_->setBounds(ownerBounds.getX(), ownerBounds.getBottom() - 1, ownerBounds.getWidth(), height);
    tray_->setVisible(height > 0);
    tray_->toFront(false);
}

void LaneControlStrip::applyTrayOpen(float open) {
    trayOpen_ = juce::jlimit(0.0f, 1.0f, open);
    if (tray_ != nullptr)
        tray_->setAlpha(trayOpen_);
    placeTray(ownerBounds_);
    if (onTrayExtentChanged)
        onTrayExtentChanged();
    if (onEditLookChanged)
        onEditLookChanged();
}

// Closed for good: the hidden controls go back to being parked in the strip and the tray is gone from view.
void LaneControlStrip::landTray() {
    trayDriver_.stop(updater_);
    applyTrayOpen(trayTo_);
    if (trayOpen_ > 0.0f || editing_)
        return;
    for (auto& [control, chip] : chips_)
        if (tray_ != nullptr && chip->getParentComponent() == tray_.get()) {
            addChildComponent(*chip);
            chip->setVisible(false);
        }
    if (tray_ != nullptr)
        tray_->setVisible(false);
    layoutChips(false);
}

void LaneControlStrip::animateTray(bool open) {
    trayFrom_ = trayOpen_;
    trayTo_ = open ? 1.0f : 0.0f;
    if (!FadeVisibility::canAnimateIn(this) || prefersReducedMotion()) {
        landTray();
        return;
    }
    trayDriver_.start(
        updater_, kTrayMs, easeInOutCubic, [this](float t) { applyTrayOpen(trayFrom_ + (trayTo_ - trayFrom_) * t); },
        [this] { landTray(); });
}

void LaneControlStrip::stepForTest(float t) {
    if (trayDriver_.isRunning()) {
        if (t >= 1.0f)
            landTray();
        else
            applyTrayOpen(trayFrom_ + (trayTo_ - trayFrom_) * easeInOutCubic(t));
    }
    if (slideDriver_.isRunning()) {
        for (const auto& [chip, to] : slideTo_)
            chip->setBounds(t >= 1.0f ? to : AnimationDriver::lerpBounds(slideFrom_[chip], to, easeOutCubic(t)));
        if (t >= 1.0f) {
            slideDriver_.stop(updater_);
            slideFrom_.clear();
            slideTo_.clear();
        }
    }
    if (growDriver_.isRunning()) {
        if (t >= 1.0f)
            stopGrow();
        else
            for (auto* c : growing_)
                c->setVisualScale(c->getParentComponent() == this ? easeOutBackGrow(t) : easeOutCubic(t));
    }
    if (row_ != nullptr)
        row_->stepForTest(t);
}

// ---- Showing, hiding and ordering ----

std::vector<LaneControlChip*> LaneControlStrip::editOrder() const {
    std::vector<LaneControlChip*> order;
    for (const auto control : shown_)
        order.push_back(chipFor(control));
    for (const auto control : laneControlCatalogue(kind_))
        if (std::find(shown_.begin(), shown_.end(), control) == shown_.end())
            order.push_back(chipFor(control));
    return order;
}

void LaneControlStrip::focusChip(LaneControl control) {
    if (auto* chip = chipFor(control))
        chip->grabKeyboardFocus();
}

// The one place a change to the set goes through: this strip applies it, then the shared set saves it and every other
// strip of the kind follows.
void LaneControlStrip::commitShown(std::vector<LaneControl> next) {
    applyShown(next, isShowing());
    if (set_ != nullptr)
        set_->setShown(kind_, next);
}

void LaneControlStrip::toggleControl(LaneControl control) {
    auto next = shown_;
    const auto it = std::find(next.begin(), next.end(), control);
    if (it != next.end())
        next.erase(it);
    else
        next.push_back(control);
    commitShown(std::move(next));
    focusChip(control);
}

bool LaneControlStrip::moveControl(LaneControl control, int delta) {
    const auto it = std::find(shown_.begin(), shown_.end(), control);
    if (it == shown_.end())
        return false;
    const int from = (int)(it - shown_.begin());
    const int to = from + delta;
    if (to < 0 || to >= (int)shown_.size())
        return false;
    auto next = shown_;
    std::swap(next[(size_t)from], next[(size_t)to]);
    commitShown(std::move(next));
    focusChip(control);
    return true;
}

// Esc closes the "+" row or leaves edit mode. In edit mode Left/Right walk the chips, Alt+Left/Right move the
// focused chip along the header, and Space shows or hides it (Return does too, through the chip's click).
bool LaneControlStrip::editKey(LaneControlChip& chip, const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::escapeKey) && key.getModifiers().isAnyModifierKeyDown() == false) {
        if (editing_) {
            exitEdit();
            more_->grabKeyboardFocus();
            return true;
        }
        if (isRowOpen()) {
            closeRow();
            more_->grabKeyboardFocus();
            return true;
        }
        return false;
    }
    if (!editing_)
        return false;
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isShiftDown())
        return false;
    const bool left = key.isKeyCode(juce::KeyPress::leftKey);
    if (left || key.isKeyCode(juce::KeyPress::rightKey)) {
        const int step = left ? -1 : 1;
        if (mods.isAltDown()) {
            if (chip.getControl().has_value())
                moveControl(*chip.getControl(), step);
            return true;
        }
        const auto order = editOrder();
        if (order.empty())
            return true;
        const auto found = std::find(order.begin(), order.end(), &chip);
        int index = found == order.end() ? (int)order.size() : (int)(found - order.begin());
        index = juce::jlimit(0, (int)order.size() - 1, index + step);
        order[(size_t)index]->grabKeyboardFocus();
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::spaceKey) && !mods.isAltDown() && chip.getControl().has_value()) {
        toggleControl(*chip.getControl());
        return true;
    }
    return false;
}

// ---- Dragging a chip ----

void LaneControlStrip::chipPressed(LaneControlChip& chip, const juce::MouseEvent& e) {
    pressed_ = &chip;
    dragging_ = false;
    pressScreen_ = e.getScreenPosition();
    grabOffset_ = e.getPosition();
}

void LaneControlStrip::beginDrag(LaneControlChip& chip, const juce::MouseEvent&) {
    auto* layer = dragLayer();
    dragging_ = true;
    ghost_ = std::make_unique<LaneControlChip>(chip.getControl(), kind_);
    ghost_->setEditing(true);
    ghost_->setLifted(true);
    ghost_->setInterceptsMouseClicks(false, false);
    ghost_->setWantsKeyboardFocus(false);
    ghost_->setSize(chip.getWidth(), chip.getHeight());
    if (layer != nullptr)
        layer->addAndMakeVisible(*ghost_);
    chip.setAlpha(0.0f); // the chip stays where the press landed (mouse events keep coming to it); the ghost moves
    dragGapAt_.reset();
    const bool fromHeader = chip.getParentComponent() == this;
    // The line closes up where the chip was lifted from.
    if (fromHeader) {
        animateNextLayout_ = true;
        layoutChips(true);
        animateNextLayout_ = false;
    } else {
        layoutTrayChips();
    }
}

LaneControlStrip::DropZone LaneControlStrip::zoneAt(juce::Point<int> screenPos) const {
    if (auto* owner = getParentComponent(); owner != nullptr && owner->getScreenBounds().contains(screenPos))
        return DropZone::Header;
    if (tray_ != nullptr && tray_->isVisible() && tray_->getScreenBounds().contains(screenPos))
        return DropZone::Tray;
    return DropZone::None;
}

// Where in the line the dragged chip would land: after every other chip whose centre is left of the pointer.
int LaneControlStrip::insertionIndexAt(juce::Point<int> screenPos, LaneControl dragged) const {
    const int x = getLocalPoint(nullptr, screenPos).x;
    const auto list = fitting();
    const float p = effectivePitch((int)list.size() + 1);
    int index = 0;
    for (int i = 0; i < (int)list.size(); ++i)
        if (list[(size_t)i] != dragged &&
            (float)x >
                p * (float)(i + (dragGapAt_.has_value() && i >= *dragGapAt_ ? 1 : 0)) + (float)headerChipWidth() * 0.5f)
            index = i + 1;
    return juce::jlimit(0, (int)list.size(), index);
}

void LaneControlStrip::chipDragged(LaneControlChip& chip, const juce::MouseEvent& e) {
    if (pressed_ != &chip)
        return;
    const auto screen = e.getScreenPosition();
    if (!dragging_) {
        if (screen.getDistanceFrom(pressScreen_) < kDragThreshold)
            return;
        beginDrag(chip, e);
    }
    if (ghost_ != nullptr)
        if (auto* layer = ghost_->getParentComponent()) {
            ghost_->setTopLeftPosition(layer->getLocalPoint(nullptr, screen - grabOffset_));
            ghost_->toFront(false);
        }
    // Neighbours slide apart where the chip would land.
    const auto zone = zoneAt(screen);
    const auto gap =
        zone == DropZone::Header ? std::optional<int>(insertionIndexAt(screen, *chip.getControl())) : std::nullopt;
    if (gap != dragGapAt_) {
        dragGapAt_ = gap;
        animateNextLayout_ = true;
        layoutChips(true);
        animateNextLayout_ = false;
    }
}

void LaneControlStrip::chipReleased(LaneControlChip& chip, const juce::MouseEvent& e) {
    if (pressed_ != &chip)
        return;
    if (!dragging_) {
        pressed_ = nullptr;
        if (chip.getControl().has_value() && chip.getLocalBounds().contains(e.getPosition()))
            toggleControl(*chip.getControl()); // a click shows or hides it
        return;
    }
    finishDrag(chip, e.getScreenPosition());
}

// The drop: over the header the chip lands at the insertion point, over the tray it is hidden, anywhere else it goes
// back where it came from.
void LaneControlStrip::finishDrag(LaneControlChip& chip, juce::Point<int> screenPos) {
    const auto control = *chip.getControl();
    const auto origin = std::find(shown_.begin(), shown_.end(), control);
    const int originIndex = origin != shown_.end() ? (int)(origin - shown_.begin()) : -1;
    const auto zone = zoneAt(screenPos);
    const int insertAt = dragGapAt_.value_or(0);
    auto next = shown_;
    next.erase(std::remove(next.begin(), next.end(), control), next.end());
    if (zone == DropZone::Header)
        next.insert(next.begin() + std::min<size_t>((size_t)insertAt, next.size()), control);
    else if (zone == DropZone::None && originIndex >= 0)
        next.insert(next.begin() + std::min<size_t>((size_t)originIndex, next.size()), control);
    ghost_.reset();
    chip.setAlpha(1.0f);
    dragging_ = false;
    dragGapAt_.reset();
    pressed_ = nullptr;
    if (next == shown_) {
        animateNextLayout_ = true;
        layoutChips(true);
        animateNextLayout_ = false;
        return;
    }
    commitShown(std::move(next));
}

} // namespace synth::ui
