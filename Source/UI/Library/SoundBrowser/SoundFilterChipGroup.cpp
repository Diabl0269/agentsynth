// SoundFilterChipGroup.cpp -- the toggle chips of one filter group: wrapped layout, painting from theme tokens, the
// mouse, the keys of a list of controls and the one spoken value.
#include "UI/Library/SoundBrowser/SoundFilterChipGroup.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

// Built per call: a function-local static Font is destroyed after JUCE's own caches at exit.
juce::Font chipFont() { return juce::Font(juce::FontOptions(12.0f)); }

class ChipValue : public juce::AccessibilityTextValueInterface {
public:
    explicit ChipValue(const SoundFilterChipGroup& owner)
        : owner_(owner) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return owner_.getAccessibilityValueText(); }
    void setValueAsString(const juce::String&) override {}

private:
    const SoundFilterChipGroup& owner_;
};

} // namespace

SoundFilterChipGroup::SoundFilterChipGroup(const juce::String& groupName, const juce::String& tooltipNoun)
    : groupName_(groupName)
    , noun_(tooltipNoun) {
    setTitle(groupName + " filters");
    setTooltip("Show only " + tooltipNoun + ". Left and Right move, Space turns one on or off.");
    setWantsKeyboardFocus(true);
}

void SoundFilterChipGroup::setItems(std::vector<Item> items) {
    items_ = std::move(items);
    focused_ = juce::jlimit(0, juce::jmax(0, (int)items_.size() - 1), focused_);
    resized();
    repaint();
}

void SoundFilterChipGroup::setOn(const juce::String& id, bool on) {
    for (auto& item : items_)
        if (item.id == id && item.on != on) {
            item.on = on;
            repaint();
        }
}

bool SoundFilterChipGroup::isOn(const juce::String& id) const {
    for (const auto& item : items_)
        if (item.id == id)
            return item.on;
    return false;
}

int SoundFilterChipGroup::chipWidth(const Item& item) const {
    return (int)std::ceil(chipFont().getStringWidthFloat(item.label)) + 18;
}

std::vector<juce::Rectangle<int>> SoundFilterChipGroup::layoutChips(int width) const {
    std::vector<juce::Rectangle<int>> out;
    int x = 0;
    int y = 0;
    for (const auto& item : items_) {
        const int w = chipWidth(item);
        if (x > 0 && x + w > width) {
            x = 0;
            y += kChipHeight + kGap;
        }
        out.push_back({x, y, w, kChipHeight});
        x += w + kGap;
    }
    return out;
}

int SoundFilterChipGroup::heightForWidth(int width) const {
    const auto laid = layoutChips(width);
    return laid.empty() ? 0 : laid.back().getBottom();
}

void SoundFilterChipGroup::resized() { bounds_ = layoutChips(getWidth()); }

juce::Rectangle<int> SoundFilterChipGroup::getChipBounds(int index) const {
    return index >= 0 && index < (int)bounds_.size() ? bounds_[(size_t)index] : juce::Rectangle<int>();
}

int SoundFilterChipGroup::indexAt(juce::Point<int> p) const {
    for (int i = 0; i < (int)bounds_.size(); ++i)
        if (bounds_[(size_t)i].contains(p))
            return i;
    return -1;
}

void SoundFilterChipGroup::setFocusedIndex(int index) {
    focused_ = juce::jlimit(0, juce::jmax(0, (int)items_.size() - 1), index);
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

juce::String SoundFilterChipGroup::spokenChip(int index) const {
    if (index < 0 || index >= (int)items_.size())
        return groupName_;
    const auto& item = items_[(size_t)index];
    return item.label + " filter, " + (item.on ? "on" : "off");
}

juce::String SoundFilterChipGroup::getAccessibilityValueText() const { return spokenChip(focused_); }

void SoundFilterChipGroup::toggle(int index) {
    if (index < 0 || index >= (int)items_.size())
        return;
    auto& item = items_[(size_t)index];
    item.on = !item.on;
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    if (onToggled)
        onToggled(item.id, item.on);
}

void SoundFilterChipGroup::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    g.setFont(chipFont());
    for (int i = 0; i < (int)items_.size(); ++i) {
        const auto& item = items_[(size_t)i];
        const auto area = bounds_[(size_t)i].toFloat();
        if (item.on) {
            g.setColour(c.accent.withAlpha(0.22f));
            g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
            g.setColour(c.accent);
        } else {
            if (i == hovered_) {
                g.setColour(c.accent.withAlpha(0.10f));
                g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
            }
            g.setColour(c.border);
        }
        g.drawRoundedRectangle(area.reduced(0.5f), area.getHeight() * 0.5f, 1.0f);
        g.setColour(item.on ? c.textPrimary : c.textMuted);
        g.drawText(item.label, bounds_[(size_t)i], juce::Justification::centred, false);
        if (i == focused_)
            paintFocusRing(g, area.expanded(1.0f), *this, area.getHeight() * 0.5f + 1.0f);
    }
}

void SoundFilterChipGroup::mouseDown(const juce::MouseEvent& e) {
    const int i = indexAt(e.getPosition());
    if (i < 0)
        return;
    grabKeyboardFocus();
    setFocusedIndex(i);
    toggle(i);
}

void SoundFilterChipGroup::mouseMove(const juce::MouseEvent& e) {
    const int i = indexAt(e.getPosition());
    if (i == hovered_)
        return;
    hovered_ = i;
    setTooltip(i >= 0 ? "Show only " + items_[(size_t)i].label.toLowerCase()
                      : "Show only " + noun_ + ". Left and Right move, Space turns one on or off.");
    setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void SoundFilterChipGroup::mouseExit(const juce::MouseEvent&) {
    hovered_ = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

bool SoundFilterChipGroup::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const int last = (int)items_.size() - 1;
    if (key.isKeyCode(juce::KeyPress::leftKey)) {
        setFocusedIndex(focused_ - 1);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::rightKey)) {
        setFocusedIndex(juce::jmin(last, focused_ + 1));
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::homeKey)) {
        setFocusedIndex(0);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::endKey)) {
        setFocusedIndex(last);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::spaceKey) || key.isKeyCode(juce::KeyPress::returnKey)) {
        toggle(focused_);
        return true;
    }
    return false;
}

void SoundFilterChipGroup::focusGained(FocusChangeType) {
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void SoundFilterChipGroup::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> SoundFilterChipGroup::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(
        *this, juce::AccessibilityRole::list, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<ChipValue>(*this)});
}

} // namespace synth::ui
