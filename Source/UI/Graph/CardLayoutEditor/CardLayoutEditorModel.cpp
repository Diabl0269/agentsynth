// CardLayoutEditorModel.cpp -- the editor's working layout: normalising what the card draws, the rows
// the list shows, and every edit. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutEditorModel.h"
#include <algorithm>

namespace synth::ui {

namespace {

const CardParamItem* asParam(const CardItem& item) { return std::get_if<CardParamItem>(&item); }

juce::String keyOf(const CardItem& item) {
    if (const auto* p = asParam(item))
        return p->paramId;
    return cardLayoutViewKey(std::get<CardViewItem>(item).view);
}

bool matches(const juce::String& name, const juce::String& search) {
    return search.isEmpty() || name.toLowerCase().contains(search);
}

} // namespace

juce::String cardLayoutViewKey(CardView view) { return "view:" + juce::String((int)view); }

juce::String cardLayoutViewName(CardView view) {
    switch (view) {
    case CardView::Scope:
        return "Scope display";
    case CardView::Response:
        return "Response display";
    case CardView::Spectrum:
        return "Spectrum display";
    case CardView::Envelope:
        return "Envelope display";
    case CardView::LfoShape:
        return "Shape display";
    case CardView::LfoCurve:
        return "Curve editor";
    case CardView::Waveform:
        return "Waveform display";
    case CardView::Wavetable:
        return "Wavetable display";
    case CardView::EqCurve:
        return "EQ curve";
    case CardView::Threshold:
        return "Threshold display";
    case CardView::GainReduction:
        return "Gain reduction meter";
    }
    return "Display";
}

juce::String cardLayoutWidgetName(CardWidget widget) {
    switch (widget) {
    case CardWidget::Auto:
        return "Automatic";
    case CardWidget::Knob:
        return "Knob";
    case CardWidget::KnobLarge:
        return "Large knob";
    case CardWidget::FaderV:
        return "Vertical fader";
    case CardWidget::FaderH:
        return "Horizontal fader";
    case CardWidget::Toggle:
        return "Toggle";
    case CardWidget::Choice:
        return "Menu";
    case CardWidget::Segmented:
        return "Segmented";
    case CardWidget::Stepper:
        return "Stepper";
    }
    return "Automatic";
}

CardLayoutEditorModel::CardLayoutEditorModel(std::vector<CardLayoutEditorParam> params, HiddenRows hiddenRows,
                                             bool groups)
    : params_(std::move(params))
    , hiddenRows_(hiddenRows)
    , groups_(groups) {}

// Duplicates and items the source does not know are set aside; when hidden rows stay in place, a
// parameter the layout never placed (one a later release added) is appended to the last group,
// hidden, which is exactly where the card already shows it: in the More row.
void CardLayoutEditorModel::load(const CardLayout& layout) {
    sections_.clear();
    hidden_.clear();
    missing_.clear();
    basedOn_ = layout.basedOn;
    juce::StringArray seen;
    for (const auto& source : layout.sections) {
        CardSection section = source;
        section.items.clear();
        for (const auto& item : source.items) {
            const auto key = keyOf(item);
            if (seen.contains(key))
                continue;
            if (const auto* p = asParam(item); p != nullptr && findParam(p->paramId) == nullptr) {
                missing_.push_back(*p);
                continue;
            }
            seen.add(key);
            section.items.push_back(item);
        }
        sections_.push_back(std::move(section));
    }
    if (sections_.empty()) {
        CardSection section;
        section.id = "main";
        sections_.push_back(std::move(section));
    }
    if (hiddenRows_ != HiddenRows::StayInPlace)
        return;
    for (const auto& id : layout.hidden)
        if (findParam(id) != nullptr)
            hidden_.addIfNotAlreadyThere(id);
    for (const auto& param : params_) {
        if (seen.contains(param.paramId))
            continue;
        CardParamItem item;
        item.paramId = param.paramId;
        // Poly joins a footer card's footer on its own, so a layout never lists it: keep it there
        // rather than hiding it into the More row the first time the layout is edited.
        if (auto footer = std::find_if(sections_.begin(), sections_.end(),
                                       [](const CardSection& s) { return s.id == CardSection::kFooterId; });
            param.paramId == "poly" && footer != sections_.end()) {
            footer->items.emplace_back(item);
            continue;
        }
        sections_.back().items.emplace_back(item);
        hidden_.addIfNotAlreadyThere(param.paramId);
    }
}

CardLayout CardLayoutEditorModel::toLayout() const {
    CardLayout layout;
    layout.version = CardLayout::kCurrentVersion;
    layout.basedOn = basedOn_;
    layout.sections = sections_;
    layout.hidden = hidden_;
    return layout;
}

const CardLayoutEditorParam* CardLayoutEditorModel::findParam(const juce::String& paramId) const {
    for (const auto& param : params_)
        if (param.paramId == paramId)
            return &param;
    return nullptr;
}

juce::String CardLayoutEditorModel::nameOf(const CardItem& item) const {
    if (const auto* p = asParam(item)) {
        const auto* param = findParam(p->paramId);
        return param != nullptr ? param->displayName : p->paramId;
    }
    return cardLayoutViewName(std::get<CardViewItem>(item).view);
}

CardLayoutEditorModel::Row CardLayoutEditorModel::rowFor(const CardItem& item, int section) const {
    Row row;
    row.section = section;
    row.key = keyOf(item);
    row.name = nameOf(item);
    if (const auto* p = asParam(item)) {
        row.kind = Row::Kind::Param;
        row.shown = !hidden_.contains(p->paramId);
        row.label = p->label;
        row.widget = p->widget;
        if (const auto* param = findParam(p->paramId))
            row.widgetChoices = param->widgetChoices;
    } else {
        row.kind = Row::Kind::View;
    }
    return row;
}

// Grouped: every section's header (kept while a search runs, so a drop can still land in any
// group), then its items that match.
void CardLayoutEditorModel::addRowsInPlace(std::vector<Row>& out, const juce::String& search) const {
    for (int s = 0; s < (int)sections_.size(); ++s) {
        if (groups_) {
            Row header;
            header.kind = Row::Kind::Header;
            header.section = s;
            header.key = headerKey(s);
            header.name = sections_[(size_t)s].title.value_or(juce::String());
            out.push_back(header);
        }
        for (const auto& item : sections_[(size_t)s].items)
            if (matches(nameOf(item), search))
                out.push_back(rowFor(item, s));
    }
}

// Ticked rows first, in the layout's order; then every other parameter in the source's order.
void CardLayoutEditorModel::addRowsTickedFirst(std::vector<Row>& out, const juce::String& search) const {
    for (int s = 0; s < (int)sections_.size(); ++s)
        for (const auto& item : sections_[(size_t)s].items)
            if (matches(nameOf(item), search))
                out.push_back(rowFor(item, s));
    for (const auto& param : params_) {
        if (find(param.paramId).valid() || !matches(param.displayName, search))
            continue;
        Row row;
        row.key = param.paramId;
        row.name = param.displayName;
        row.shown = false;
        row.placed = false;
        row.widgetChoices = param.widgetChoices;
        out.push_back(row);
    }
}

std::vector<CardLayoutEditorModel::Row> CardLayoutEditorModel::rows(const juce::String& search) const {
    std::vector<Row> out;
    const auto needle = search.trim().toLowerCase();
    if (hiddenRows_ == HiddenRows::StayInPlace)
        addRowsInPlace(out, needle);
    else
        addRowsTickedFirst(out, needle);
    return out;
}

juce::StringArray CardLayoutEditorModel::missingNames() const {
    juce::StringArray names;
    for (const auto& item : missing_)
        names.add(item.label.value_or(item.paramId));
    return names;
}

CardLayoutEditorModel::Position CardLayoutEditorModel::find(const juce::String& key) const {
    for (int s = 0; s < (int)sections_.size(); ++s) {
        const auto& items = sections_[(size_t)s].items;
        for (int i = 0; i < (int)items.size(); ++i)
            if (keyOf(items[(size_t)i]) == key)
                return {s, i};
    }
    return {};
}

CardItem CardLayoutEditorModel::take(Position at) {
    auto& items = sections_[(size_t)at.section].items;
    CardItem item = items[(size_t)at.index];
    items.erase(items.begin() + at.index);
    return item;
}

void CardLayoutEditorModel::insert(CardItem item, int section, int index) {
    section = juce::jlimit(0, (int)sections_.size() - 1, section);
    auto& items = sections_[(size_t)section].items;
    index = juce::jlimit(0, (int)items.size(), index);
    items.insert(items.begin() + index, std::move(item));
}

CardParamItem* CardLayoutEditorModel::findItem(const juce::String& paramId) {
    const auto at = find(paramId);
    if (!at.valid())
        return nullptr;
    return std::get_if<CardParamItem>(&sections_[(size_t)at.section].items[(size_t)at.index]);
}

bool CardLayoutEditorModel::isShown(const juce::String& paramId) const {
    if (hiddenRows_ == HiddenRows::StayInPlace)
        return find(paramId).valid() && !hidden_.contains(paramId);
    return find(paramId).valid();
}

void CardLayoutEditorModel::setShown(const juce::String& paramId, bool shown) {
    const auto* param = findParam(paramId);
    if (param == nullptr)
        return;
    if (hiddenRows_ == HiddenRows::StayInPlace) {
        if (shown)
            hidden_.removeString(paramId);
        else
            hidden_.addIfNotAlreadyThere(paramId);
        return;
    }
    const auto at = find(paramId);
    if (!shown) {
        if (at.valid())
            take(at);
        return;
    }
    if (at.valid())
        return;
    CardParamItem item;
    item.paramId = paramId;
    item.indexHint = param->index;
    insert(item, (int)sections_.size() - 1, (int)sections_.back().items.size());
}

void CardLayoutEditorModel::setLabel(const juce::String& paramId, const juce::String& text) {
    auto* item = findItem(paramId);
    const auto* param = findParam(paramId);
    if (item == nullptr || param == nullptr)
        return;
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || trimmed == param->displayName)
        item->label.reset();
    else
        item->label = trimmed;
}

void CardLayoutEditorModel::setWidget(const juce::String& paramId, CardWidget widget) {
    if (auto* item = findItem(paramId))
        item->widget = widget;
}

void CardLayoutEditorModel::setSectionTitle(int section, const juce::String& title) {
    if (section < 0 || section >= (int)sections_.size())
        return;
    const auto trimmed = title.trim();
    auto& target = sections_[(size_t)section];
    if (trimmed.isEmpty())
        target.title.reset();
    else
        target.title = trimmed;
}

// A new group gets an id no other section uses, so a stored layout never holds two of the same.
int CardLayoutEditorModel::addGroup() {
    int n = (int)sections_.size() + 1;
    const auto taken = [this](const juce::String& id) {
        return std::any_of(sections_.begin(), sections_.end(), [&id](const CardSection& s) { return s.id == id; });
    };
    while (taken("group-" + juce::String(n)))
        ++n;
    CardSection section;
    section.id = "group-" + juce::String(n);
    section.title = "New group";
    sections_.push_back(std::move(section));
    return (int)sections_.size() - 1;
}

bool CardLayoutEditorModel::moveBy(const juce::String& key, int delta) {
    const auto at = find(key);
    if (!at.valid() || delta == 0)
        return false;
    const int size = (int)sections_[(size_t)at.section].items.size();
    const int target = at.index + (delta < 0 ? -1 : 1);
    if (target >= 0 && target < size) {
        auto item = take(at);
        insert(std::move(item), at.section, target);
        return true;
    }
    const int neighbour = at.section + (delta < 0 ? -1 : 1);
    if (!groups_ || neighbour < 0 || neighbour >= (int)sections_.size())
        return false;
    auto item = take(at);
    insert(std::move(item), neighbour, delta < 0 ? (int)sections_[(size_t)neighbour].items.size() : 0);
    return true;
}

void CardLayoutEditorModel::moveToIndex(const juce::String& key, int index) {
    const auto at = find(key);
    if (!at.valid())
        return;
    auto item = take(at);
    insert(std::move(item), 0, index);
}

// The dropped item goes before the visible item that now follows it, else after the one that precedes
// it, else first in the group whose header precedes it. Looking neighbours up in the model (not by
// visible index) is what keeps rows a search hides in their places.
void CardLayoutEditorModel::dropAt(const juce::String& key, const std::vector<juce::String>& visibleOrder) {
    const auto from = find(key);
    const auto it = std::find(visibleOrder.begin(), visibleOrder.end(), key);
    if (!from.valid() || it == visibleOrder.end())
        return;
    const auto place = (size_t)(it - visibleOrder.begin());
    auto item = take(from);
    if (place + 1 < visibleOrder.size() && !isHeaderKey(visibleOrder[place + 1])) {
        if (const auto next = find(visibleOrder[place + 1]); next.valid())
            return insert(std::move(item), next.section, next.index);
    }
    if (place > 0) {
        const auto& previous = visibleOrder[place - 1];
        if (isHeaderKey(previous))
            return insert(std::move(item), previous.substring(1).getIntValue(), 0);
        if (const auto prev = find(previous); prev.valid())
            return insert(std::move(item), prev.section, prev.index + 1);
    }
    insert(std::move(item), 0, 0);
}

} // namespace synth::ui
