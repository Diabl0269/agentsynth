// TimelineSectionStripMenu.cpp
//
// The Sections strip's menus (the block's and the empty strip's) and every edit they apply. The menus are built here;
// the edits go through applySectionMenuChoice / applyStripMenuChoice, the headless seams the tests drive, so a real
// PopupMenu is never needed to exercise one. One choice is one undo step.

#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

#include "UI/Layout/CardCallOut.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Timeline/SectionColours.h"
#include <algorithm>

namespace synth::ui {

namespace {
juce::String keyText(const juce::KeyPress& key) { return key.getTextDescriptionWithIcons(); }
const juce::KeyPress kRenameKey(juce::KeyPress::F2Key, juce::ModifierKeys::noModifiers, 0);
const juce::KeyPress kPlaceAgainKey('d', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kDeleteKey(juce::KeyPress::deleteKey, juce::ModifierKeys::noModifiers, 0);
} // namespace

//==============================================================================
// ---- Edits ----

synth::PlacementId TimelineSectionStrip::makeSectionFromRange(double startBeat, double endBeat) {
    if (doc_ == nullptr)
        return {};
    const int index = (int)doc_->getSections().size();
    const auto name = "Section " + juce::String(index + 1);
    const auto colour = sectionColourFor(index);
    const double spb = secondsPerBeat();
    const auto before = doc_->getPlacements().size();
    performEdit(
        [this, startBeat, endBeat, name, colour, spb] { doc_->makeSection(startBeat, endBeat, name, colour, spb); });
    if (doc_->getPlacements().size() == before)
        return {}; // the model refused (an overlap, a cap): nothing changed
    // The new section's one placement starts at startBeat (placements are never overlapped, so it is the only one
    // there).
    const auto* placement = doc_->placementAt(startBeat);
    if (placement == nullptr)
        return {};
    const auto id = placement->id;
    refresh();
    selectPlacement(id);
    beginRenameSection(id);
    return id;
}

bool TimelineSectionStrip::placeAgain(synth::PlacementId id, bool atEnd) {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    const auto* section = placement != nullptr ? doc_->getSection(placement->section) : nullptr;
    if (section == nullptr)
        return false;
    const auto sectionId = section->id;
    double at = placement->startBeat + section->lengthBeats;
    if (atEnd) {
        double end = doc_->getArrangementEndBeat();
        for (const auto& other : doc_->getPlacements())
            if (const auto* s = doc_->getSection(other.section))
                end = std::max(end, other.startBeat + s->lengthBeats);
        at = sectionstrip::ceilToBar(end, beatsPerBar());
    }
    const auto mode = dropMode();
    const double spb = secondsPerBeat();
    synth::PlacementId placed;
    performEdit([this, sectionId, at, mode, spb, &placed] { placed = doc_->placeSection(sectionId, at, mode, spb); });
    refresh();
    if (!placed.isValid())
        return false;
    selectPlacement(placed);
    return true;
}

// The placement moves to a copy of its section with a name of its own ("Chorus 2") and the next palette colour, and
// its name editor opens: the point of making one unique is to call it something else.
bool TimelineSectionStrip::makeUnique(synth::PlacementId id) {
    if (!isSectionMenuChoiceEnabled(id, SectionMenuChoice::MakeUnique))
        return false;
    const auto* placement = doc_->getPlacement(id);
    const auto* section = doc_->getSection(placement->section);
    juce::StringArray taken;
    for (const auto& other : doc_->getSections())
        taken.add(other.name);
    const auto name = sectionstrip::uniqueSectionName(section->name, taken);
    const auto colour = sectionColourFor((int)doc_->getSections().size());
    synth::SectionId made;
    performEdit([this, id, name, colour, &made] { made = doc_->makePlacementUnique(id, name, colour); });
    refresh();
    if (!made.isValid())
        return false;
    selectPlacement(id);
    beginRenameSection(id);
    return true;
}

bool TimelineSectionStrip::selectClipsOf(synth::PlacementId id) {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    const auto* section = placement != nullptr ? doc_->getSection(placement->section) : nullptr;
    if (section == nullptr || !onSelectClips)
        return false;
    const double start = placement->startBeat;
    const double end = start + section->lengthBeats;
    std::vector<synth::ClipId> ids;
    for (const auto& track : doc_->getTracks()) {
        if (track.kind == synth::TrackKind::Automation)
            continue;
        for (const auto& clip : track.clips)
            if (clip.startBeat >= start - sectionstrip::kBarEpsilon && clip.startBeat < end - sectionstrip::kBarEpsilon)
                ids.push_back(clip.id);
    }
    onSelectClips(ids);
    return !ids.empty();
}

bool TimelineSectionStrip::isSectionMenuChoiceEnabled(synth::PlacementId id, SectionMenuChoice choice) const {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    if (placement == nullptr)
        return false;
    if (choice == SectionMenuChoice::MakeUnique)
        return doc_->placementsOf(placement->section).size() >
               1; // a section that plays once has nothing to separate from
    return true;
}

bool TimelineSectionStrip::applySectionMenuChoice(synth::PlacementId id, SectionMenuChoice choice) {
    if (doc_ == nullptr || doc_->getPlacement(id) == nullptr)
        return false;
    const double spb = secondsPerBeat();
    bool done = false;
    switch (choice) {
    case SectionMenuChoice::Rename:
        beginRenameSection(id);
        return renameEditor_ != nullptr;
    case SectionMenuChoice::ChangeColour: {
        auto popup = buildColourPicker(id);
        if (popup == nullptr)
            return false;
        auto anchor = getScreenBounds();
        for (const auto& block : buildBlocks())
            if (block.id == id)
                anchor = localAreaToGlobal(block.bounds.toNearestInt());
        synth::ui::launchCardCallOut(std::move(popup), anchor, this);
        return true;
    }
    case SectionMenuChoice::PlaceAgainAfter:
        return placeAgain(id, false);
    case SectionMenuChoice::PlaceAgainAtEnd:
        return placeAgain(id, true);
    case SectionMenuChoice::MakeUnique:
        return makeUnique(id);
    case SectionMenuChoice::SelectClips:
        return selectClipsOf(id);
    case SectionMenuChoice::RemoveKeepMusic:
        performEdit([this, id, &done] { done = doc_->removePlacementKeepMusic(id); });
        break;
    case SectionMenuChoice::DeleteWithBars:
        performEdit([this, id, spb, &done] { done = doc_->deletePlacementAndBars(id, spb); });
        break;
    }
    refresh();
    return done;
}

bool TimelineSectionStrip::applyStripMenuChoice(StripMenuChoice choice) {
    if (doc_ == nullptr)
        return false;
    int made = 0;
    switch (choice) {
    case StripMenuChoice::MakeFromMarkers: {
        if (doc_->getMarkers().empty())
            return false;
        const double spb = secondsPerBeat();
        const double end = doc_->getArrangementEndBeat();
        performEdit([this, end, spb, &made] {
            made = doc_->makeSectionsFromMarkers(end, [](int index) { return sectionColourFor(index); }, spb);
        });
        break;
    }
    }
    refresh();
    return made > 0;
}

//==============================================================================
// ---- Colour ----

std::unique_ptr<synth::ui::ColourPickerPopup> TimelineSectionStrip::buildColourPicker(synth::PlacementId id) {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    const auto* section = placement != nullptr ? doc_->getSection(placement->section) : nullptr;
    if (section == nullptr)
        return nullptr;
    // What a no-net-change close restores, and what the one recorded step restores TO: the shape the marker picker
    // uses.
    const auto sectionId = section->id;
    const juce::uint32 original = section->colourArgb;
    juce::Component::SafePointer<TimelineSectionStrip> safeThis(this);
    return std::make_unique<synth::ui::ColourPickerPopup>(
        juce::Colour(original), propertiesFile_,
        [safeThis, sectionId](juce::Colour c) {
            // Live preview: writes the doc directly, no undo, so every drag repaints the blocks.
            if (auto* self = safeThis.getComponent(); self != nullptr && self->doc_ != nullptr)
                self->doc_->setSectionColour(sectionId, c.getARGB());
        },
        [safeThis, sectionId, original](juce::Colour finalColour) {
            auto* self = safeThis.getComponent();
            if (self == nullptr || self->doc_ == nullptr)
                return;
            // Put the original back outside the recorded step so its undo restores it, then record the real edit.
            self->doc_->setSectionColour(sectionId, original);
            if (finalColour.getARGB() == original)
                return;
            self->performEdit(
                [self, sectionId, finalColour] { self->doc_->setSectionColour(sectionId, finalColour.getARGB()); });
            self->refresh();
        });
}

//==============================================================================
// ---- Menus ----

void TimelineSectionStrip::openSectionContextMenu(synth::PlacementId id) { buildAndShowBlockMenu(id); }

void TimelineSectionStrip::buildAndShowBlockMenu(synth::PlacementId id) {
    if (doc_ == nullptr || doc_->getPlacement(id) == nullptr)
        return;
    juce::Component::SafePointer<TimelineSectionStrip> safeThis(this);
    juce::PopupMenu menu;
    const auto add = [&](const juce::String& text, SectionMenuChoice choice, const juce::KeyPress* key = nullptr) {
        juce::PopupMenu::Item item(text);
        item.setEnabled(isSectionMenuChoiceEnabled(id, choice));
        if (key != nullptr)
            item.shortcutKeyDescription = keyText(*key);
        item.action = [safeThis, id, choice] {
            if (auto* self = safeThis.getComponent())
                self->applySectionMenuChoice(id, choice);
        };
        menu.addItem(item);
    };
    add("Rename...", SectionMenuChoice::Rename, &kRenameKey);
    add("Change colour...", SectionMenuChoice::ChangeColour);
    menu.addSeparator();
    add("Place again after this", SectionMenuChoice::PlaceAgainAfter, &kPlaceAgainKey);
    add("Place again at the end", SectionMenuChoice::PlaceAgainAtEnd);
    menu.addSeparator();
    add("Make unique", SectionMenuChoice::MakeUnique);
    add("Select its clips", SectionMenuChoice::SelectClips);
    menu.addSeparator();
    add("Remove section, keep music", SectionMenuChoice::RemoveKeepMusic);
    add("Delete section and its bars", SectionMenuChoice::DeleteWithBars, &kDeleteKey);
    // Plain Options at the pointer, exactly as the ruler's marker menu: anchoring to this full-width strip would put
    // the menu somewhere unrelated to the block that was clicked.
    menu.showMenuAsync(synth::ui::contextMenuOptionsAtPointer());
}

void TimelineSectionStrip::openStripContextMenu() {
    if (doc_ == nullptr)
        return;
    juce::Component::SafePointer<TimelineSectionStrip> safeThis(this);
    juce::PopupMenu menu;
    juce::PopupMenu::Item item("Make sections from markers");
    item.setEnabled(!doc_->getMarkers().empty());
    item.action = [safeThis] {
        if (auto* self = safeThis.getComponent())
            self->applyStripMenuChoice(StripMenuChoice::MakeFromMarkers);
    };
    menu.addItem(item);
    menu.showMenuAsync(synth::ui::contextMenuOptionsAtPointer());
}

} // namespace synth::ui
