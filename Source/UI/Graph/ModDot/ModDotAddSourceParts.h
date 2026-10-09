#pragma once

// The small parts of the Add source list: a source row, a group's fold header and the "Expand all" links.
// Each is a real Tab stop with the accent focus ring (radius 6), a screen-reader name and a tooltip.
// Up/Down are left to the page, which walks them as one list.

#include "ModDotGlyphButton.h"
#include "ModDotMotion.h"
#include "ModDotPalette.h"
#include "ModSourceCatalog.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/SearchMatch.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** One source in a group, with how many things it already moves on the right ("2 targets", "Not used yet"). An
 *  "added" source (already on the knob) is greyed and cannot be picked twice. A "New <module>" row (`newType` set)
 *  carries a small "New" tag instead and creates that module when picked. */
class ModDotChoiceRow final
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kHeight = 26;
    static constexpr int kUsageWidth = 86;

    ModDotChoiceRow(ModSourceItem item, bool added, juce::String usage = {}, juce::String newType = {})
        : item_(std::move(item))
        , added_(added)
        , usage_(std::move(usage))
        , newType_(std::move(newType))
        , hover_(*this) {
        setWantsKeyboardFocus(true);
        auto title = item_.label();
        if (isNew()) {
            setTitle(title + ", new module");
            setTooltip("Create " + title + " and add it as a source");
        } else {
            setTitle(title + (usage_.isNotEmpty() ? ", " + usage_ : juce::String()) + (added ? ", added" : ""));
            setTooltip(added ? title + " is already on this knob" : "Add " + title);
        }
        setMouseCursor(added ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    }

    bool isNew() const noexcept { return newType_.isNotEmpty(); }
    /** The factory key a "New" row creates; empty for an existing source. */
    const juce::String& newType() const noexcept { return newType_; }
    const juce::String& usageText() const noexcept { return usage_; }
    /** What a search matches: the module's key and aliases for a "New" row (so "adsr" and "amp env" find "New Env"),
     *  else the label and aliases. */
    juce::String matchText() const {
        return isNew() ? (item_.aliases.isEmpty() ? newType_ : newType_ + " " + item_.aliases) : item_.searchText();
    }

    const ModSourceItem& item() const noexcept { return item_; }
    bool isAdded() const noexcept { return added_; }
    void setQuery(const juce::String& query) {
        if (query != query_) {
            query_ = query;
            repaint();
        }
    }
    const juce::String& query() const noexcept { return query_; }

    std::function<void(const ModDotChoiceRow&)> onPick;

    /** Picks it, as a click or Return would; an added source does nothing. */
    void pick() {
        if (!added_ && onPick)
            onPick(*this);
    }

    void paint(juce::Graphics& g) override {
        const auto p = modDotPaletteFor(*this);
        const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float)getWidth(), (float)kHeight).reduced(4.0f, 1.0f);
        if (!added_ && hover_.value() > 0.0f) {
            g.setColour(p.hover.withAlpha(hover_.value()));
            g.fillRoundedRectangle(area, 6.0f);
        }
        const auto text = juce::Rectangle<int>(0, 0, getWidth(), kHeight).reduced(14, 0);
        auto labelArea = text;
        if (isNew())
            paintNewTag(g, p, labelArea.removeFromRight(36));
        else if (usage_.isNotEmpty()) {
            g.setColour(added_ ? p.disabled : p.muted);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(usage_, labelArea.removeFromRight(kUsageWidth), juce::Justification::centredRight);
        }
        drawSearchHighlightedText(g, item_.label(), query_, labelArea, juce::Font(juce::FontOptions(12.5f)),
                                  added_ ? p.disabled : p.text, p.accent.withAlpha(0.28f), p.accent);
        paintFocusRing(g, area, *this, 6.0f);
    }
    void mouseEnter(const juce::MouseEvent&) override { hover_.setHovered(true); }
    void mouseExit(const juce::MouseEvent&) override { hover_.setHovered(false); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (getLocalBounds().contains(e.getPosition()))
            pick();
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
            pick();
            return true;
        }
        return false;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    static void paintNewTag(juce::Graphics& g, const ModDotPalette& p, juce::Rectangle<int> area) {
        const auto pill = area.toFloat().withSizeKeepingCentre((float)area.getWidth(), 14.0f);
        g.setColour(p.accent.withAlpha(0.18f));
        g.fillRoundedRectangle(pill, 7.0f);
        g.setColour(p.accent);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("New", pill.toNearestInt(), juce::Justification::centred);
    }

    ModSourceItem item_;
    bool added_;
    juce::String usage_;
    juce::String newType_;
    juce::String query_;
    ModDotHoverFade hover_;
};

/** A group's header: fold arrow, name, count. Click, Return or Space toggles; Left folds, Right unfolds. */
class ModDotGroupHeader final : public juce::Button {
public:
    static constexpr int kHeight = 26;

    explicit ModDotGroupHeader(juce::String groupName)
        : juce::Button(groupName)
        , group_(std::move(groupName))
        , hover_(*this) {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setExpanded(true, 1.0f);
    }

    std::function<void(bool expand)> onSetExpanded;

    /** `fold` is the arrow's turn, 0 (folded) .. 1 (open); `expanded` is the state it is heading for. */
    void setExpanded(bool expanded, float fold) {
        if (fold != fold_) {
            fold_ = fold;
            repaint();
        }
        if (expanded != expanded_ || getTitle().isEmpty()) {
            expanded_ = expanded;
            setTitle(group_ + (expanded ? ", expanded" : ", collapsed"));
            setTooltip((expanded ? "Collapse " : "Expand ") + group_);
        }
    }
    bool isExpanded() const noexcept { return expanded_; }
    void setCount(int count) {
        if (count != count_) {
            count_ = count;
            repaint();
        }
    }
    int count() const noexcept { return count_; }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key.isKeyCode(juce::KeyPress::leftKey)) {
            if (onSetExpanded)
                onSetExpanded(false);
            return true;
        }
        if (key.isKeyCode(juce::KeyPress::rightKey)) {
            if (onSetExpanded)
                onSetExpanded(true);
            return true;
        }
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
            triggerClick();
            return true;
        }
        return false;
    }
    void mouseEnter(const juce::MouseEvent& e) override {
        juce::Button::mouseEnter(e);
        hover_.setHovered(true);
    }
    void mouseExit(const juce::MouseEvent& e) override {
        juce::Button::mouseExit(e);
        hover_.setHovered(false);
    }
    void paintButton(juce::Graphics& g, bool, bool) override {
        const auto p = modDotPaletteFor(*this);
        const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float)getWidth(), (float)kHeight).reduced(4.0f, 1.0f);
        g.setColour(p.hover.withAlpha(hover_.value()));
        g.fillRoundedRectangle(area, 6.0f);
        paintModDotChevron(g, juce::Rectangle<float>(8.0f, 8.0f).withCentre({16.0f, (float)kHeight * 0.5f}), fold_,
                           p.muted);
        g.setColour(p.text);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText(group_, juce::Rectangle<int>(28, 0, getWidth() - 70, kHeight), juce::Justification::centredLeft);
        g.setColour(p.muted);
        g.setFont(p.mono(11.0f));
        g.drawText(juce::String(count_), juce::Rectangle<int>(getWidth() - 44, 0, 32, kHeight),
                   juce::Justification::centredRight);
        paintFocusRing(g, area, *this, 6.0f);
    }

private:
    juce::String group_;
    bool expanded_ = true;
    float fold_ = 1.0f;
    int count_ = 0;
    ModDotHoverFade hover_;
};

/** "Expand all" / "Collapse all": accent text, no box. */
class ModDotLinkButton final : public juce::Button {
public:
    ModDotLinkButton(const juce::String& text, const juce::String& name)
        : juce::Button(name)
        , text_(text) {
        setTitle(name);
        setTooltip(name);
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    int preferredWidth() const {
        return juce::roundToInt(juce::GlyphArrangement::getStringWidth(juce::Font(juce::FontOptions(11.5f)), text_)) +
               12;
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
            triggerClick();
            return true;
        }
        return false;
    }
    void paintButton(juce::Graphics& g, bool over, bool) override {
        const auto p = modDotPaletteFor(*this);
        g.setColour(over ? p.accent : p.accent.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText(text_, getLocalBounds(), juce::Justification::centred);
        paintFocusRing(g, getLocalBounds().toFloat(), *this, 6.0f);
    }

private:
    juce::String text_;
};

} // namespace synth::ui
