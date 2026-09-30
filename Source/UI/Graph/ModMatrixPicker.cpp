#include "ModMatrixPicker.h"

#include "UI/Layout/FocusRegion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kWidth = 300;
constexpr int kMaxHeight = 360;
constexpr int kOuterPadding = 6;
constexpr int kSearchHeight = 26;
constexpr int kRowHeight = 24;
constexpr int kHeaderHeight = 20;
constexpr float kRowFontSize = 13.0f;
constexpr float kHeaderFontSize = 11.0f;

struct Palette {
    juce::Colour panel = juce::Colours::darkgrey.darker(0.4f);
    juce::Colour border = juce::Colours::grey.darker();
    juce::Colour field = juce::Colours::black.withAlpha(0.35f);
    juce::Colour text = juce::Colours::white;
    juce::Colour muted = juce::Colours::grey;
    juce::Colour accent = juce::Colours::lightblue;
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
        p.radius = lf->getTheme().metrics.cornerRadius;
    }
    return p;
}
} // namespace

// One header or item line. Items report a click and their own hover so the picker keeps the
// highlight; headers are inert.
class ModMatrixPicker::Row
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    enum class Kind { Header, Item };

    Row(Kind kind, int id, juce::String text, bool isCurrent, std::function<void(Row&)> onClick,
        std::function<void(Row&)> onHover)
        : kind_(kind)
        , id_(id)
        , text_(std::move(text))
        , isCurrent_(isCurrent)
        , onClick_(std::move(onClick))
        , onHover_(std::move(onHover)) {
        setTitle(text_);
        if (kind_ == Kind::Header) {
            setInterceptsMouseClicks(false, false);
        } else {
            setTooltip(text_);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
    }

    Kind kind() const noexcept { return kind_; }
    int id() const noexcept { return id_; }
    const juce::String& text() const noexcept { return text_; }
    int preferredHeight() const noexcept { return kind_ == Kind::Header ? kHeaderHeight : kRowHeight; }

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

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        if (kind_ == Kind::Header) {
            g.setColour(palette_.muted);
            g.setFont(juce::Font(juce::FontOptions(kHeaderFontSize, juce::Font::bold)));
            g.drawText(text_.toUpperCase(), bounds.reduced(6, 0), juce::Justification::centredLeft, true);
            return;
        }
        if (highlighted_) {
            g.setColour(palette_.accent.withAlpha(0.25f));
            g.fillRoundedRectangle(bounds.toFloat().reduced(2.0f, 1.0f), 3.0f);
        }
        // The row a combo currently holds is set apart by colour and weight, not by a glyph.
        g.setColour(isCurrent_ ? palette_.accent : palette_.text);
        g.setFont(juce::Font(juce::FontOptions(kRowFontSize, isCurrent_ ? juce::Font::bold : juce::Font::plain)));
        g.drawText(text_, bounds.reduced(12, 0), juce::Justification::centredLeft, true);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        if (onHover_)
            onHover_(*this);
    }
    void mouseMove(const juce::MouseEvent&) override {
        if (onHover_ && !highlighted_)
            onHover_(*this);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (kind_ == Kind::Item && onClick_ && getLocalBounds().contains(e.getPosition()))
            onClick_(*this);
    }

private:
    Kind kind_;
    int id_;
    juce::String text_;
    bool isCurrent_;
    std::function<void(Row&)> onClick_;
    std::function<void(Row&)> onHover_;
    Palette palette_;
    bool highlighted_ = false;
};

// A single-line editor that claims Up/Down/Return/Escape for the list. Its own caret handling would
// swallow the arrows, and it reports Return and Escape through a posted command message, which would
// make a pick land a message-loop turn late.
class ModMatrixPicker::SearchField : public juce::TextEditor {
public:
    std::function<bool(const juce::KeyPress&)> onNavigationKey;

    bool keyPressed(const juce::KeyPress& key) override {
        if (onNavigationKey && onNavigationKey(key))
            return true;
        return juce::TextEditor::keyPressed(key);
    }
};

ModMatrixPicker::ModMatrixPicker(juce::String what, std::vector<Item> items, int selectedId,
                                 std::function<void(int)> onChoose)
    : what_(std::move(what))
    , items_(std::move(items))
    , selectedId_(selectedId)
    , onChoose_(std::move(onChoose))
    , searchEditor_(std::make_unique<SearchField>()) {
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

    rebuildRows();
    applyColours();
    setSize(kWidth, preferredHeight());
}

ModMatrixPicker::~ModMatrixPicker() = default;

void ModMatrixPicker::rebuildRows() {
    rowColumn_.removeAllChildren();
    rows_.clear();

    juce::String currentCategory;
    bool first = true;
    for (const auto& item : items_) {
        if (item.category.isNotEmpty() && (first || item.category != currentCategory)) {
            rows_.push_back(std::make_unique<Row>(Row::Kind::Header, 0, item.category, false, nullptr, nullptr));
            rowColumn_.addAndMakeVisible(*rows_.back());
        }
        first = false;
        currentCategory = item.category;
        rows_.push_back(std::make_unique<Row>(
            Row::Kind::Item, item.id, item.text, item.id == selectedId_, [this](Row& r) { chooseRow(r); },
            [this](Row& r) {
                const auto visible = visibleItemRows();
                const auto it = std::find(visible.begin(), visible.end(), &r);
                if (it != visible.end())
                    setHighlight((int)(it - visible.begin()));
            }));
        rowColumn_.addAndMakeVisible(*rows_.back());
    }
    applyFilter();
}

std::vector<ModMatrixPicker::Row*> ModMatrixPicker::visibleItemRows() const {
    std::vector<Row*> out;
    for (const auto& row : rows_)
        if (row->kind() == Row::Kind::Item && row->isVisible())
            out.push_back(row.get());
    return out;
}

// A header shows only while some row under it matches; the height is deliberately not recomputed
// here (see preferredHeight), so the popup keeps its size while the list shrinks.
void ModMatrixPicker::applyFilter() {
    const auto query = searchEditor_->getText().trim();
    Row* pendingHeader = nullptr;
    bool headerHasMatch = false;
    for (const auto& row : rows_) {
        if (row->kind() == Row::Kind::Header) {
            if (pendingHeader != nullptr)
                pendingHeader->setVisible(headerHasMatch);
            pendingHeader = row.get();
            headerHasMatch = false;
            continue;
        }
        const bool matches = query.isEmpty() || row->text().containsIgnoreCase(query);
        row->setVisible(matches);
        headerHasMatch = headerHasMatch || matches;
    }
    if (pendingHeader != nullptr)
        pendingHeader->setVisible(headerHasMatch);

    layoutRowColumn();
    setHighlight(0);
    viewport_.setViewPosition(0, 0);
}

void ModMatrixPicker::layoutRowColumn() {
    const int width = juce::jmax(0, viewport_.getMaximumVisibleWidth());
    int y = 0;
    for (const auto& row : rows_) {
        if (!row->isVisible())
            continue;
        row->setBounds(0, y, width, row->preferredHeight());
        y += row->preferredHeight();
    }
    rowColumn_.setSize(width, juce::jmax(y, 1));
}

// The whole unfiltered list decides the height (capped), so typing never resizes the popup.
int ModMatrixPicker::preferredHeight() const {
    int rowsHeight = 0;
    for (const auto& row : rows_)
        rowsHeight += row->preferredHeight();
    const int content = kOuterPadding + kSearchHeight + 4 + juce::jmax(rowsHeight, kRowHeight) + kOuterPadding;
    return juce::jlimit(kSearchHeight + kRowHeight + 2 * kOuterPadding + 4, kMaxHeight, content);
}

void ModMatrixPicker::resized() {
    auto bounds = getLocalBounds().reduced(kOuterPadding);
    searchEditor_->setBounds(bounds.removeFromTop(kSearchHeight));
    bounds.removeFromTop(4);
    viewport_.setBounds(bounds);
    layoutRowColumn();
}

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
    searchEditor_->setTextToShowWhenEmpty("Search " + what_ + "s...", p.muted);
    for (const auto& row : rows_)
        row->setPalette(p);
    repaint();
}

void ModMatrixPicker::lookAndFeelChanged() { applyColours(); }

// A CallOutBox attaches its content after construction, so the LookAndFeel and the focus target
// only become real once this component has a parent.
void ModMatrixPicker::parentHierarchyChanged() {
    applyColours();
    if (isShowing())
        searchEditor_->grabKeyboardFocus();
}

void ModMatrixPicker::setHighlight(int visibleItemIndex) {
    const auto visible = visibleItemRows();
    if (visible.empty()) {
        highlighted_ = 0;
        return;
    }
    highlighted_ = juce::jlimit(0, (int)visible.size() - 1, visibleItemIndex);
    for (int i = 0; i < (int)visible.size(); ++i)
        visible[(size_t)i]->setHighlighted(i == highlighted_);

    // Keep the highlighted row inside the viewport.
    const auto rowBounds = visible[(size_t)highlighted_]->getBounds();
    const auto view = viewport_.getViewArea();
    if (rowBounds.getY() < view.getY())
        viewport_.setViewPosition(0, rowBounds.getY());
    else if (rowBounds.getBottom() > view.getBottom())
        viewport_.setViewPosition(0, rowBounds.getBottom() - view.getHeight());
}

void ModMatrixPicker::moveHighlight(int delta) { setHighlight(highlighted_ + delta); }

void ModMatrixPicker::chooseHighlighted() {
    const auto visible = visibleItemRows();
    if (!visible.empty())
        chooseRow(*visible[(size_t)juce::jlimit(0, (int)visible.size() - 1, highlighted_)]);
}

// Reports the pick, then closes. The callback is copied first: closing is asynchronous, but nothing
// here may depend on that.
void ModMatrixPicker::chooseRow(const Row& row) {
    const int id = row.id();
    auto callback = onChoose_;
    dismiss();
    if (callback)
        callback(id);
}

void ModMatrixPicker::dismiss() {
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

// ---- Test seams ----

void ModMatrixPicker::setSearchTextForTest(const juce::String& text) {
    searchEditor_->setText(text, juce::dontSendNotification);
    applyFilter();
}

juce::String ModMatrixPicker::getSearchTextForTest() const { return searchEditor_->getText(); }

std::vector<juce::String> ModMatrixPicker::getVisibleRowNamesForTest() const {
    std::vector<juce::String> names;
    for (const auto& row : rows_)
        if (row->isVisible())
            names.push_back(row->text());
    return names;
}

std::vector<juce::String> ModMatrixPicker::getVisibleItemTextsForTest() const {
    std::vector<juce::String> names;
    for (auto* row : visibleItemRows())
        names.push_back(row->text());
    return names;
}

void ModMatrixPicker::chooseVisibleItemForTest(int index) {
    const auto visible = visibleItemRows();
    if (index >= 0 && index < (int)visible.size())
        chooseRow(*visible[(size_t)index]);
}

bool ModMatrixPicker::sendKeyForTest(const juce::KeyPress& key) { return searchEditor_->keyPressed(key); }

juce::TextEditor& ModMatrixPicker::getSearchEditorForTest() noexcept { return *searchEditor_; }

} // namespace synth::ui
