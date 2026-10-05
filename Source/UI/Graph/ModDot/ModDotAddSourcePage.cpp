#include "ModDotAddSourcePage.h"

#include "UI/Layout/FocusRing.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kSearchY = 8;
constexpr int kLinksY = kSearchY + ModDotAddSourcePage::kSearchHeight + 4;
constexpr int kListY = kLinksY + ModDotAddSourcePage::kLinksHeight + 2;
constexpr int kBottomPad = 6;
constexpr int kEmptyListHeight = 52;

const ModSourceGroup kGroupOrder[] = {ModSourceGroup::Lfos,     ModSourceGroup::Envelopes,  ModSourceGroup::Macros,
                                      ModSourceGroup::Midi,     ModSourceGroup::Sequencers, ModSourceGroup::Oscillators,
                                      ModSourceGroup::Filters,  ModSourceGroup::Effects,    ModSourceGroup::Other,
                                      ModSourceGroup::NewModule};
} // namespace

// One group: its header and its rows. The section's own bounds are as tall as the fold leaves room for; the rows
// keep their natural places, so a folding group reveals and hides them rather than squashing them.
struct ModDotAddSourcePage::Section final : juce::Component {
    Section(ModSourceGroup g)
        : group(g)
        , header(modSourceGroupName(g)) {
        addAndMakeVisible(header);
    }

    int rowsHeight() const { return visibleRows * ModDotChoiceRow::kHeight; }
    int heightNow() const {
        return shown ? ModDotGroupHeader::kHeight + juce::roundToInt((float)rowsHeight() * fold) : 0;
    }
    void layoutChildren() {
        header.setBounds(0, 0, getWidth(), ModDotGroupHeader::kHeight);
        int y = ModDotGroupHeader::kHeight;
        for (auto& row : rows) {
            if (row->isVisible()) {
                row->setBounds(0, y, getWidth(), ModDotChoiceRow::kHeight);
                y += ModDotChoiceRow::kHeight;
            }
        }
    }

    ModSourceGroup group;
    ModDotGroupHeader header;
    std::vector<std::unique_ptr<ModDotChoiceRow>> rows;
    int visibleRows = 0;
    bool shown = true;
    float fold = 1.0f, foldFrom = 1.0f, foldTo = 1.0f;
};

ModDotAddSourcePage::ModDotAddSourcePage(juce::String paramName)
    : paramName_(std::move(paramName))
    , search_(std::make_unique<NavigationSearchField>())
    , expandAll_("Expand all", "Expand all groups")
    , collapseAll_("Collapse all", "Collapse all groups")
    , updater_(this) {
    setTitle("Source list for " + paramName_);

    search_->setTitle("Search sources");
    search_->setTooltip(
        "Type to filter. Down moves into the results, Return adds the best match, Escape closes the list.");
    search_->setMultiLine(false);
    search_->setReturnKeyStartsNewLine(false);
    search_->setJustification(juce::Justification::centredLeft);
    search_->setBorder(juce::BorderSize<int>(0));
    search_->setIndents(14, 0);
    search_->setFont(juce::Font(juce::FontOptions(12.5f)));
    search_->setTextToShowWhenEmpty("Search sources", juce::Colours::grey);
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

    expandAll_.onClick = [this] { setAll(true); };
    collapseAll_.onClick = [this] { setAll(false); };
    addAndMakeVisible(expandAll_);
    addAndMakeVisible(collapseAll_);

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&list_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setWantsKeyboardFocus(false);
    setSize(kWidth, preferredHeight());
}

ModDotAddSourcePage::~ModDotAddSourcePage() { anim_.stop(updater_); }

void ModDotAddSourcePage::setChoices(std::vector<Choice> choices) {
    choices_ = std::move(choices);
    rebuild();
}

void ModDotAddSourcePage::reset() {
    search_->setText({}, false);
    query_ = {};
    searchCollapsed_.clear();
    applyState(false);
    focusEntry();
}

void ModDotAddSourcePage::focusEntry() { search_->grabKeyboardFocus(); }

void ModDotAddSourcePage::rebuild() {
    sections_.clear();
    list_.removeAllChildren();
    for (const auto group : kGroupOrder) {
        auto section = std::make_unique<Section>(group);
        for (const auto& choice : choices_) {
            if (choice.item.group != group)
                continue;
            auto row = std::make_unique<ModDotChoiceRow>(
                choice.item, choice.added,
                choice.newType.isEmpty() ? modSourceUsageText(choice.targets) : juce::String(), choice.newType);
            row->onPick = [this](const ModDotChoiceRow& picked) {
                if (picked.isNew()) {
                    if (onPickNew)
                        onPickNew(picked.newType(), picked.item().channel);
                } else if (onPick) {
                    onPick(picked.item());
                }
            };
            section->addAndMakeVisible(*row);
            section->rows.push_back(std::move(row));
        }
        if (section->rows.empty())
            continue;
        const auto g = group;
        section->header.onClick = [this, g] { toggleGroup(g, !effectiveExpanded(g)); };
        section->header.onSetExpanded = [this, g](bool expand) { toggleGroup(g, expand); };
        if (const auto it = userExpanded_.find(group); it == userExpanded_.end())
            userExpanded_[group] = true;
        section->fold = section->foldFrom = section->foldTo = effectiveExpanded(group) ? 1.0f : 0.0f;
        list_.addAndMakeVisible(*section);
        sections_.push_back(std::move(section));
    }
    applyState(false);
}

bool ModDotAddSourcePage::effectiveExpanded(ModSourceGroup group) const {
    return query_.isNotEmpty() ? searchCollapsed_.count(group) == 0 : userExpanded_.at(group);
}

// A "New <module>" row is offered only for a typed query, matched against the module's own name.
bool ModDotAddSourcePage::rowMatches(const ModDotChoiceRow& row) const {
    if (row.isNew() && query_.isEmpty())
        return false;
    return searchMatches(row.matchText(), query_);
}

void ModDotAddSourcePage::setQuery(const juce::String& text) {
    const auto query = text.trim();
    if (search_->getText() != text)
        search_->setText(text, false);
    if (query == query_)
        return;
    query_ = query;
    searchCollapsed_.clear();
    applyState(true);
}

void ModDotAddSourcePage::toggleGroup(ModSourceGroup group, bool expand) {
    if (query_.isNotEmpty()) {
        if (expand)
            searchCollapsed_.erase(group);
        else
            searchCollapsed_.insert(group);
    } else {
        userExpanded_[group] = expand;
    }
    applyState(true);
}

void ModDotAddSourcePage::setAll(bool expand) {
    for (const auto& section : sections_) {
        if (query_.isNotEmpty()) {
            if (expand)
                searchCollapsed_.erase(section->group);
            else
                searchCollapsed_.insert(section->group);
        } else {
            userExpanded_[section->group] = expand;
        }
    }
    applyState(true);
}

// Works out each group's rows (the ones the query matches), whether it is shown and where its fold is heading, then
// tweens every fold from where it is to there (landing at once when not on screen).
void ModDotAddSourcePage::applyState(bool animate) {
    for (auto& section : sections_) {
        int visible = 0;
        for (auto& row : section->rows) {
            const bool match = rowMatches(*row);
            row->setVisible(match);
            row->setQuery(query_);
            visible += match ? 1 : 0;
        }
        section->visibleRows = visible;
        section->shown = visible > 0;
        section->setVisible(section->shown);
        section->foldFrom = section->fold;
        section->foldTo = effectiveExpanded(section->group) ? 1.0f : 0.0f;
        section->header.setCount(visible);
        section->header.setExpanded(section->foldTo > 0.5f, section->fold);
        const bool focusable = section->foldTo > 0.5f;
        for (auto& row : section->rows)
            row->setWantsKeyboardFocus(focusable);
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

void ModDotAddSourcePage::layoutList() {
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

// The list is as tall as its rows (a few at least, so "No source matches" has room), up to the cap, then scrolls.
int ModDotAddSourcePage::listViewHeight() const {
    int cap = kMaxListHeight;
    if (maxHeight_ > 0)
        cap = juce::jlimit(kEmptyListHeight, kMaxListHeight, maxHeight_ - kListY - kBottomPad);
    return juce::jmin(juce::jmax(listHeight_, kEmptyListHeight), cap);
}

int ModDotAddSourcePage::preferredHeight() const { return kListY + listViewHeight() + kBottomPad; }

void ModDotAddSourcePage::setMaxHeight(int height) {
    if (height == maxHeight_)
        return;
    maxHeight_ = height;
    heightChanged();
}

void ModDotAddSourcePage::resized() {
    search_->setBounds(10, kSearchY, getWidth() - 20, kSearchHeight);
    expandAll_.setBounds(8, kLinksY, expandAll_.preferredWidth(), kLinksHeight);
    collapseAll_.setBounds(expandAll_.getRight() + 2, kLinksY, collapseAll_.preferredWidth(), kLinksHeight);
    viewport_.setBounds(0, kListY, getWidth(), getHeight() - kListY - kBottomPad);
    layoutList();
}

void ModDotAddSourcePage::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    g.setColour(p.border);
    g.fillRect(juce::Rectangle<int>(8, 0, getWidth() - 16, 1));

    const auto pill = juce::Rectangle<float>(10.0f, (float)kSearchY, (float)getWidth() - 20.0f, (float)kSearchHeight);
    g.setColour(p.field);
    g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
    g.setColour(p.border);
    g.drawRoundedRectangle(pill.reduced(0.5f), pill.getHeight() * 0.5f, 1.0f);
    if (search_->hasKeyboardFocus(true))
        paintFocusRingAlways(g, pill, *this, pill.getHeight() * 0.5f);

    if (!showsNoMatch() && !choices_.empty())
        return;
    g.setColour(p.muted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(choices_.empty() ? "No sources in the project" : "No source matches",
               juce::Rectangle<int>(0, kListY, getWidth(), kEmptyListHeight), juce::Justification::centred);
}

bool ModDotAddSourcePage::showsNoMatch() const {
    return std::none_of(sections_.begin(), sections_.end(), [](const auto& s) { return s->shown; });
}

bool ModDotAddSourcePage::stepBack() {
    if (query_.isNotEmpty()) {
        setQuery({});
        search_->grabKeyboardFocus();
        return true;
    }
    if (onCollapseRequested)
        onCollapseRequested();
    return true;
}

// The best-scoring source wins; a "New <module>" row is picked only when no existing source matches.
void ModDotAddSourcePage::pickBestMatch() {
    ModDotChoiceRow* best = nullptr;
    int bestScore = 0;
    for (const bool wantNew : {false, true}) {
        for (auto& s : sections_)
            for (auto& row : s->rows) {
                if (!row->isVisible() || row->isAdded() || s->foldTo < 0.5f || row->isNew() != wantNew)
                    continue;
                const int score = searchScore(row->matchText(), query_);
                if (best == nullptr || score < bestScore) {
                    best = row.get();
                    bestScore = score;
                }
            }
        if (best != nullptr)
            break;
    }
    if (best != nullptr)
        best->pick();
}

std::vector<juce::Component*> ModDotAddSourcePage::focusStops() const {
    std::vector<juce::Component*> stops;
    for (const auto& s : sections_) {
        if (!s->shown)
            continue;
        stops.push_back(&s->header);
        if (s->foldTo < 0.5f)
            continue;
        for (const auto& row : s->rows)
            if (row->isVisible())
                stops.push_back(row.get());
    }
    return stops;
}

void ModDotAddSourcePage::moveStop(juce::Component* from, int step) {
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

bool ModDotAddSourcePage::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey)) {
        moveStop(juce::Component::getCurrentlyFocusedComponent(), key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1);
        return true;
    }
    return false;
}

std::vector<juce::String> ModDotAddSourcePage::visibleGroupNames() const {
    std::vector<juce::String> names;
    for (const auto& s : sections_)
        if (s->shown)
            names.push_back(modSourceGroupName(s->group));
    return names;
}

std::vector<juce::String> ModDotAddSourcePage::visibleRowLabels() const {
    std::vector<juce::String> labels;
    for (const auto& s : sections_)
        if (s->shown && s->foldTo > 0.5f)
            for (const auto& row : s->rows)
                if (row->isVisible())
                    labels.push_back(row->item().label());
    return labels;
}

ModDotChoiceRow* ModDotAddSourcePage::visibleRow(int index) const {
    int n = 0;
    for (const auto& s : sections_)
        if (s->shown && s->foldTo > 0.5f)
            for (const auto& row : s->rows)
                if (row->isVisible() && n++ == index)
                    return row.get();
    return nullptr;
}

ModDotGroupHeader* ModDotAddSourcePage::header(const juce::String& groupName) const {
    for (const auto& s : sections_)
        if (s->shown && modSourceGroupName(s->group) == groupName)
            return &s->header;
    return nullptr;
}

bool ModDotAddSourcePage::isGroupExpanded(const juce::String& groupName) const {
    for (const auto& s : sections_)
        if (modSourceGroupName(s->group) == groupName)
            return s->foldTo > 0.5f;
    return false;
}

} // namespace synth::ui
