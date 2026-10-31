#include "ModMatrixPicker.h"

#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/SearchMatch.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace synth::ui {

namespace {
constexpr int kWidth = 300;
constexpr int kMaxHeight = 360;
constexpr int kOuterPadding = 6;
constexpr int kSearchHeight = 26;
constexpr int kRowHeight = 24;
constexpr int kHeaderHeight = 20;
constexpr int kDetailRowHeight = 40; // a row with a second line
constexpr float kDetailFontSize = 11.0f;
constexpr float kRowFontSize = 13.0f;
constexpr float kHeaderFontSize = 11.0f;
constexpr int kGlyphGutter = 16; // before the text of every row when any row has a tick or a plus
constexpr float kTagFontSize = 9.0f;
constexpr int kOverscan = 4 * kRowHeight; // rows kept past each edge of the view, so a scroll never shows a gap

struct Palette {
    juce::Colour panel = juce::Colours::darkgrey.darker(0.4f);
    juce::Colour border = juce::Colours::grey.darker();
    juce::Colour field = juce::Colours::black.withAlpha(0.35f);
    juce::Colour text = juce::Colours::white;
    juce::Colour muted = juce::Colours::grey;
    juce::Colour accent = juce::Colours::lightblue;
    juce::Colour success = juce::Colours::limegreen;
    float radius = 6.0f;
};

// Theme tokens when the themed LookAndFeel is active, plain colours otherwise (headless tests, or a
// parentless CallOutBox window).
Palette paletteFor(juce::Component& comp) {
    Palette p;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&comp.getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        p.panel = c.surface;
        p.border = c.border;
        p.field = c.bg0;
        p.text = c.textPrimary;
        p.muted = c.textMuted;
        p.accent = c.accent;
        p.success = c.success;
        p.radius = lf->getTheme().metrics.cornerRadius;
    }
    return p;
}

} // namespace

// One header or item line in view: a recycled component bound to an entry of the list. It keeps the fade the filter
// steers, and reports a click and its own hover by entry so the picker keeps the highlight.
class ModMatrixPicker::Row
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    explicit Row(ModMatrixPicker& owner)
        : owner_(owner) {
        fade.onFrame = [this] { owner_.onRowFadeFrame(); };
    }

    FadeVisibility fade{this};

    int entry() const noexcept { return entry_; }
    void unbind() noexcept { entry_ = -1; }
    void bindHeader(int entry, const juce::String& text) {
        entry_ = entry;
        header_ = true;
        text_ = text;
        detail_ = {};
        isCurrent_ = ticked_ = isNew_ = gutter_ = false;
        setTitle(text);
        setTooltip({});
        setEnabled(true);
        setInterceptsMouseClicks(false, false);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
    // A disabled row is announced as unavailable and takes no pick; its detail says why.
    void bindItem(int entry, const Item& item, bool isCurrent, bool gutter) {
        entry_ = entry;
        header_ = false;
        text_ = item.text;
        detail_ = item.detail;
        isCurrent_ = isCurrent;
        ticked_ = item.ticked;
        isNew_ = item.isNew;
        gutter_ = gutter;
        const auto spoken = detail_.isNotEmpty() ? text_ + ", " + detail_ : text_;
        setTitle(item.accessibleName.isNotEmpty() ? item.accessibleName : spoken);
        setTooltip(spoken);
        setEnabled(item.enabled);
        setInterceptsMouseClicks(true, true);
        setMouseCursor(item.enabled ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void setHighlighted(bool on) {
        if (highlighted_ != on) {
            highlighted_ = on;
            repaint();
        }
    }
    void setPalette(const Palette& p) {
        palette_ = p;
        repaint();
    }
    // The (trimmed) search query whose matched letters this row paints highlighted.
    void setQuery(const juce::String& query) {
        if (query_ != query) {
            query_ = query;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        if (header_) {
            g.setColour(palette_.muted);
            g.setFont(juce::Font(juce::FontOptions(kHeaderFontSize, juce::Font::bold)));
            g.drawText(text_, bounds.reduced(6, 0), juce::Justification::centredLeft, true);
            return;
        }
        if (highlighted_) {
            g.setColour(palette_.accent.withAlpha(0.25f));
            g.fillRoundedRectangle(bounds.toFloat().reduced(2.0f, 1.0f), 3.0f);
        }
        // The row a combo currently holds is set apart by colour and weight, not by a glyph.
        const auto textColour = !isEnabled() ? palette_.muted : isCurrent_ ? palette_.accent : palette_.text;
        const juce::Font rowFont(juce::FontOptions(kRowFontSize, isCurrent_ ? juce::Font::bold : juce::Font::plain));
        const auto fill = palette_.accent.withAlpha(0.28f);
        auto content = bounds.reduced(12, 0);
        if (gutter_) {
            paintMark(g, content.removeFromLeft(kGlyphGutter));
            if (isNew_)
                paintNewTag(g, content);
        }
        if (detail_.isEmpty()) {
            drawSearchHighlightedText(g, text_, query_, content, rowFont, textColour, fill, palette_.accent);
            return;
        }
        auto lines = content.withTrimmedTop(3).withTrimmedBottom(3);
        drawSearchHighlightedText(g, text_, query_, lines.removeFromTop(lines.getHeight() / 2), rowFont, textColour,
                                  fill, palette_.accent);
        drawSearchHighlightedText(g, detail_, query_, lines, juce::Font(juce::FontOptions(kDetailFontSize)),
                                  palette_.muted, fill, palette_.accent);
    }

    // The tick of the current item, or the green plus of a row that creates, centred in its gutter.
    void paintMark(juce::Graphics& g, juce::Rectangle<int> gutter) const {
        const auto centre = gutter.toFloat().getCentre();
        if (isNew_) {
            g.setColour(palette_.success);
            g.drawLine(centre.x - 4.0f, centre.y, centre.x + 4.0f, centre.y, 1.6f);
            g.drawLine(centre.x, centre.y - 4.0f, centre.x, centre.y + 4.0f, 1.6f);
        } else if (ticked_) {
            juce::Path tick;
            tick.startNewSubPath(centre.x - 4.0f, centre.y);
            tick.lineTo(centre.x - 1.5f, centre.y + 3.0f);
            tick.lineTo(centre.x + 4.0f, centre.y - 3.5f);
            g.setColour(isEnabled() ? palette_.accent : palette_.muted);
            g.strokePath(tick, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // The "New" tag at the row's right edge, which takes that width off `content`.
    void paintNewTag(juce::Graphics& g, juce::Rectangle<int>& content) const {
        const juce::Font font(juce::FontOptions(kTagFontSize, juce::Font::bold));
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, "New", 0.0f, 0.0f);
        const int width = (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()) + 10;
        const auto tag = content.removeFromRight(width).withSizeKeepingCentre(width, 14).toFloat();
        content.removeFromRight(6);
        g.setColour(palette_.muted);
        g.drawRoundedRectangle(tag.reduced(0.5f), 4.0f, 1.0f);
        g.setFont(font);
        g.drawText("New", tag, juce::Justification::centred, false);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        if (entry_ >= 0 && !header_)
            owner_.hoverEntry(entry_);
    }
    void mouseMove(const juce::MouseEvent&) override {
        if (entry_ >= 0 && !header_ && !highlighted_)
            owner_.hoverEntry(entry_);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (entry_ >= 0 && !header_ && isEnabled() && getLocalBounds().contains(e.getPosition()))
            owner_.chooseEntry(entry_);
    }

private:
    ModMatrixPicker& owner_;
    int entry_ = -1;
    bool header_ = false;
    juce::String text_;
    juce::String detail_;
    juce::String query_;
    bool isCurrent_ = false;
    bool ticked_ = false;
    bool isNew_ = false;
    bool gutter_ = false;
    Palette palette_;
    bool highlighted_ = false;
};

ModMatrixPicker::ModMatrixPicker(juce::String what, std::vector<Item> items, int selectedId,
                                 std::function<void(int)> onChoose)
    : what_(std::move(what))
    , items_(std::move(items))
    , selectedId_(selectedId)
    , onChoose_(std::move(onChoose))
    , searchEditor_(std::make_unique<NavigationSearchField>()) {
    setComponentID("modMatrixPicker");
    setTitle("Modulation " + what_ + " picker");
    setWantsKeyboardFocus(false);

    searchEditor_->setComponentID("modMatrixPickerSearch");
    searchEditor_->setTitle("Search modulation " + what_ + "s");
    searchEditor_->setTooltip("Type to filter. Up and Down move, Return picks, Escape closes.");
    searchEditor_->setMultiLine(false);
    searchEditor_->setReturnKeyStartsNewLine(false);
    searchEditor_->setSelectAllWhenFocused(true);
    searchEditor_->setJustification(juce::Justification::centredLeft);
    searchEditor_->setBorder(juce::BorderSize<int>(0));
    searchEditor_->setIndents(6, 0);
    searchEditor_->setFont(juce::Font(juce::FontOptions(kRowFontSize)));
    searchEditor_->onTextChange = [this] { applyFilter(); };
    searchEditor_->onNavigationKey = [this](const juce::KeyPress& key) {
        if (key == juce::KeyPress::downKey) {
            moveHighlight(1);
            return true;
        }
        if (key == juce::KeyPress::upKey) {
            moveHighlight(-1);
            return true;
        }
        if (key == juce::KeyPress::returnKey) {
            chooseHighlighted();
            return true;
        }
        if (key == juce::KeyPress::escapeKey) {
            dismiss();
            return true;
        }
        return false;
    };
    addAndMakeVisible(*searchEditor_);

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&rowColumn_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setWantsKeyboardFocus(false); // the search field keeps focus; Up/Down scroll the highlight
    viewport_.onViewMoved = [this] { syncRows(); };

    rebuildRows();
    applyColours();
    setSize(kWidth, preferredHeight());
}

ModMatrixPicker::~ModMatrixPicker() {
    if (onClosed)
        onClosed();
}

void ModMatrixPicker::rebuildRows() {
    for (auto& entry : entries_)
        if (entry.row != nullptr)
            releaseRow(entry);
    entries_.clear();
    entries_.reserve(items_.size());
    laidOut_ = false;
    markGutter_ = std::any_of(items_.begin(), items_.end(), [](const Item& i) { return i.ticked || i.isNew; });

    juce::String currentCategory;
    bool first = true;
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        if (item.category.isNotEmpty() && (first || item.category != currentCategory)) {
            Entry header;
            header.header = true;
            header.item = (int)i;
            header.height = kHeaderHeight;
            entries_.push_back(std::move(header));
        }
        first = false;
        currentCategory = item.category;
        Entry entry;
        entry.item = (int)i;
        entry.height = item.detail.isNotEmpty() ? kDetailRowHeight : kRowHeight;
        entry.haystack = item.text + " " + item.detail + " " + item.searchText;
        entries_.push_back(std::move(entry));
    }
    applyFilter();
}

// Takes a free component (or makes one) for the entry. Its fade is left landed on hidden; the caller decides.
ModMatrixPicker::Row& ModMatrixPicker::acquireRow(int entryIndex) {
    if (freeRows_.empty()) {
        pool_.push_back(std::make_unique<Row>(*this));
        rowColumn_.addChildComponent(*pool_.back());
        freeRows_.push_back(pool_.back().get());
    }
    auto* row = freeRows_.back();
    freeRows_.pop_back();
    auto& entry = entries_[(size_t)entryIndex];
    entry.row = row;
    if (entry.header) {
        row->bindHeader(entryIndex, items_[(size_t)entry.item].category);
    } else {
        const auto& item = items_[(size_t)entry.item];
        row->bindItem(entryIndex, item, item.id == selectedId_, markGutter_);
    }
    row->setPalette(paletteFor(*this));
    row->setQuery(query_);
    row->setHighlighted(!entry.header && !visibleItems_.empty() &&
                        visibleItems_[(size_t)juce::jlimit(0, (int)visibleItems_.size() - 1, highlighted_)] ==
                            entryIndex);
    return *row;
}

void ModMatrixPicker::releaseRow(Entry& entry) {
    entry.row->fade.snapTo(false);
    entry.row->unbind();
    freeRows_.push_back(entry.row);
    entry.row = nullptr;
}

// A header shows only while some row under it matches; the height is deliberately not recomputed
// here (see preferredHeight), so the popup keeps its size while the list shrinks.
void ModMatrixPicker::applyFilter() {
    query_ = searchEditor_->getText().trim();
    const auto words = searchQueryWords(query_);
    visibleItems_.clear();
    Entry* pendingHeader = nullptr;
    bool headerHasMatch = false;
    // Word by word, in any order, ignoring case: "osc 8" finds "Oscillator 8" (synth::ui::searchMatches).
    int bestIndex = -1;
    int bestScore = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        auto& entry = entries_[i];
        entry.wasShown = entry.shown;
        if (entry.header) {
            if (pendingHeader != nullptr)
                pendingHeader->shown = headerHasMatch;
            pendingHeader = &entry;
            headerHasMatch = false;
            continue;
        }
        const auto& item = items_[(size_t)entry.item];
        const bool found = searchWordsMatch(entry.haystack, words);
        entry.shown = found || item.isNew; // a row that creates stays, however little the query finds
        headerHasMatch = headerHasMatch || entry.shown;
        if (!entry.shown)
            continue;
        if (found && item.enabled) {
            const int score = searchWordsScore(entry.haystack, words);
            if (bestIndex < 0 || score < bestScore) {
                bestIndex = (int)visibleItems_.size();
                bestScore = score;
            }
        }
        visibleItems_.push_back((int)i);
    }
    if (pendingHeader != nullptr)
        pendingHeader->shown = headerHasMatch;

    steerRows();
    laidOut_ = true;
    // The best match leads on a query (the first among equals); with none pickable the highlight stays on the first
    // row. After the jump to the top, so a best match further down stays scrolled into view.
    setHighlight(query_.isNotEmpty() && bestIndex >= 0 ? bestIndex : nextPickable(-1, 1));
}

// A filter pass restarts the list at the top. Only what lands in that first screenful is animated: a row with a
// component follows the filter from its current opacity, a row newly shown there fades in, and everything further down
// just takes its new state. Nothing here costs more than the screenful, whatever the list's length.
void ModMatrixPicker::steerRows() {
    syncing_ = true;
    viewport_.setViewPosition(0, 0);
    const int bottom = viewport_.getViewHeight() + kOverscan;
    int targetY = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        auto& entry = entries_[i];
        const bool inWindow = targetY < bottom;
        if (entry.row != nullptr) {
            ++rowsSteered_;
            entry.row->setQuery(query_);
            if (inWindow && laidOut_)
                entry.row->fade.setShown(entry.shown);
            else
                entry.row->fade.snapTo(entry.shown);
        } else if (entry.shown && inWindow) {
            ++rowsSteered_;
            auto& row = acquireRow((int)i);
            if (laidOut_ && !entry.wasShown)
                row.fade.setShown(true);
            else
                row.fade.snapTo(true);
        }
        targetY += entry.shown ? entry.height : 0;
    }
    layoutEntries();
    syncing_ = false;
    syncRows();
}

// A row the filter is taking out keeps a slot squeezed to its fade's progress, so the rows below slide up while it goes
// and one that comes back opens its slot the same way. Plain arithmetic over the list: only bound rows are components.
void ModMatrixPicker::layoutEntries() {
    const int width = juce::jmax(0, viewport_.getMaximumVisibleWidth());
    int y = 0;
    for (auto& entry : entries_) {
        const bool fading = entry.row != nullptr && entry.row->fade.isFading();
        const int slot = fading        ? juce::roundToInt((float)entry.height * entry.row->fade.progress())
                         : entry.shown ? entry.height
                                       : 0;
        entry.y = y;
        if (entry.row != nullptr)
            entry.row->setBounds(0, y, width, slot);
        y += slot;
    }
    const bool wasSyncing = std::exchange(syncing_, true); // a shrinking column moves the view: one sync, by the caller
    rowColumn_.setSize(width, juce::jmax(y, 1));
    syncing_ = wasSyncing;
}

// Gives the rows in the view (and a few past it) a component and takes it back from the rest. A row that is still
// fading keeps its component until the fade has landed: its driver must not be stopped from inside its own frame.
void ModMatrixPicker::syncRows() {
    if (syncing_)
        return;
    syncing_ = true;
    const auto view = viewport_.getViewArea();
    const int top = view.getY() - kOverscan;
    const int bottom = view.getBottom() + kOverscan;
    const int width = juce::jmax(0, viewport_.getMaximumVisibleWidth());
    for (size_t i = 0; i < entries_.size(); ++i) {
        auto& entry = entries_[i];
        const bool inWindow = entry.shown && entry.y + entry.height > top && entry.y < bottom;
        if (entry.row != nullptr) {
            if (!inWindow && !entry.row->fade.isFading())
                releaseRow(entry);
        } else if (inWindow) {
            auto& row = acquireRow((int)i);
            row.setBounds(0, entry.y, width, entry.height);
            row.fade.snapTo(true);
        }
    }
    syncing_ = false;
}

void ModMatrixPicker::onRowFadeFrame() {
    layoutEntries();
    syncRows();
    if (!anyRowFading())
        setHighlight(highlighted_); // the rows have settled: keep the highlighted one in view
}

bool ModMatrixPicker::anyRowFading() const {
    return std::any_of(pool_.begin(), pool_.end(), [](const auto& row) { return row->fade.isFading(); });
}

bool ModMatrixPicker::isFilterFadingForTest() const { return anyRowFading(); }

// The whole unfiltered list decides the height (capped), so typing never resizes the popup.
int ModMatrixPicker::preferredHeight() const {
    int rowsHeight = 0;
    for (const auto& entry : entries_)
        rowsHeight += entry.height;
    const int footer = footerHeight_ > 0 ? footerHeight_ + 4 : 0;
    const int content = kOuterPadding + kSearchHeight + 4 + juce::jmax(rowsHeight, kRowHeight) + footer + kOuterPadding;
    return juce::jlimit(kSearchHeight + kRowHeight + 2 * kOuterPadding + 4 + footer, kMaxHeight + footer, content);
}

void ModMatrixPicker::resized() {
    auto bounds = getLocalBounds().reduced(kOuterPadding);
    searchEditor_->setBounds(bounds.removeFromTop(kSearchHeight));
    bounds.removeFromTop(4);
    if (footer_ != nullptr) {
        footer_->setBounds(bounds.removeFromBottom(footerHeight_));
        bounds.removeFromBottom(4);
    }
    viewport_.setBounds(bounds);
    layoutEntries();
    syncRows();
}

void ModMatrixPicker::setItems(std::vector<Item> items) {
    items_ = std::move(items);
    rebuildRows();
    applyColours();
    setSize(kWidth, preferredHeight());
    listFade_.snapTo(false);
    listFade_.setShown(true);
}

void ModMatrixPicker::setFooter(std::unique_ptr<juce::Component> footer, int height) {
    footer_ = std::move(footer);
    footerHeight_ = footer_ != nullptr ? height : 0;
    if (footer_ != nullptr)
        addAndMakeVisible(*footer_);
    setSize(kWidth, preferredHeight());
    resized();
}

void ModMatrixPicker::clearFooter() { setFooter(nullptr, 0); }

void ModMatrixPicker::paint(juce::Graphics& g) {
    const auto p = paletteFor(*this);
    auto bounds = getLocalBounds().toFloat();
    g.setColour(p.panel);
    g.fillRoundedRectangle(bounds, p.radius);
    g.setColour(p.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), p.radius, 1.0f);
}

// The picker is a focus target as a whole (its search field holds focus), so it gets the same
// accent outline every other keyboard-reachable panel does.
void ModMatrixPicker::paintOverChildren(juce::Graphics& g) { paintFocusRegionOutline(*this, g); }

void ModMatrixPicker::applyColours() {
    const auto p = paletteFor(*this);
    searchEditor_->setColour(juce::TextEditor::backgroundColourId, p.field);
    searchEditor_->setColour(juce::TextEditor::textColourId, p.text);
    searchEditor_->setColour(juce::TextEditor::outlineColourId, p.border);
    searchEditor_->setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
    searchEditor_->setTextToShowWhenEmpty(placeholder_.isNotEmpty() ? placeholder_ : "Search " + what_ + "s...",
                                          p.muted);
    for (const auto& row : pool_)
        row->setPalette(p);
    repaint();
}

void ModMatrixPicker::lookAndFeelChanged() { applyColours(); }

// A CallOutBox attaches its content after construction, so the LookAndFeel and the focus target
// only become real once this component has a parent.
void ModMatrixPicker::parentHierarchyChanged() {
    applyColours();
    reveal_.startIfInCallout(); // eases in when it opens in a call-out
    if (isShowing())
        searchEditor_->grabKeyboardFocus();
}

const ModMatrixPicker::Item& ModMatrixPicker::itemAtVisible(int visibleIndex) const {
    return items_[(size_t)entries_[(size_t)visibleItems_[(size_t)visibleIndex]].item];
}

void ModMatrixPicker::setHighlight(int visibleItemIndex) {
    if (visibleItems_.empty()) {
        highlighted_ = 0;
        return;
    }
    highlighted_ = juce::jlimit(0, (int)visibleItems_.size() - 1, visibleItemIndex);
    const int target = visibleItems_[(size_t)highlighted_];
    for (const auto& row : pool_)
        row->setHighlighted(row->entry() == target);

    // Keep the highlighted row inside the viewport; the move gives it a component if it had none.
    const auto& entry = entries_[(size_t)target];
    const auto view = viewport_.getViewArea();
    if (entry.y < view.getY())
        viewport_.setViewPosition(0, entry.y);
    else if (entry.y + entry.height > view.getBottom())
        viewport_.setViewPosition(0, entry.y + entry.height - view.getHeight());
}

// The next pickable visible item after `from` in direction `step`; `from` itself when there is none that way
// (a list of disabled rows leaves the highlight where it is, and a pick on it does nothing).
int ModMatrixPicker::nextPickable(int from, int step) const {
    for (int i = from + step; i >= 0 && i < (int)visibleItems_.size(); i += step)
        if (itemAtVisible(i).enabled)
            return i;
    return juce::jmax(from, 0);
}

// Keyboard focus stays in the search field and most rows have no component to read, so the move is spoken.
void ModMatrixPicker::moveHighlight(int delta) {
    const int before = highlighted_;
    setHighlight(nextPickable(highlighted_, delta < 0 ? -1 : 1));
    if (highlighted_ != before && !visibleItems_.empty())
        juce::AccessibilityHandler::postAnnouncement(itemAtVisible(highlighted_).text,
                                                     juce::AccessibilityHandler::AnnouncementPriority::low);
}

void ModMatrixPicker::hoverEntry(int entryIndex) {
    if (!items_[(size_t)entries_[(size_t)entryIndex].item].enabled)
        return;
    const auto it = std::lower_bound(visibleItems_.begin(), visibleItems_.end(), entryIndex);
    if (it != visibleItems_.end() && *it == entryIndex)
        setHighlight((int)(it - visibleItems_.begin()));
}

void ModMatrixPicker::chooseHighlighted() {
    if (!visibleItems_.empty())
        chooseEntry(visibleItems_[(size_t)juce::jlimit(0, (int)visibleItems_.size() - 1, highlighted_)]);
}

// Reports the pick, then closes. The callback is copied first: closing is asynchronous, but nothing
// here may depend on that.
void ModMatrixPicker::chooseEntry(int entryIndex) {
    const auto& item = items_[(size_t)entries_[(size_t)entryIndex].item];
    if (!item.enabled)
        return;
    const int id = item.id;
    auto callback = onChoose_;
    dismiss();
    if (callback)
        callback(id);
}

void ModMatrixPicker::setAccessibleNames(const juce::String& pickerTitle, const juce::String& searchTitle) {
    setTitle(pickerTitle);
    searchEditor_->setTitle(searchTitle);
}

void ModMatrixPicker::setSearchPlaceholder(const juce::String& text) {
    placeholder_ = text;
    applyColours();
}

void ModMatrixPicker::dismiss() {
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        synth::ui::PopupMotion::dismissCallOut(*box);
}

// ---- Test seams ----

void ModMatrixPicker::setSearchTextForTest(const juce::String& text) {
    searchEditor_->setText(text, juce::dontSendNotification);
    applyFilter();
}

juce::String ModMatrixPicker::getSearchTextForTest() const { return searchEditor_->getText(); }

std::vector<juce::String> ModMatrixPicker::getVisibleRowNamesForTest() const {
    std::vector<juce::String> names;
    for (const auto& entry : entries_)
        if (entry.shown)
            names.push_back(entry.header ? items_[(size_t)entry.item].category : items_[(size_t)entry.item].text);
    return names;
}

std::vector<juce::String> ModMatrixPicker::getVisibleItemTextsForTest() const {
    std::vector<juce::String> names;
    for (int i = 0; i < (int)visibleItems_.size(); ++i)
        names.push_back(itemAtVisible(i).text);
    return names;
}

std::vector<juce::String> ModMatrixPicker::getVisibleItemDetailsForTest() const {
    std::vector<juce::String> details;
    for (int i = 0; i < (int)visibleItems_.size(); ++i)
        details.push_back(itemAtVisible(i).detail);
    return details;
}

std::vector<std::vector<SearchSpan>> ModMatrixPicker::getVisibleItemHighlightSpansForTest() const {
    std::vector<std::vector<SearchSpan>> spans;
    for (int i = 0; i < (int)visibleItems_.size(); ++i)
        spans.push_back(searchHighlightSpans(itemAtVisible(i).text, query_));
    return spans;
}

bool ModMatrixPicker::isVisibleItemTickedForTest(int index) const {
    return index >= 0 && index < (int)visibleItems_.size() && itemAtVisible(index).ticked;
}

bool ModMatrixPicker::isVisibleItemNewForTest(int index) const {
    return index >= 0 && index < (int)visibleItems_.size() && itemAtVisible(index).isNew;
}

bool ModMatrixPicker::isHighlightInViewForTest() const {
    if (visibleItems_.empty())
        return false;
    const auto& entry = entries_[(size_t)visibleItems_[(size_t)highlighted_]];
    return entry.row != nullptr && viewport_.getViewArea().contains(entry.row->getBounds());
}

juce::String ModMatrixPicker::getSearchPlaceholderForTest() const { return searchEditor_->getTextToShowWhenEmpty(); }

bool ModMatrixPicker::isVisibleItemPickableForTest(int index) const {
    return index >= 0 && index < (int)visibleItems_.size() && itemAtVisible(index).enabled;
}

void ModMatrixPicker::chooseVisibleItemForTest(int index) {
    if (index >= 0 && index < (int)visibleItems_.size())
        chooseEntry(visibleItems_[(size_t)index]);
}

bool ModMatrixPicker::sendKeyForTest(const juce::KeyPress& key) { return searchEditor_->keyPressed(key); }

juce::TextEditor& ModMatrixPicker::getSearchEditorForTest() noexcept { return *searchEditor_; }

} // namespace synth::ui
