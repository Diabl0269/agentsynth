// SoundFilterChipRow.cpp -- the removable chips under the search: the grow-in and shrink-out of each one with the rest
// sliding along, painting from theme tokens, the mouse and the keys of a list of controls.
#include "UI/Library/SoundBrowser/SoundFilterChipRow.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {

constexpr int kChipHeight = 22;
constexpr int kGap = 6;
constexpr int kSideMargin = 12;
constexpr int kCrossWidth = 14;

// Built per call: a function-local static Font is destroyed after JUCE's own caches at exit.
juce::Font chipFont() { return juce::Font(juce::FontOptions(12.0f)); }

class RowValue : public juce::AccessibilityTextValueInterface {
public:
    explicit RowValue(const SoundFilterChipRow& owner)
        : owner_(owner) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return owner_.getAccessibilityValueText(); }
    void setValueAsString(const juce::String&) override {}

private:
    const SoundFilterChipRow& owner_;
};

} // namespace

SoundFilterChipRow::SoundFilterChipRow() {
    setTitle("Chosen filters");
    setTooltip("Chosen filters. Left and Right move, Space removes one.");
    setWantsKeyboardFocus(true);
}

SoundFilterChipRow::~SoundFilterChipRow() { anim_.stop(vblank_); }

// How much of its size a chip shows at presence `shown`: it grows with the tween, but under Reduce Motion it is whole
// from its first frame and only fades.
static float sizeFactor(float shown) { return prefersReducedMotion() ? (shown > 0.0f ? 1.0f : 0.0f) : shown; }

int SoundFilterChipRow::fullWidth(const Item& item) const {
    if (item.chip.id == kClearAllId)
        return (int)std::ceil(juce::GlyphArrangement::getStringWidth(chipFont(), item.chip.text)) + 8;
    return (int)std::ceil(juce::GlyphArrangement::getStringWidth(chipFont(), item.chip.text)) + 20 + kCrossWidth;
}

float SoundFilterChipRow::rowPresence() const {
    float best = 0.0f;
    for (const auto& item : items_)
        if (item.chip.id != kClearAllId)
            best = juce::jmax(best, item.shown);
    return prefersReducedMotion() ? (best > 0.0f ? 1.0f : 0.0f) : best;
}

int SoundFilterChipRow::visibleChipCount() const {
    int n = 0;
    for (const auto& item : items_)
        n += (!item.leaving && item.chip.id != kClearAllId) ? 1 : 0;
    return n;
}

void SoundFilterChipRow::setChips(const std::vector<SoundFilterModel::Chip>& chips) {
    std::vector<Item> next;
    // Existing chips keep their place and their current size; ones not in `chips` start to leave.
    for (auto& old : items_) {
        if (old.chip.id == kClearAllId)
            continue;
        const auto it = std::find_if(chips.begin(), chips.end(), [&](const auto& c) { return c.id == old.chip.id; });
        Item item = old;
        item.from = old.shown;
        item.leaving = it == chips.end();
        item.to = item.leaving ? 0.0f : 1.0f;
        if (!item.leaving)
            item.chip = *it;
        if (!(item.leaving && old.shown <= 0.0f))
            next.push_back(std::move(item));
    }
    for (const auto& chip : chips) {
        const bool known = std::any_of(next.begin(), next.end(), [&](const Item& i) { return i.chip.id == chip.id; });
        if (!known) {
            Item item;
            item.chip = chip;
            item.from = item.shown = 0.0f;
            item.to = 1.0f;
            next.push_back(std::move(item));
        }
    }
    const bool any = !chips.empty();
    Item clear;
    clear.chip = {kClearAllId, "Clear all", "Clear all filters"};
    clear.from = clear.shown = 1.0f;
    for (const auto& old : items_)
        if (old.chip.id == kClearAllId) {
            clear.from = old.shown;
            clear.shown = old.shown;
        }
    clear.to = any ? 1.0f : 0.0f;
    if (any && clear.shown <= 0.0f)
        clear.from = clear.shown = 0.0f;
    next.push_back(std::move(clear));
    items_ = std::move(next);
    focused_ = juce::jlimit(0, juce::jmax(0, (int)items_.size() - 1), focused_);
    startMotion();
}

void SoundFilterChipRow::startMotion() {
    const double ms = motionMs(190.0, 80.0);
    const auto land = [this] {
        for (auto& item : items_)
            item.shown = item.to;
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [](const Item& i) { return i.leaving || (i.to <= 0.0f && i.shown <= 0.0f); }),
                     items_.end());
        if (!items_.empty() && items_.back().chip.id == kClearAllId && items_.back().to <= 0.0f)
            items_.pop_back();
        focused_ = juce::jlimit(0, juce::jmax(0, (int)items_.size() - 1), focused_);
        relayout();
        if (onMotionFrame)
            onMotionFrame();
    };
    if (ms <= 0.0 || (!isShowing() && !animateOffscreenForTest_)) {
        anim_.stop(vblank_);
        land();
        return;
    }
    // Under Reduce Motion nothing slides or grows: the chips fade (the same tween value drives their opacity).
    anim_.start(
        vblank_, ms, easeOutCubic,
        [this](float t) {
            for (auto& item : items_)
                item.shown = item.from + (item.to - item.from) * t;
            relayout();
            if (onMotionFrame)
                onMotionFrame();
        },
        land);
    for (auto& item : items_)
        item.shown = item.from;
    relayout();
}

void SoundFilterChipRow::finishMotionForTest() {
    anim_.completeForTest();
    for (auto& item : items_)
        item.shown = item.to;
    items_.erase(std::remove_if(items_.begin(), items_.end(), [](const Item& i) { return i.leaving; }), items_.end());
    if (!items_.empty() && items_.back().chip.id == kClearAllId && items_.back().to <= 0.0f)
        items_.pop_back();
    relayout();
}

// Chips sit left to right at their animated widths; Clear all is pinned to the right end while it shows.
void SoundFilterChipRow::relayout() {
    bounds_.assign(items_.size(), {});
    int x = kSideMargin;
    const int y = (getHeight() - kChipHeight) / 2;
    int clearIndex = -1;
    for (int i = 0; i < (int)items_.size(); ++i) {
        const auto& item = items_[(size_t)i];
        if (item.chip.id == kClearAllId) {
            clearIndex = i;
            continue;
        }
        const float size = sizeFactor(item.shown);
        const int w = juce::roundToInt((float)fullWidth(item) * size);
        bounds_[(size_t)i] = {x, y, w, kChipHeight};
        x += w + juce::roundToInt((float)kGap * size);
    }
    if (clearIndex >= 0) {
        const auto& clear = items_[(size_t)clearIndex];
        const int w = juce::roundToInt((float)fullWidth(clear) * sizeFactor(clear.shown));
        const int clearX = juce::jmin(x, juce::jmax(kSideMargin, getWidth() - kSideMargin - fullWidth(clear)));
        bounds_[(size_t)clearIndex] = {clearX, y, w, kChipHeight};
    }
    repaint();
}

void SoundFilterChipRow::resized() { relayout(); }

juce::Rectangle<int> SoundFilterChipRow::getChipBounds(const juce::String& id) const {
    for (int i = 0; i < (int)items_.size(); ++i)
        if (items_[(size_t)i].chip.id == id)
            return bounds_[(size_t)i];
    return {};
}

juce::Rectangle<int> SoundFilterChipRow::getRemoveBounds(const juce::String& id) const {
    auto b = getChipBounds(id);
    return b.isEmpty() ? b : b.removeFromRight(kCrossWidth + 6);
}

std::vector<int> SoundFilterChipRow::focusOrder() const {
    std::vector<int> order;
    for (int i = 0; i < (int)items_.size(); ++i)
        if (!items_[(size_t)i].leaving && items_[(size_t)i].to > 0.0f)
            order.push_back(i);
    return order;
}

juce::String SoundFilterChipRow::getFocusedId() const {
    return focused_ >= 0 && focused_ < (int)items_.size() ? items_[(size_t)focused_].chip.id : juce::String();
}

juce::String SoundFilterChipRow::spoken(int index) const {
    if (index < 0 || index >= (int)items_.size())
        return "No filters";
    const auto& item = items_[(size_t)index];
    return item.chip.id == kClearAllId ? juce::String("Clear all filters") : "Remove " + item.chip.text + " filter";
}

juce::String SoundFilterChipRow::getAccessibilityValueText() const { return spoken(focused_); }

void SoundFilterChipRow::setFocusedItem(int index) {
    focused_ = index;
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

int SoundFilterChipRow::indexAtX(int x) const {
    for (int i = 0; i < (int)bounds_.size(); ++i)
        if (!items_[(size_t)i].leaving && bounds_[(size_t)i].getWidth() > 4 &&
            bounds_[(size_t)i].expanded(kGap / 2, 0).contains(x, getHeight() / 2))
            return i;
    return -1;
}

void SoundFilterChipRow::activate(int index) {
    if (index < 0 || index >= (int)items_.size())
        return;
    const auto id = items_[(size_t)index].chip.id;
    if (id == kClearAllId) {
        if (onClearAll)
            onClearAll();
    } else if (onRemove) {
        onRemove(id);
    }
}

void SoundFilterChipRow::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    g.setFont(chipFont());
    for (int i = 0; i < (int)items_.size(); ++i) {
        const auto& item = items_[(size_t)i];
        const auto b = bounds_[(size_t)i];
        if (b.getWidth() <= 1)
            continue;
        const float presence = juce::jlimit(0.0f, 1.0f, item.shown);
        const float alpha = prefersReducedMotion() ? presence : juce::jmin(1.0f, presence * 1.5f);
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(b.expanded(2));
        if (item.chip.id == kClearAllId) {
            g.setColour((i == hovered_ ? c.accent : c.textMuted).withMultipliedAlpha(alpha));
            g.drawText(item.chip.text, b, juce::Justification::centred, false);
        } else {
            const auto area = b.toFloat();
            g.setColour(
                (i == hovered_ ? c.accent.withAlpha(0.30f) : c.accent.withAlpha(0.18f)).withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
            g.setColour(c.accent.withMultipliedAlpha(alpha));
            g.drawRoundedRectangle(area.reduced(0.5f), area.getHeight() * 0.5f, 1.0f);
            g.setColour(c.textPrimary.withMultipliedAlpha(alpha));
            g.drawText(item.chip.text, b.withTrimmedRight(kCrossWidth + 4).withTrimmedLeft(10),
                       juce::Justification::centredLeft, false);
            const auto cross =
                juce::Rectangle<float>(6.0f, 6.0f)
                    .withCentre({(float)b.getRight() - 6.0f - (float)kCrossWidth * 0.5f, area.getCentreY()});
            juce::Path x;
            x.startNewSubPath(cross.getTopLeft());
            x.lineTo(cross.getBottomRight());
            x.startNewSubPath(cross.getTopRight());
            x.lineTo(cross.getBottomLeft());
            g.setColour(c.textMuted.withMultipliedAlpha(alpha));
            g.strokePath(x, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (i == focused_ && hasKeyboardFocus(false))
            paintFocusRing(g, b.toFloat().expanded(1.0f), *this, (float)kChipHeight * 0.5f);
    }
}

void SoundFilterChipRow::mouseDown(const juce::MouseEvent& e) {
    const int i = indexAtX(e.x);
    if (i < 0)
        return;
    grabKeyboardFocus();
    setFocusedItem(i);
    activate(i);
}

void SoundFilterChipRow::mouseMove(const juce::MouseEvent& e) {
    const int i = indexAtX(e.x);
    if (i == hovered_)
        return;
    hovered_ = i;
    setTooltip(i >= 0 ? items_[(size_t)i].chip.tooltip
                      : juce::String("Chosen filters. Left and Right move, Space removes one."));
    setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void SoundFilterChipRow::mouseExit(const juce::MouseEvent&) {
    hovered_ = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

bool SoundFilterChipRow::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const auto order = focusOrder();
    if (order.empty())
        return false;
    const auto pos = std::find(order.begin(), order.end(), focused_);
    const int at = pos == order.end() ? 0 : (int)(pos - order.begin());
    if (key.isKeyCode(juce::KeyPress::leftKey)) {
        setFocusedItem(order[(size_t)juce::jmax(0, at - 1)]);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::rightKey)) {
        setFocusedItem(order[(size_t)juce::jmin((int)order.size() - 1, at + 1)]);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::spaceKey) || key.isKeyCode(juce::KeyPress::returnKey) ||
        key.isKeyCode(juce::KeyPress::deleteKey) || key.isKeyCode(juce::KeyPress::backspaceKey)) {
        activate(pos == order.end() ? order[0] : focused_);
        return true;
    }
    return false;
}

void SoundFilterChipRow::focusGained(FocusChangeType) {
    const auto order = focusOrder();
    if (!order.empty() && std::find(order.begin(), order.end(), focused_) == order.end())
        focused_ = order[0];
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void SoundFilterChipRow::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> SoundFilterChipRow::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(
        *this, juce::AccessibilityRole::list, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<RowValue>(*this)});
}

} // namespace synth::ui
