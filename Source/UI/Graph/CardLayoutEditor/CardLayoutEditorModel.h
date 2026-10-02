#pragma once

#include "UI/Graph/CardLayoutEditor/CardLayoutEditorSource.h"

namespace synth::ui {

/**
 * The card layout editor's working layout and every edit it offers, with no UI: the rows the list
 * shows (filtered by a search), tick/untick, rename, widget choice, group titles, + Add group, and
 * moves by key or by a drop. A row is keyed by its parameter id, "view:<n>" for a view, or "#<n>" for
 * section n's header. docs/layout/module-card-layout.md#editing-a-layout.
 */
class CardLayoutEditorModel {
public:
    using HiddenRows = CardLayoutEditorSource::HiddenRows;

    struct Row {
        enum class Kind { Header, Param, View };
        Kind kind = Kind::Param;
        int section = 0;
        juce::String key;
        juce::String name; ///< The parameter's or view's own name, or the section title.
        bool shown = true;
        bool placed = true; ///< False for an unticked row that left the layout.
        bool tab = false;   ///< A header of a `tab` section: one tab of the card's tab strip.
        std::optional<juce::String> label;
        CardWidget widget = CardWidget::Auto;
        std::vector<CardWidget> widgetChoices;
    };

    CardLayoutEditorModel(std::vector<CardLayoutEditorParam> params, HiddenRows hiddenRows, bool groups);

    /** Replaces the working layout with `layout`, normalised: every parameter placed once (an unplaced
     *  one is appended, hidden, when hidden rows stay in place), and items naming no known parameter
     *  set aside as missing. */
    void load(const CardLayout& layout);
    /** The working layout; missing items are dropped. */
    CardLayout toLayout() const;

    std::vector<Row> rows(const juce::String& search) const;
    /** Each missing item's label or id. */
    juce::StringArray missingNames() const;
    void clearMissing() { missing_.clear(); }

    bool isShown(const juce::String& paramId) const;
    void setShown(const juce::String& paramId, bool shown);
    /** Trimmed; empty or the parameter's own name clears the override. */
    void setLabel(const juce::String& paramId, const juce::String& text);
    void setWidget(const juce::String& paramId, CardWidget widget);
    void setSectionTitle(int section, const juce::String& title);
    /** Appends an empty titled group; returns its index. */
    int addGroup();
    int sectionCount() const { return (int)sections_.size(); }

    /** One place up (-1) or down (+1), into the neighbouring group at a group's edge. False if it
     *  cannot move. */
    bool moveBy(const juce::String& key, int delta);
    /** Moves `key` to `index` among the first section's items (clamped). */
    void moveToIndex(const juce::String& key, int index);
    /** Puts `key` where a drop left it: `visibleOrder` is the dragged group's keys (headers included)
     *  in the dropped order; a search may hide others, which keep their places. */
    void dropAt(const juce::String& key, const std::vector<juce::String>& visibleOrder);

    const CardLayoutEditorParam* findParam(const juce::String& paramId) const;
    static juce::String headerKey(int section) { return "#" + juce::String(section); }
    static bool isHeaderKey(const juce::String& key) { return key.startsWithChar('#'); }

private:
    struct Position {
        int section = -1;
        int index = -1;
        bool valid() const { return section >= 0; }
    };
    Position find(const juce::String& key) const;
    CardItem take(Position at);
    void insert(CardItem item, int section, int index);
    CardParamItem* findItem(const juce::String& paramId);
    juce::String nameOf(const CardItem& item) const;
    void addRowsInPlace(std::vector<Row>& out, const juce::String& search) const;
    void addRowsTickedFirst(std::vector<Row>& out, const juce::String& search) const;
    Row rowFor(const CardItem& item, int section) const;

    std::vector<CardLayoutEditorParam> params_;
    HiddenRows hiddenRows_;
    bool groups_;
    std::vector<CardSection> sections_;
    juce::StringArray hidden_;
    std::optional<juce::String> basedOn_;
    std::vector<CardParamItem> missing_;
};

/** The key of a view item's row. */
juce::String cardLayoutViewKey(CardView view);
/** A view's name in the editor list ("Threshold display"). */
juce::String cardLayoutViewName(CardView view);
/** A widget's name in the editor's widget choice ("Vertical fader"). */
juce::String cardLayoutWidgetName(CardWidget widget);

} // namespace synth::ui
