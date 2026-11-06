// TimelineSectionStripVariations.cpp
//
// The Sections strip's variation menu: the letter chip's click menu and the block menu's "Variation" submenu are the
// same item list, built by addVariationItems. Every item applies through applyVariationMenuChoice, the headless seam
// the tests drive; one choice is one undo step and outlines the placements it rewrote.

#include "Timeline/TimelineDoc/SectionVariationLetter.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr std::size_t kMaxNamedTracks = 3; // a row names this many own tracks, then "+N"

std::size_t countMusicalTracks(const synth::TimelineDoc& doc) {
    return (std::size_t)std::count_if(doc.getTracks().begin(), doc.getTracks().end(),
                                      [](const synth::Track& t) { return t.kind != synth::TrackKind::Automation; });
}
} // namespace

juce::String TimelineSectionStrip::variationRowLabel(synth::PlacementId id, int letter) const {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    const auto* variation = placement != nullptr ? doc_->getVariation(placement->section, letter) : nullptr;
    if (variation == nullptr)
        return {};
    const auto label = synth::variationLetter(letter);
    if (letter == 0 || variation->ownTracks.size() >= countMusicalTracks(*doc_))
        return label;
    juce::StringArray names;
    std::size_t listed = 0;
    for (const auto trackValue : variation->ownTracks) {
        const auto* track = doc_->getTrack(synth::TrackId{trackValue});
        if (track == nullptr)
            continue;
        if (listed++ < kMaxNamedTracks)
            names.add(track->name);
    }
    if (listed > kMaxNamedTracks)
        names.add("+" + juce::String((int)(listed - kMaxNamedTracks)));
    return names.isEmpty() ? label : label + " (" + names.joinIntoString(", ") + ")";
}

juce::String TimelineSectionStrip::variationChoiceLabel(synth::PlacementId id, VariationChoice choice) const {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    const auto* section = placement != nullptr ? doc_->getSection(placement->section) : nullptr;
    if (section == nullptr)
        return {};
    const auto letter = synth::variationLetter(placement->variation);
    switch (choice) {
    case VariationChoice::NewVariation:
        return "New variation (copy of " + letter + ")";
    case VariationChoice::PlayEverywhere:
        return "Play " + letter + " in every " + section->name;
    case VariationChoice::RemoveVariation:
        return "Remove variation " + letter;
    case VariationChoice::Switch:
        break;
    }
    return {};
}

bool TimelineSectionStrip::isVariationChoiceEnabled(synth::PlacementId id, VariationChoice choice) const {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    const auto* section = placement != nullptr ? doc_->getSection(placement->section) : nullptr;
    if (section == nullptr)
        return false;
    switch (choice) {
    case VariationChoice::Switch:
        return true;
    case VariationChoice::NewVariation:
        return (int)section->variations.size() < synth::TimelineDoc::kMaxVariations;
    case VariationChoice::PlayEverywhere: {
        const auto others = doc_->placementsOf(section->id);
        return others.size() > 1 && std::any_of(others.begin(), others.end(), [&](synth::PlacementId other) {
                   return doc_->getPlacement(other)->variation != placement->variation;
               });
    }
    case VariationChoice::RemoveVariation:
        return placement->variation != 0;
    }
    return false;
}

bool TimelineSectionStrip::applyVariationMenuChoice(synth::PlacementId id, VariationChoice choice, int letter) {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    if (placement == nullptr || !isVariationChoiceEnabled(id, choice))
        return false;
    const auto sectionId = placement->section;
    const int current = placement->variation;
    // The placements this choice rewrites, taken before it runs: those that move to another letter.
    std::vector<synth::PlacementId> affected;
    for (const auto other : doc_->placementsOf(sectionId)) {
        const int played = doc_->getPlacement(other)->variation;
        if ((choice == VariationChoice::PlayEverywhere && played != current) ||
            (choice == VariationChoice::RemoveVariation && played == current))
            affected.push_back(other);
    }
    if (choice == VariationChoice::Switch || choice == VariationChoice::NewVariation)
        affected = {id};

    bool done = false;
    switch (choice) {
    case VariationChoice::Switch:
        performEdit([this, id, letter, &done] { done = doc_->setPlacementVariation(id, letter); });
        break;
    case VariationChoice::NewVariation:
        performEdit([this, id, &done] { done = doc_->addVariation(id) >= 0; });
        break;
    case VariationChoice::PlayEverywhere:
        performEdit([this, sectionId, current, &done] { done = doc_->playVariationEverywhere(sectionId, current); });
        break;
    case VariationChoice::RemoveVariation:
        performEdit([this, sectionId, current, &done] { done = doc_->removeVariation(sectionId, current); });
        break;
    }
    refresh();
    if (done)
        outlinePlacements(affected);
    return done;
}

void TimelineSectionStrip::outlinePlacements(const std::vector<synth::PlacementId>& placements) {
    motion_.startOutline(placements);
}

//==============================================================================
void TimelineSectionStrip::addVariationItems(juce::PopupMenu& menu, synth::PlacementId id) {
    const auto* placement = doc_ != nullptr ? doc_->getPlacement(id) : nullptr;
    const auto* section = placement != nullptr ? doc_->getSection(placement->section) : nullptr;
    if (section == nullptr)
        return;
    juce::Component::SafePointer<TimelineSectionStrip> safeThis(this);
    for (const auto& variation : section->variations) {
        const int letter = variation.letter;
        juce::PopupMenu::Item row(variationRowLabel(id, letter));
        row.setTicked(letter == placement->variation);
        row.action = [safeThis, id, letter] {
            if (auto* self = safeThis.getComponent())
                self->applyVariationMenuChoice(id, VariationChoice::Switch, letter);
        };
        menu.addItem(row);
    }
    menu.addSeparator();
    const auto add = [&](VariationChoice choice) {
        juce::PopupMenu::Item item(variationChoiceLabel(id, choice));
        item.setEnabled(isVariationChoiceEnabled(id, choice));
        item.action = [safeThis, id, choice] {
            if (auto* self = safeThis.getComponent())
                self->applyVariationMenuChoice(id, choice);
        };
        menu.addItem(item);
    };
    add(VariationChoice::NewVariation);
    add(VariationChoice::PlayEverywhere);
    if (placement->variation != 0)
        add(VariationChoice::RemoveVariation);
}

// Anchored at the chip (else the block): the menu belongs to the letter that was clicked.
void TimelineSectionStrip::openVariationMenu(synth::PlacementId id) {
    if (doc_ == nullptr || doc_->getPlacement(id) == nullptr)
        return;
    juce::PopupMenu menu;
    addVariationItems(menu, id);
    menu.showMenuAsync(variationMenuOptions(id));
}

juce::PopupMenu::Options TimelineSectionStrip::variationMenuOptions(synth::PlacementId id) const {
    for (const auto& block : buildBlocks()) {
        if (block.id != id)
            continue;
        const auto chip = sectionstrip::chipBounds(block.bounds);
        const auto area = chip.isEmpty() ? block.bounds : chip;
        return synth::ui::contextMenuOptions(localAreaToGlobal(area.toNearestInt()));
    }
    return synth::ui::contextMenuOptionsAtPointer();
}

} // namespace synth::ui
