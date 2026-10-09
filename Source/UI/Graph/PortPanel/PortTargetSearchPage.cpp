// PortTargetSearchPage.cpp -- the port panel's "Add connection" page: groups, rows, search, keys.
// docs/layout/cables.md#port-connections-panel.

#include "PortTargetSearchPage.h"

#include "UI/Layout/FocusRing.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kBackY = 8;
constexpr int kSearchY = kBackY + PortTargetSearchPage::kBackHeight + 4;
constexpr int kListY = kSearchY + PortTargetSearchPage::kSearchHeight + 4;
constexpr int kBottomPad = 6;
constexpr int kEmptyListHeight = 52;
constexpr juce::uint32 kNewSectionKey = 0xFFFFFFFFu; // a node uid is never this
} // namespace

// One module (or the "New module" group): its header, when it has several rows, and its rows. The section's own
// bounds are as tall as the fold leaves room for; the rows keep their natural places, so a folding group reveals and
// hides them rather than squashing them. A module with one target has no header and no fold.
struct PortTargetSearchPage::Section final : juce::Component {
    struct Item {
        PortTarget target;
        std::unique_ptr<ModDotChoiceRow> row;
    };

    Section(juce::uint32 k, juce::String name, bool hasHeader)
        : key(k)
        , grouped(hasHeader)
        , header(name) {
        addChildComponent(header);
        header.setVisible(hasHeader);
    }

    int rowsHeight() const { return visibleRows * ModDotChoiceRow::kHeight; }
    int heightNow() const {
        if (!shown)
            return 0;
        return grouped ? ModDotGroupHeader::kHeight + juce::roundToInt((float)rowsHeight() * fold) : rowsHeight();
    }
    void layoutChildren() {
        int y = 0;
        if (grouped) {
            header.setBounds(0, 0, getWidth(), ModDotGroupHeader::kHeight);
            y = ModDotGroupHeader::kHeight;
        }
        for (auto& item : items) {
            if (item.row->isVisible()) {
                item.row->setBounds(0, y, getWidth(), ModDotChoiceRow::kHeight);
                y += ModDotChoiceRow::kHeight;
            }
        }
    }

    juce::uint32 key;
    bool grouped;
    ModDotGroupHeader header;
    std::vector<Item> items;
    int visibleRows = 0;
    bool shown = true;
    float fold = 1.0f, foldFrom = 1.0f, foldTo = 1.0f;
};

PortTargetSearchPage::PortTargetSearchPage(juce::String jackTitle)
    : jackTitle_(std::move(jackTitle))
    , back_("Back", "Back to connections")
    , search_(std::make_unique<NavigationSearchField>())
    , updater_(this) {
    setTitle("Add a connection to " + jackTitle_);
    setComponentID("portTargetSearchPage");

    back_.setTooltip("Back to this jack's connections. Escape does the same");
    back_.onClick = [this] {
        if (onBackRequested)
            onBackRequested();
    };
    addAndMakeVisible(back_);

    search_->setTitle("Search jacks and knobs");
    search_->setTooltip(
        "Type to filter. Down moves into the results, Return connects the best match, Escape goes back.");
    search_->setMultiLine(false);
    search_->setReturnKeyStartsNewLine(false);
    search_->setJustification(juce::Justification::centredLeft);
    search_->setBorder(juce::BorderSize<int>(0));
    search_->setIndents(14, 0);
    search_->setFont(juce::Font(juce::FontOptions(12.5f)));
    search_->setTextToShowWhenEmpty("Search jacks and modules", juce::Colours::grey);
    search_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search_->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search_->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search_->onTextChange = [this] { setQuery(search_->getText()); };
    search_->onNavigationKey = [this](const juce::KeyPress& key) {
        if (key == juce::KeyPress::downKey) {
            const auto stops = focusStops();
            auto it = std::find_if(stops.begin(), stops.end(), [this](juce::Component* c) {
                return query_.isEmpty() || dynamic_cast<ModDotChoiceRow*>(c) != nullptr;
            });
            if (it != stops.end())
                (*it)->grabKeyboardFocus();
            return true;
        }
        if (key == juce::KeyPress::upKey)
            return true;
        if (key == juce::KeyPress::returnKey) {
            pickBestMatch();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
            return stepBack();
        return false;
    };
    // The editor posts Return/Escape as a message when its own keyPressed does not see them; same actions.
    search_->onReturnKey = [this] { pickBestMatch(); };
    search_->onEscapeKey = [this] { stepBack(); };
    addAndMakeVisible(*search_);

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&list_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setWantsKeyboardFocus(false);
    setSize(kWidth, preferredHeight());
}

PortTargetSearchPage::~PortTargetSearchPage() { anim_.stop(updater_); }

void PortTargetSearchPage::setTargets(std::vector<PortTargetModule> modules, std::vector<PortTarget> newModules) {
    juce::StringArray signature;
    for (const auto& m : modules)
        for (const auto& t : m.targets)
            signature.add(t.label() + (t.connected ? "|c" : "|") + juce::String(t.knobChannel));
    for (const auto& t : newModules)
        signature.add("new|" + t.newType);
    if (signature == signature_ && !sections_.empty())
        return;
    signature_ = signature;
    modules_ = std::move(modules);
    newModules_ = std::move(newModules);
    rebuild();
}

void PortTargetSearchPage::reset() {
    search_->setText({}, false);
    query_ = {};
    searchCollapsed_.clear();
    applyState(false);
    focusEntry();
}

void PortTargetSearchPage::focusEntry() { search_->grabKeyboardFocus(); }

void PortTargetSearchPage::previewRow(const ModDotChoiceRow& row, bool lit) {
    if (!onPreview)
        return;
    if (!lit) {
        onPreview(nullptr);
        return;
    }
    onPreview(targetOf(row));
}

const PortTarget* PortTargetSearchPage::targetOf(const ModDotChoiceRow& row) const {
    for (const auto& s : sections_)
        for (const auto& item : s->items)
            if (item.row.get() == &row)
                return &item.target;
    return nullptr;
}

void PortTargetSearchPage::rebuild() {
    if (onPreview)
        onPreview(nullptr); // the rows a preview came from are about to go
    sections_.clear();
    list_.removeAllChildren();
    auto addSection = [this](juce::uint32 key, const juce::String& name, const std::vector<PortTarget>& targets) {
        if (targets.empty())
            return;
        const bool isNewGroup = key == kNewSectionKey;
        const bool grouped = isNewGroup || targets.size() > 1;
        auto section = std::make_unique<Section>(key, name, grouped);
        for (const auto& target : targets) {
            ModSourceItem item;
            // A grouped row shows its port under the module's header, a lone one "module . port". The page matches on
            // the target's own search text (module, port and aliases), so what the row paints is only what is shown.
            item.moduleTitle = (grouped && !isNewGroup) ? target.portName : target.label();
            item.aliases = target.aliases;
            auto row = std::make_unique<ModDotChoiceRow>(
                item, target.connected, target.connected ? juce::String("Connected") : juce::String(), target.newType);
            const auto full = target.label();
            if (target.isNew()) {
                row->setTitle(full + ", new module");
                row->setTooltip("Create " + full + " and connect it");
            } else if (target.connected) {
                row->setTitle(full + ", connected");
                row->setTooltip(full + " is already connected to this jack");
            } else {
                row->setTitle(full);
                row->setTooltip(target.kind == PortTarget::Kind::Knob ? "Modulate " + full : "Connect to " + full);
            }
            row->onHighlight = [this](const ModDotChoiceRow& r, bool lit) { previewRow(r, lit); };
            section->addAndMakeVisible(*row);
            section->items.push_back({target, std::move(row)});
        }
        for (auto& item : section->items)
            item.row->onPick = [this, t = &item.target](const ModDotChoiceRow&) {
                if (onPick)
                    onPick(*t);
            };
        if (grouped && userExpanded_.find(key) == userExpanded_.end())
            userExpanded_[key] = true;
        section->fold = section->foldFrom = section->foldTo = effectiveExpanded(*section) ? 1.0f : 0.0f;
        const auto raw = section.get();
        raw->header.onClick = [this, raw] { toggleSection(*raw, !effectiveExpanded(*raw)); };
        raw->header.onSetExpanded = [this, raw](bool expand) { toggleSection(*raw, expand); };
        list_.addAndMakeVisible(*section);
        sections_.push_back(std::move(section));
    };
    for (const auto& m : modules_)
        addSection(m.node.uid, m.title, m.targets);
    addSection(kNewSectionKey, "New module", newModules_);
    applyState(false);
}

bool PortTargetSearchPage::effectiveExpanded(const Section& section) const {
    if (!section.grouped)
        return true;
    return query_.isNotEmpty() ? searchCollapsed_.count(section.key) == 0 : userExpanded_.at(section.key);
}

// A "New <module>" row is offered only for a typed query.
bool PortTargetSearchPage::rowMatches(const PortTarget& target) const {
    if (target.isNew() && query_.isEmpty())
        return false;
    return searchMatches(target.searchText(), query_);
}

void PortTargetSearchPage::setQuery(const juce::String& text) {
    const auto query = text.trim();
    if (search_->getText() != text)
        search_->setText(text, false);
    if (query == query_)
        return;
    query_ = query;
    searchCollapsed_.clear();
    applyState(true);
}

void PortTargetSearchPage::toggleSection(const Section& section, bool expand) {
    if (query_.isNotEmpty()) {
        if (expand)
            searchCollapsed_.erase(section.key);
        else
            searchCollapsed_.insert(section.key);
    } else {
        userExpanded_[section.key] = expand;
    }
    applyState(true);
}

// Works out each group's rows (the ones the query matches), whether it is shown and where its fold is heading, then
// tweens every fold from where it is to there (landing at once when not on screen).
void PortTargetSearchPage::applyState(bool animate) {
    for (auto& section : sections_) {
        int visible = 0;
        for (auto& item : section->items) {
            const bool match = rowMatches(item.target);
            item.row->setVisible(match);
            item.row->setQuery(query_);
            visible += match ? 1 : 0;
        }
        section->visibleRows = visible;
        section->shown = visible > 0;
        section->setVisible(section->shown);
        section->foldFrom = section->fold;
        section->foldTo = effectiveExpanded(*section) ? 1.0f : 0.0f;
        section->header.setCount(visible);
        section->header.setExpanded(section->foldTo > 0.5f, section->fold);
        const bool focusable = section->foldTo > 0.5f;
        for (auto& item : section->items)
            item.row->setWantsKeyboardFocus(focusable);
    }
    const bool moving =
        std::any_of(sections_.begin(), sections_.end(), [](const auto& s) { return s->foldFrom != s->foldTo; });
    if (!animate || !moving || !modDotMotionAllowed(*this)) {
        anim_.stop(updater_);
        for (auto& s : sections_) {
            s->fold = s->foldTo;
            s->header.setExpanded(s->foldTo > 0.5f, s->fold);
        }
        layoutList();
        return;
    }
    anim_.start(
        updater_, kFoldMs, easeOutCubic,
        [this](float t) {
            for (auto& s : sections_) {
                s->fold = s->foldFrom + (s->foldTo - s->foldFrom) * t;
                s->header.setExpanded(s->foldTo > 0.5f, s->fold);
            }
            layoutList();
        },
        [this] {
            for (auto& s : sections_)
                s->fold = s->foldTo;
            layoutList();
        });
}

void PortTargetSearchPage::layoutList() {
    const int width = juce::jmax(0, viewport_.getMaximumVisibleWidth());
    int y = 0;
    for (auto& s : sections_) {
        const int h = s->heightNow();
        s->setBounds(0, y, width, h);
        s->layoutChildren();
        y += h;
    }
    list_.setSize(width, juce::jmax(y, 1));
    const bool changed = y != listHeight_;
    listHeight_ = y;
    if (changed || viewport_.getHeight() != listViewHeight())
        heightChanged();
    repaint();
}

// The list is as tall as its rows (a few at least, so "Nothing matches" has room), up to the cap, then scrolls.
int PortTargetSearchPage::listViewHeight() const {
    int cap = kMaxListHeight;
    if (maxHeight_ > 0)
        cap = juce::jlimit(kEmptyListHeight, kMaxListHeight, maxHeight_ - kListY - kBottomPad);
    return juce::jmin(juce::jmax(listHeight_, kEmptyListHeight), cap);
}

int PortTargetSearchPage::preferredHeight() const { return kListY + listViewHeight() + kBottomPad; }

void PortTargetSearchPage::setMaxHeight(int height) {
    if (height == maxHeight_)
        return;
    maxHeight_ = height;
    heightChanged();
}

void PortTargetSearchPage::resized() {
    back_.setBounds(8, kBackY, back_.preferredWidth(), kBackHeight);
    search_->setBounds(10, kSearchY, getWidth() - 20, kSearchHeight);
    viewport_.setBounds(0, kListY, getWidth(), juce::jmax(0, getHeight() - kListY - kBottomPad));
    layoutList();
}

void PortTargetSearchPage::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    g.setColour(p.muted);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText("Connect " + jackTitle_,
               juce::Rectangle<int>(back_.getRight() + 6, kBackY, getWidth() - back_.getRight() - 14, kBackHeight),
               juce::Justification::centredLeft, true);

    const auto pill = juce::Rectangle<float>(10.0f, (float)kSearchY, (float)getWidth() - 20.0f, (float)kSearchHeight);
    g.setColour(p.field);
    g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
    g.setColour(p.border);
    g.drawRoundedRectangle(pill.reduced(0.5f), pill.getHeight() * 0.5f, 1.0f);
    if (search_->hasKeyboardFocus(true))
        paintFocusRingAlways(g, pill, *this, pill.getHeight() * 0.5f);

    if (!showsNoMatch())
        return;
    g.setColour(p.muted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(emptyText(), juce::Rectangle<int>(0, kListY, getWidth(), kEmptyListHeight),
               juce::Justification::centred);
}

bool PortTargetSearchPage::showsNoMatch() const {
    return std::none_of(sections_.begin(), sections_.end(), [](const auto& s) { return s->shown; });
}

juce::String PortTargetSearchPage::emptyText() const {
    return modules_.empty() ? juce::String("Nothing to connect to yet") : juce::String("Nothing matches");
}

bool PortTargetSearchPage::stepBack() {
    if (query_.isNotEmpty()) {
        setQuery({});
        search_->grabKeyboardFocus();
        return true;
    }
    if (onBackRequested)
        onBackRequested();
    return true;
}

// The best-scoring target wins; a "New <module>" row is picked only when no existing target matches. A connected
// target cannot be picked.
void PortTargetSearchPage::pickBestMatch() {
    const Section::Item* best = nullptr;
    int bestScore = 0;
    for (const bool wantNew : {false, true}) {
        for (auto& s : sections_)
            for (auto& item : s->items) {
                if (!item.row->isVisible() || item.target.connected || s->foldTo < 0.5f ||
                    item.target.isNew() != wantNew)
                    continue;
                const int score = searchScore(item.target.searchText(), query_);
                if (best == nullptr || score < bestScore) {
                    best = &item;
                    bestScore = score;
                }
            }
        if (best != nullptr)
            break;
    }
    if (best != nullptr)
        best->row->pick();
}

std::vector<juce::Component*> PortTargetSearchPage::focusStops() const {
    std::vector<juce::Component*> stops;
    for (const auto& s : sections_) {
        if (!s->shown)
            continue;
        if (s->grouped)
            stops.push_back(&s->header);
        if (s->foldTo < 0.5f)
            continue;
        for (const auto& item : s->items)
            if (item.row->isVisible())
                stops.push_back(item.row.get());
    }
    return stops;
}

void PortTargetSearchPage::moveStop(juce::Component* from, int step) {
    const auto stops = focusStops();
    int index = -1;
    for (int i = 0; i < (int)stops.size(); ++i)
        if (stops[(size_t)i] == from)
            index = i;
    if (from == nullptr || index < 0)
        return;
    index += step;
    if (index < 0) {
        search_->grabKeyboardFocus();
        return;
    }
    index = juce::jmin(index, (int)stops.size() - 1);
    auto* target = stops[(size_t)index];
    target->grabKeyboardFocus();
    const auto area = list_.getLocalArea(target, target->getLocalBounds());
    const auto view = viewport_.getViewArea();
    if (area.getY() < view.getY())
        viewport_.setViewPosition(0, area.getY());
    else if (area.getBottom() > view.getBottom())
        viewport_.setViewPosition(0, area.getBottom() - view.getHeight());
}

bool PortTargetSearchPage::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey)) {
        moveStop(juce::Component::getCurrentlyFocusedComponent(), key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1);
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
        return stepBack();
    return false;
}

std::vector<juce::String> PortTargetSearchPage::visibleGroupNames() const {
    std::vector<juce::String> names;
    for (const auto& s : sections_)
        if (s->shown && s->grouped)
            names.push_back(s->header.getName());
    return names;
}

std::vector<juce::String> PortTargetSearchPage::visibleRowLabels() const {
    std::vector<juce::String> labels;
    for (const auto& s : sections_)
        if (s->shown && s->foldTo > 0.5f)
            for (const auto& item : s->items)
                if (item.row->isVisible())
                    labels.push_back(item.row->item().label());
    return labels;
}

ModDotChoiceRow* PortTargetSearchPage::visibleRow(int index) const {
    int n = 0;
    for (const auto& s : sections_)
        if (s->shown && s->foldTo > 0.5f)
            for (const auto& item : s->items)
                if (item.row->isVisible() && n++ == index)
                    return item.row.get();
    return nullptr;
}

ModDotGroupHeader* PortTargetSearchPage::header(const juce::String& groupName) const {
    for (const auto& s : sections_)
        if (s->shown && s->grouped && s->header.getName() == groupName)
            return &s->header;
    return nullptr;
}

bool PortTargetSearchPage::isGroupExpanded(const juce::String& groupName) const {
    for (const auto& s : sections_)
        if (s->grouped && s->header.getName() == groupName)
            return s->foldTo > 0.5f;
    return false;
}

} // namespace synth::ui
