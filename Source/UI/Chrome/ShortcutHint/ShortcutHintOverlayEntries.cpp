#include "ShortcutHintOverlay.h"

#include "ShortcutHintText.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

// Concern: which buttons get a bubble right now, what each says, and where it goes. Read fresh each
// time the hints appear (and on a rebind or resize while they are up), so a rebound key shows its
// new chord and nothing is remembered between holds.

namespace {

// Bubbles here are plain juce::Components' worth of geometry; a themed LookAndFeel is optional so
// the entry list is still testable headless (estimated widths).
const synth::theme::AppLookAndFeel* themedLook(const juce::Component& c) {
    return dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel());
}

} // namespace

int ShortcutHintOverlay::textWidth(const juce::Font& font, const juce::String& text) const {
    return juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, text));
}

int ShortcutHintOverlay::capWidth(const juce::String& text) const {
    if (const auto* lf = themedLook(*this))
        return lf->getShortcutKeyCapWidth(text);
    using LnF = synth::theme::AppLookAndFeel;
    return juce::jmax(LnF::kKeyCapMinWidth, text.length() * 6 + 2 * LnF::kKeyCapSidePadding);
}

juce::String ShortcutHintOverlay::keyTextFor(const juce::String& actionId) const {
    return hint::formatKeyCapTextForPlatform(shortcuts_.getBinding(actionId));
}

juce::Rectangle<int> ShortcutHintOverlay::boundsInOverlay(const juce::Component& c) const {
    return getLocalArea(&c, c.getLocalBounds());
}

// A component gets a hint only while it is really on screen and reachable: visible up to the host,
// its centre inside every ancestor's bounds (not scrolled or clipped away), and no later (higher)
// sibling at any level covering that centre. Deliberately not isShowing() or getComponentAt(): the
// first also wants a native window and the second wants the host itself visible, and neither
// changes anything in the real app (the host is on screen whenever Cmd is held).
bool ShortcutHintOverlay::isHintable(const juce::Component& c) const {
    if (!host_.isParentOf(&c))
        return false;
    auto point = c.getLocalBounds().getCentre();
    const juce::Component* node = &c;
    while (node != &host_) {
        const auto* parent = node->getParentComponent();
        if (!node->isVisible())
            return false;
        point = parent->getLocalPoint(node, point);
        if (!parent->getLocalBounds().contains(point))
            return false;
        for (int i = parent->getIndexOfChildComponent(node) + 1; i < parent->getNumChildComponents(); ++i) {
            const auto* above = parent->getChildComponent(i);
            bool takesClicks = true, childrenTakeClicks = true;
            above->getInterceptsMouseClicks(takesClicks, childrenTakeClicks);
            if (above->isVisible() && takesClicks && above->getBounds().contains(point))
                return false;
        }
        node = parent;
    }
    return true;
}

void ShortcutHintOverlay::rebuildEntries() {
    entries_.clear();
    const DockHintInfo dock = dockSource_ ? dockSource_() : DockHintInfo{};
    addBubbleEntries(dock);
    if (dock.open)
        addTabEntries(dock);
    else
        addHiddenDockRow(dock);
    repaint();
}

void ShortcutHintOverlay::addBubbleEntries(const DockHintInfo& dock) {
    using synth::theme::AppLookAndFeel;
    std::vector<hint::BubbleRequest> requests;
    std::vector<juce::String> texts;
    const auto dockBounds = dock.dock != nullptr && dock.open ? boundsInOverlay(*dock.dock) : juce::Rectangle<int>();

    for (const auto& target : targets_) {
        auto* c = target.component.getComponent();
        if (c == nullptr || !isHintable(*c))
            continue;
        const auto text = keyTextFor(target.actionId);
        if (text.isEmpty())
            continue;
        hint::BubbleRequest request;
        request.anchor = boundsInOverlay(*c);
        request.size = {capWidth(text), AppLookAndFeel::kKeyCapHeight};
        if (!dockBounds.isEmpty() && dock.dock->isParentOf(c))
            request.container = dockBounds;
        requests.push_back(request);
        texts.push_back(text);
    }

    const auto placed = hint::placeBubbles(requests, getLocalBounds());
    for (size_t i = 0; i < placed.size(); ++i)
        if (placed[i])
            entries_.push_back({*placed[i], *placed[i], texts[i], {}, false});
}

void ShortcutHintOverlay::addTabEntries(const DockHintInfo& dock) {
    using synth::theme::AppLookAndFeel;
    for (const auto& tab : dock.tabs) {
        if (tab.button == nullptr || !isHintable(*tab.button))
            continue;
        const auto text = keyTextFor(tab.actionId);
        if (text.isEmpty())
            continue;

        // Tab names are centred in their tab, so the name ends half its width right of centre.
        const auto tabBounds = boundsInOverlay(*tab.button);
        // The stock text-button font: the UI face at 60% of the button height, capped at 16.
        const auto* lf = themedLook(*this);
        const juce::Font font =
            lf != nullptr
                ? juce::Font(juce::FontOptions(lf->getTheme().type.uiFamily,
                                               juce::jmin(16.0f, tab.button->getHeight() * 0.6f), juce::Font::plain))
                : juce::Font(juce::FontOptions(12.0f));
        const int nameRight = tabBounds.getCentreX() + textWidth(font, tab.name) / 2;
        const juce::Point<int> size{capWidth(text), AppLookAndFeel::kKeyCapCompactHeight};
        if (auto bubble = hint::placeInsideTab(tabBounds, nameRight, size))
            entries_.push_back({*bubble, *bubble, text, {}, false});
    }
}

void ShortcutHintOverlay::addHiddenDockRow(const DockHintInfo& dock) {
    using synth::theme::AppLookAndFeel;
    if (dock.statusBar == nullptr)
        return;

    struct Item {
        juce::String key, label;
    };
    std::vector<Item> items;
    if (dock.toggle != nullptr) {
        const auto text = keyTextFor(dock.toggleActionId);
        if (text.isNotEmpty())
            items.push_back({text, dock.toggleLabel});
    }
    for (const auto& tab : dock.tabs) {
        const auto text = keyTextFor(tab.actionId);
        if (text.isNotEmpty())
            items.push_back({text, tab.name});
    }

    const auto* lf = themedLook(*this);
    const juce::Font labelFont =
        lf != nullptr
            ? juce::Font(juce::FontOptions(lf->getTheme().type.uiFamily, lf->getTheme().type.label, juce::Font::plain))
            : juce::Font(juce::FontOptions(10.5f));
    constexpr int kPillPad = 2;
    constexpr int kCapToLabel = 6;
    constexpr int kLabelEndPad = 8;

    std::vector<int> widths;
    for (const auto& item : items)
        widths.push_back(kPillPad + capWidth(item.key) + kCapToLabel + textWidth(labelFont, item.label) + kLabelEndPad);

    const int statusTop = boundsInOverlay(*dock.statusBar).getY();
    const auto row = hint::layoutHiddenRow(widths, hint::kPillHeight, getLocalBounds(), statusTop);
    for (size_t i = 0; i < row.size(); ++i) {
        const juce::Rectangle<int> cap(row[i].getX() + kPillPad,
                                       row[i].getCentreY() - AppLookAndFeel::kKeyCapHeight / 2, capWidth(items[i].key),
                                       AppLookAndFeel::kKeyCapHeight);
        entries_.push_back({row[i], cap, items[i].key, items[i].label, true});
    }
}

} // namespace synth::ui
