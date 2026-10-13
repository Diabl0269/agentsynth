// Concern: the lane header control strip -- its chips, their layout (overflow into the "+" row), the shown set and the
// "+" row. Edit mode (the tray, dragging, the keyboard) is in LaneControlStripEdit.cpp.
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlStrip.h"

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <set>

namespace synth::ui {

LaneControlStrip::LaneControlStrip(LaneControlKind kind, LaneControlSet* set, Actions actions)
    : kind_(kind)
    , set_(set)
    , actions_(std::move(actions))
    , updater_(this) {
    setComponentID("laneControlStrip");
    // Empty strip area belongs to the header (a press there focuses or drags the lane); only the chips take presses.
    setInterceptsMouseClicks(false, true);
    shown_ = set_ != nullptr ? set_->shown(kind_) : defaultLaneControls(kind_);
    for (const auto control : laneControlCatalogue(kind_)) {
        auto chip = std::make_unique<LaneControlChip>(control, kind_);
        wireChip(*chip);
        addChildComponent(*chip);
        chips_[control] = std::move(chip);
    }
    more_ = std::make_unique<LaneControlChip>(std::nullopt, kind_);
    wireChip(*more_);
    more_->onClick = [this] {
        if (editing_)
            exitEdit();
        else
            toggleRow();
    };
    addAndMakeVisible(*more_);
    if (set_ != nullptr)
        set_->addListener(this);
    refreshStates();
}

LaneControlStrip::~LaneControlStrip() {
    trayDriver_.stop(updater_);
    slideDriver_.stop(updater_);
    growDriver_.stop(updater_);
    if (set_ != nullptr) {
        set_->removeListener(this);
        set_->releaseEditing(this);
    }
}

void LaneControlStrip::wireChip(LaneControlChip& chip) {
    if (!chip.isMoreButton())
        chip.onClick = [this, &chip] {
            if (editing_)
                toggleControl(*chip.getControl());
            else
                perform(*chip.getControl(), chip);
        };
    chip.onEditPress = [this](LaneControlChip& c, const juce::MouseEvent& e) { chipPressed(c, e); };
    chip.onEditDrag = [this](LaneControlChip& c, const juce::MouseEvent& e) { chipDragged(c, e); };
    chip.onEditRelease = [this](LaneControlChip& c, const juce::MouseEvent& e) { chipReleased(c, e); };
    chip.onEditKey = [this](LaneControlChip& c, const juce::KeyPress& k) { return editKey(c, k); };
    chip.onContextMenu = [this](juce::Point<int> p) {
        if (onContextMenu)
            onContextMenu(p);
    };
}

void LaneControlStrip::perform(LaneControl control, juce::Component& anchor) {
    if (actions_.perform)
        actions_.perform(control, anchor);
}

void LaneControlStrip::refresh() {
    const auto wanted = set_ != nullptr ? set_->shown(kind_) : defaultLaneControls(kind_);
    if (wanted != shown_)
        applyShown(wanted, false);
    refreshStates();
}

void LaneControlStrip::refreshStates() {
    for (auto& [control, chip] : chips_) {
        chip->setEnabled(!actions_.isEnabled || actions_.isEnabled(control));
        chip->setLit(actions_.isLit && actions_.isLit(control));
        chip->setDetail(actions_.detail ? actions_.detail(control) : juce::String());
        chip->setIconColour(actions_.iconColour ? actions_.iconColour(control) : std::nullopt);
    }
}

void LaneControlStrip::laneControlsChanged(LaneControlKind kind) {
    if (kind != kind_ || set_ == nullptr)
        return;
    const auto wanted = set_->shown(kind_);
    if (wanted != shown_)
        applyShown(wanted, isShowing());
}

void LaneControlStrip::setChipSize(int size) {
    size = juce::jmax(10, size);
    if (size == chipSize_)
        return;
    chipSize_ = size;
    layoutChips(false);
}

// A chip is wider in edit mode (it carries a grip), and the pitch with it.
int LaneControlStrip::headerChipWidth() const { return editing_ ? chipSize_ + 4 : chipSize_; }

int LaneControlStrip::pitch() const { return headerChipWidth() + (editing_ ? 3 : kGap); }

int LaneControlStrip::preferredWidth() const { return (int)shown_.size() * pitch() + chipSize_; }

// Edit mode never hides a chip, so a long set squeezes its pitch (down to overlapping) instead.
float LaneControlStrip::effectivePitch(int count) const {
    float p = (float)pitch();
    if (editing_ && count > 0 && getWidth() > 0)
        p = std::min(p, (float)(getWidth() - chipSize_) / (float)count);
    return std::max(p, 10.0f);
}

// The shown controls that fit in the strip's width, in order; the rest are listed in the "+" row. The control being
// dragged is out of the line while its drag lasts.
std::vector<LaneControl> LaneControlStrip::fitting() const {
    std::vector<LaneControl> list;
    for (const auto control : shown_)
        if (!(dragging_ && pressed_ != nullptr && pressed_->getControl() == control))
            list.push_back(control);
    if (editing_ || getWidth() <= 0)
        return list;
    const int p = chipSize_ + kGap;
    if ((int)list.size() * p + chipSize_ > getWidth())
        list.resize((size_t)juce::jlimit(0, (int)list.size(), (getWidth() - chipSize_) / p));
    return list;
}

std::vector<LaneControl> LaneControlStrip::visibleControls() const { return fitting(); }

std::vector<LaneControl> LaneControlStrip::hiddenControls() const {
    const auto visible = editing_ ? shown_ : fitting();
    std::vector<LaneControl> hidden;
    for (const auto control : laneControlCatalogue(kind_))
        if (std::find(visible.begin(), visible.end(), control) == visible.end())
            hidden.push_back(control);
    return hidden;
}

LaneControlChip* LaneControlStrip::chipFor(LaneControl control) const {
    const auto it = chips_.find(control);
    return it != chips_.end() ? it->second.get() : nullptr;
}

std::map<LaneControlChip*, juce::Rectangle<int>> LaneControlStrip::targetBounds(std::optional<int> gapAt) const {
    std::map<LaneControlChip*, juce::Rectangle<int>> targets;
    const auto list = fitting();
    const float p = effectivePitch((int)list.size() + (gapAt.has_value() ? 1 : 0));
    const int y = (getHeight() - chipSize_) / 2;
    const int w = headerChipWidth();
    for (int i = 0; i < (int)list.size(); ++i) {
        const int slot = i + (gapAt.has_value() && i >= *gapAt ? 1 : 0);
        targets[chipFor(list[(size_t)i])] = {(int)std::lround(p * (float)slot), y, w, chipSize_};
    }
    const int moreSlot = (int)list.size() + (gapAt.has_value() ? 1 : 0);
    targets[more_.get()] = {(int)std::lround(p * (float)moreSlot), y, chipSize_, chipSize_};
    return targets;
}

void LaneControlStrip::resized() { layoutChips(std::exchange(animateNextLayout_, false)); }

// Puts every chip where it belongs: those that fit in the strip (sliding there when `animate`), the rest parked
// invisible -- or, in edit mode, in the tray.
void LaneControlStrip::layoutChips(bool animate) {
    std::set<LaneControlChip*> had;
    for (auto& [control, chip] : chips_)
        if (chip->getParentComponent() == this && chip->isVisible() && !chip->getBounds().isEmpty())
            had.insert(chip.get());
    if (more_->isVisible())
        had.insert(more_.get());
    const auto targets = targetBounds(dragGapAt_);
    for (auto& [control, chip] : chips_) {
        if (dragging_ && chip.get() == pressed_)
            continue;
        if (targets.count(chip.get()) > 0) {
            if (chip->getParentComponent() != this)
                addChildComponent(*chip);
            chip->setEditing(editing_);
            chip->setDimmed(false);
            chip->setVisible(true);
        } else if (editing_ || trayOpen_ > 0.0f) {
            continue; // in the tray, which lays them out
        } else {
            if (chip->getParentComponent() != this)
                addChildComponent(*chip);
            chip->setVisible(false);
        }
    }
    const bool animateSlide = animate && FadeVisibility::canAnimateIn(this) && !prefersReducedMotion();
    slideDriver_.stop(updater_);
    slideFrom_.clear();
    slideTo_.clear();
    for (const auto& [chip, rect] : targets) {
        if (animateSlide && had.count(chip) > 0 && chip->getBounds() != rect) {
            slideFrom_[chip] = chip->getBounds();
            slideTo_[chip] = rect;
        } else {
            chip->setBounds(rect);
        }
    }
    if (!slideTo_.empty())
        slideDriver_.start(
            updater_, kSlideMs, easeOutCubic,
            [this](float t) {
                for (const auto& [chip, to] : slideTo_)
                    chip->setBounds(AnimationDriver::lerpBounds(slideFrom_[chip], to, t));
            },
            [this] {
                for (const auto& [chip, to] : slideTo_)
                    chip->setBounds(to);
                slideDriver_.stop(updater_);
                slideFrom_.clear();
                slideTo_.clear();
            });
    if (editing_ || trayOpen_ > 0.0f)
        layoutTrayChips();
}

// A new shown set: chips whose membership changed in edit mode grow in, the "+" row (now stale) closes, and the owner
// is told the width it wants changed.
void LaneControlStrip::applyShown(const std::vector<LaneControl>& next, bool animate) {
    closeRow();
    std::vector<LaneControlChip*> changed;
    for (const auto control : laneControlCatalogue(kind_)) {
        const bool was = std::find(shown_.begin(), shown_.end(), control) != shown_.end();
        const bool now = std::find(next.begin(), next.end(), control) != next.end();
        if (was != now)
            changed.push_back(chipFor(control));
    }
    shown_ = next;
    if (editing_) {
        placeEditChips();
        for (auto* chip : changed)
            if (animate)
                growChip(*chip);
    }
    animateNextLayout_ = animate;
    layoutChips(animate);
    animateNextLayout_ = false;
    if (onPreferredWidthChanged)
        onPreferredWidthChanged();
}

void LaneControlStrip::growChip(LaneControlChip& chip) {
    if (!FadeVisibility::canAnimateIn(this) || prefersReducedMotion())
        return;
    if (std::find(growing_.begin(), growing_.end(), &chip) == growing_.end())
        growing_.push_back(&chip);
    chip.setVisualScale(0.0f);
    growDriver_.start(
        updater_, kGrowMs, [](float t) { return t; },
        [this](float t) {
            for (auto* c : growing_)
                c->setVisualScale(c->getParentComponent() == this ? easeOutBackGrow(t) : easeOutCubic(t));
        },
        [this] { stopGrow(); });
}

void LaneControlStrip::stopGrow() {
    for (auto* c : growing_)
        c->setVisualScale(1.0f);
    growing_.clear();
    growDriver_.stop(updater_);
}

// ---- The "+" row ----

juce::Point<int> LaneControlStrip::rowAnchor() const {
    auto* overlay = set_ != nullptr ? set_->getOverlayParent() : nullptr;
    auto* owner = getParentComponent();
    if (overlay == nullptr || owner == nullptr)
        return {};
    return overlay->getLocalPoint(owner, juce::Point<int>(owner->getWidth() + 2, 0));
}

void LaneControlStrip::repositionRow() {
    if (isRowOpen())
        row_->reposition(rowAnchor());
}

void LaneControlStrip::toggleRow() {
    if (isRowOpen())
        closeRow();
    else
        openRow();
}

void LaneControlStrip::openRow() {
    auto* overlay = set_ != nullptr ? set_->getOverlayParent() : nullptr;
    if (overlay == nullptr || editing_)
        return;
    if (row_ == nullptr) {
        row_ = std::make_unique<LaneControlMoreRow>(kind_, *more_);
        row_->onChosen = [this](std::optional<LaneControl> control) {
            if (!control.has_value())
                enterEdit();
            else
                perform(*control, *more_);
        };
        row_->onDismissed = [this] { more_->setLit(false); };
    }
    row_->setControls(hiddenControls(), actions_.isEnabled, actions_.detail);
    more_->setLit(true);
    row_->open(*overlay, rowAnchor());
    // From the keyboard the first button takes focus; a press leaves focus where it was.
    if (more_->hasKeyboardFocus(false) && !row_->getButtons().empty())
        row_->getButtons().front()->grabKeyboardFocus();
}

void LaneControlStrip::closeRow() {
    if (row_ != nullptr)
        row_->close();
}

bool LaneControlStrip::isAnimating() const {
    return trayDriver_.isRunning() || slideDriver_.isRunning() || growDriver_.isRunning() ||
           (row_ != nullptr && row_->isAnimating());
}

} // namespace synth::ui
