// SoundBrowserNavigation.cpp -- what the list holds (Places, a folder, favourites, search results), moving between
// those lists, and the slide or fade that carries the eye from the old list to the new one.
#include "SoundBrowser/SoundSearch.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

using synth::soundbrowser::SoundEntry;

juce::String SoundBrowserComponent::placeNameOf(const juce::String& placeId) const {
    if (placeId.isEmpty())
        return "Places";
    if (placeId == kFavouritesId)
        return "Favourites";
    if (placeId == kRecentId)
        return "Used recently";
    if (placeId == kShapesId)
        return "Shapes";
    for (const auto& place : index_.snapshot()->places)
        if (place.id == placeId)
            return place.name;
    return placeId;
}

juce::String SoundBrowserComponent::selectionKey(int index) const {
    if (index < 0 || index >= (int)rows_.size())
        return {};
    const auto& row = rows_[(size_t)index];
    switch (row.kind) {
    case RowKind::Sound:
        return "s:" + row.entry->path;
    case RowKind::Folder:
        return "f:" + row.folder;
    case RowKind::Place:
        return "p:" + row.placeId;
    case RowKind::GetPack:
        return "g:" + row.packId;
    case RowKind::AddFolder:
        return "a:add";
    case RowKind::Shape:
        return "h:" + juce::String(synth::shapes::kindWord(row.shape->kind)) + (row.shape->factory ? ":f:" : ":u:") +
               row.shape->name;
    case RowKind::Hint:
        break;
    }
    return {};
}

void SoundBrowserComponent::restoreSelection(const juce::String& key) {
    selected_ = -1;
    if (key.isEmpty())
        return;
    for (int i = 0; i < (int)rows_.size(); ++i)
        if (selectionKey(i) == key) {
            selected_ = i;
            return;
        }
}

void SoundBrowserComponent::appendSoundRows(const std::vector<SoundEntry>& entries) {
    for (const auto& e : entries) {
        Row row;
        row.kind = RowKind::Sound;
        row.text = e.name;
        row.detail = soundDetailText(e);
        row.entry = e;
        rows_.push_back(std::move(row));
    }
}

void SoundBrowserComponent::rebuildPlaceRows(const synth::soundbrowser::SoundSnapshot& snap) {
    const auto place = [this](const synth::soundbrowser::SoundPlace& p, int count, bool scanning) {
        Row row;
        row.kind = RowKind::Place;
        row.text = p.name;
        row.placeId = p.id;
        row.soundCount = count;
        row.userFolder = !p.isPack;
        row.missing = row.userFolder && p.missing;
        row.scanning = row.userFolder && !row.missing && scanning;
        row.detail = row.missing ? "Not found" : (row.scanning ? "Scanning" : juce::String(count));
        rows_.push_back(std::move(row));
    };
    const auto listRow = [this](const char* id, const char* name, int count) {
        Row row;
        row.kind = RowKind::Place;
        row.text = name;
        row.placeId = id;
        row.soundCount = count;
        row.detail = juce::String(count);
        rows_.push_back(std::move(row));
    };
    listRow(kFavouritesId, "Favourites", lists_.favourites().size());
    listRow(kRecentId, "Used recently", lists_.recent().size());
    if (shapeLibrary_ != nullptr)
        listRow(kShapesId, "Shapes", shapeCount());
    for (const auto& p : snap.places)
        if (p.isPack)
            place(p, p.soundCount, false);
    appendOfferedPackRows(snap, true);
    bool headed = false;
    for (const auto& p : snap.places) {
        if (p.isPack || (folderBook_ != nullptr && folderBook_->find(p.root) == nullptr))
            continue;
        if (!headed) {
            rows_.push_back({RowKind::Hint, "Your folders", {}, {}, {}, 0, std::nullopt});
            headed = true;
        }
        place(p, p.soundCount, snap.scanning && !p.scanned);
    }
    if (folderBook_ != nullptr) {
        if (!headed)
            rows_.push_back({RowKind::Hint, "Your folders", {}, {}, {}, 0, std::nullopt});
        Row add;
        add.kind = RowKind::AddFolder;
        add.text = "Add folder...";
        rows_.push_back(std::move(add));
    }
    if (snap.scanning && snap.places.empty())
        rows_.push_back({RowKind::Hint, "Looking for sounds...", {}, {}, {}, 0, std::nullopt});
}

// The folders and sounds inside a pack or a user folder; a folder that is not there says so instead of "Nothing here".
void SoundBrowserComponent::rebuildPlaceListing(const synth::soundbrowser::SoundSnapshot& snap,
                                                const synth::soundbrowser::SoundFilter& filter) {
    const auto listing = index_.childrenOf(placeId_, folder_, filter);
    for (const auto& f : listing.folders) {
        Row row;
        row.kind = RowKind::Folder;
        row.text = f.name;
        row.placeId = placeId_;
        row.folder = folder_.isEmpty() ? f.name : folder_ + "/" + f.name;
        row.soundCount = f.soundCount;
        row.detail = juce::String(f.soundCount);
        rows_.push_back(std::move(row));
    }
    appendSoundRows(listing.entries);
    if (!rows_.empty())
        return;
    bool missing = false;
    for (const auto& p : snap.places)
        missing = missing || (p.id == placeId_ && p.missing);
    const auto text = missing ? "This folder was not found. It comes back when its drive is plugged in."
                              : (snap.scanning ? "Looking for sounds..."
                                               : (filter.isActive() ? "No sounds match the filters" : "Nothing here"));
    rows_.push_back({RowKind::Hint, text, {}, {}, {}, 0, std::nullopt});
}

void SoundBrowserComponent::rebuildRows() {
    const auto key = selectionKey(selected_);
    const auto snap = index_.snapshot();
    const auto filter = currentFilter();
    rows_.clear();
    if (showsFlatResults()) {
        appendSoundRows(index_.search(query_, filter));
        if (isSearchActive() && !filter.isActive())
            appendOfferedPackRows(*snap, false);
        if (rows_.empty())
            rows_.push_back({RowKind::Hint, "No matching sounds", {}, {}, {}, 0, std::nullopt});
    } else if (placeId_.isEmpty()) {
        rebuildPlaceRows(*snap);
    } else if (placeId_ == kShapesId) {
        rebuildShapeRows();
    } else if (placeId_ == kFavouritesId || placeId_ == kRecentId) {
        appendListedRows(placeId_ == kFavouritesId ? lists_.favourites() : lists_.recent(), filter);
        if (rows_.empty())
            rows_.push_back({RowKind::Hint,
                             filter.isActive() ? "No sounds match the filters"
                                               : (placeId_ == kFavouritesId
                                                      ? "No favourites yet. Select a sound or shape and press F."
                                                      : "Nothing used yet."),
                             {},
                             {},
                             {},
                             0,
                             std::nullopt});
    } else {
        rebuildPlaceListing(*snap, filter);
    }
    restoreSelection(key);
    if (pendingSelection_.isNotEmpty()) {
        const int before = selected_;
        restoreSelection(pendingSelection_);
        if (selected_ >= 0) {
            pendingSelection_ = {};
            scrollSelectionIntoView();
        } else {
            selected_ = before;
        }
    }
    layoutPathParts();
    updateScrollBar();
    refreshAccessibilityValue();
    repaint();
}

void SoundBrowserComponent::setQuery(const juce::String& text) {
    if (search_.getText() != text)
        search_.setText(text, juce::dontSendNotification);
    if (query_ == text)
        return;
    capturePreviousList();
    query_ = text;
    scrollOffset_ = 0;
    selected_ = -1;
    rebuildRows();
    playListMotion(0);
}

std::vector<SoundBrowserComponent::PathPart> SoundBrowserComponent::getPathParts() const {
    std::vector<PathPart> parts{{"Places", {}, {}, {}}};
    if (placeId_.isEmpty())
        return parts;
    parts.push_back({placeNameOf(placeId_), placeId_, {}, {}});
    juce::String cumulative;
    for (const auto& segment : juce::StringArray::fromTokens(folder_, "/", "")) {
        cumulative = cumulative.isEmpty() ? segment : cumulative + "/" + segment;
        parts.push_back({segment, placeId_, cumulative, {}});
    }
    return parts;
}

juce::String SoundBrowserComponent::getPathText() const {
    juce::StringArray labels;
    for (const auto& p : getPathParts())
        labels.add(p.label);
    return labels.joinIntoString(" > ");
}

void SoundBrowserComponent::showLocation(const juce::String& placeId, const juce::String& folder, int direction) {
    navigateTo(placeId, folder, direction, {});
}

void SoundBrowserComponent::navigateTo(const juce::String& placeId, const juce::String& folder, int direction,
                                       const juce::String& selectKey) {
    capturePreviousList();
    if (query_.isNotEmpty()) {
        query_ = {};
        search_.setText({}, juce::dontSendNotification);
    }
    placeId_ = placeId;
    folder_ = folder;
    scrollOffset_ = 0;
    selected_ = -1;
    rebuildRows();
    restoreSelection(selectKey);
    if (selected_ < 0)
        for (int i = 0; i < (int)rows_.size(); ++i)
            if (rows_[(size_t)i].navigable()) {
                selected_ = i;
                break;
            }
    scrollSelectionIntoView();
    refreshAccessibilityValue();
    playListMotion(direction);
}

bool SoundBrowserComponent::openSelected() {
    if (selected_ < 0 || selected_ >= (int)rows_.size())
        return false;
    const auto row = rows_[(size_t)selected_];
    if (row.kind == RowKind::Place) {
        navigateTo(row.placeId, {}, +1, {});
        return true;
    }
    if (row.kind == RowKind::Folder) {
        navigateTo(row.placeId, row.folder, +1, {});
        return true;
    }
    return false;
}

bool SoundBrowserComponent::goUp() {
    if (isSearchActive() || placeId_.isEmpty())
        return false;
    // The row we came from stays selected, so Left then Right returns to the same spot.
    if (folder_.isEmpty()) {
        navigateTo({}, {}, -1, "p:" + placeId_);
        return true;
    }
    const int cut = folder_.lastIndexOfChar('/');
    navigateTo(placeId_, cut < 0 ? juce::String() : folder_.substring(0, cut), -1, "f:" + folder_);
    return true;
}

// Pictures the list as it is now (a motion in flight is landed first), so the change that follows can slide or fade
// away from it.
void SoundBrowserComponent::capturePreviousList() {
    if (vblank_.has_value())
        listAnim_.stop(*vblank_);
    listMotion_ = 1.0f;
    previousList_ = {};
    const auto area = listArea();
    if (!isShowing() || area.isEmpty())
        return;
    previousList_ = juce::Image(juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
    juce::Graphics ig(previousList_);
    ig.fillAll(synth::theme::themeOf(*this).colors.bg0);
    paintRows(ig, contentWidth());
}

// Carries the picture away: a slide (new list in from the right going into a folder, from the left going up) or, for
// a search and under Reduce Motion, a fade.
void SoundBrowserComponent::playListMotion(int direction) {
    if (previousList_.isNull()) {
        listMotion_ = 1.0f;
        return;
    }
    motionDirection_ = direction;
    if (!vblank_.has_value())
        vblank_.emplace(this);
    listMotion_ = 0.0f;
    const double ms = direction == 0 ? motionMs(120.0, 80.0) : motionMs(190.0, 80.0);
    listAnim_.start(
        *vblank_, ms, easeOutCubic,
        [this](float t) {
            listMotion_ = t;
            repaint(listArea());
        },
        [this] {
            listMotion_ = 1.0f;
            previousList_ = {};
            repaint(listArea());
        });
}

} // namespace synth::ui
