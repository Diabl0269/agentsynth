// Concern: the colour picker of one track -- live preview, cancel and the one-undo-step commit.
#include "TrackColourPicker.h"
#include <optional>

namespace synth::ui {

namespace {

// The same preview / cancel / one-undo-step shape for every selected track: the preview writes each track's colour
// directly, closing with the clicked track's colour back restores them all, and a final pick restores the originals
// first so the ONE recorded edit (all the writes together) is what undo reverts.
std::unique_ptr<ColourPickerPopup> buildMultiTrackColourPicker(const TrackColourPickerContext& context,
                                                               juce::uint32 clickedOriginal) {
    struct Original {
        synth::TrackId id;
        juce::uint32 track;
        std::optional<juce::Colour> macro; // the channel macro this track owns, which follows its colour
    };
    std::vector<Original> originals;
    for (const auto id : context.targets)
        if (const auto* t = context.doc->getTrack(id))
            originals.push_back(
                {id, t->colourArgb, context.link != nullptr ? context.link->ownedMacroColour(id) : std::nullopt});
    auto* doc = context.doc;
    auto* link = context.link;
    const auto isAlive = context.isAlive;
    const auto performEdit = context.performEdit;
    const auto write = [doc, link](const Original& o, juce::Colour c, bool macroToo) {
        doc->setTrackColour(o.id, c.getARGB());
        if (macroToo && link != nullptr && o.macro.has_value())
            link->setOwnedMacroColour(o.id, c);
    };
    return std::make_unique<ColourPickerPopup>(
        juce::Colour(clickedOriginal), context.favourites,
        [originals, isAlive, write](juce::Colour c) {
            if (isAlive && !isAlive())
                return;
            for (const auto& o : originals)
                write(o, c, true);
        },
        [doc, link, originals, isAlive, performEdit, clickedOriginal, write](juce::Colour finalColour) {
            if (isAlive && !isAlive())
                return;
            for (const auto& o : originals) { // silently back to the originals, so undo restores exactly these
                doc->setTrackColour(o.id, o.track);
                if (link != nullptr && o.macro.has_value())
                    link->setOwnedMacroColour(o.id, *o.macro);
            }
            if (finalColour.getARGB() == clickedOriginal)
                return; // no net change
            const auto edit = [originals, finalColour, write] {
                for (const auto& o : originals)
                    write(o, finalColour, true);
            };
            if (link != nullptr && link->recordColourEdit(edit))
                return; // the surface recorded it as one graph + timeline + macro step
            if (performEdit)
                performEdit(edit);
            else
                edit();
        });
}

} // namespace

// The single-target body: previews write the doc directly (no undo, every drag repaints the row at once); the commit
// puts the original back outside the recorded mutation so the ONE recorded step's undo restores the original colour.
std::unique_ptr<ColourPickerPopup> buildTrackColourPicker(const TrackColourPickerContext& context,
                                                          synth::TrackId track) {
    if (context.doc == nullptr)
        return nullptr;
    const auto* t = context.doc->getTrack(track);
    if (t == nullptr)
        return nullptr;
    // The colour a no-net-change close restores, and what a "keep the final pick" undo step restores TO.
    const juce::uint32 originalColour = t->colourArgb;

    if (context.targets.size() > 1)
        return buildMultiTrackColourPicker(context, originalColour);

    // docs/mixer/mixer.md#a-track-and-its-macro-share-a-colour: the picker of a track that owns its channel macro fans
    // every preview write out to that macro as well, and commits both as ONE undo step. Null for anything else.
    if (context.link != nullptr)
        if (auto popup = context.link->buildOwnedMacroColourPicker(track, context.favourites))
            return popup;

    auto* doc = context.doc;
    const auto isAlive = context.isAlive;
    const auto performEdit = context.performEdit;
    return std::make_unique<ColourPickerPopup>(
        juce::Colour(originalColour), context.favourites,
        [doc, track, isAlive](juce::Colour c) {
            if (isAlive && !isAlive())
                return;
            doc->setTrackColour(track, c.getARGB());
        },
        [doc, track, isAlive, performEdit, originalColour](juce::Colour finalColour) {
            if (isAlive && !isAlive())
                return; // the surface (or its window) is gone -- nothing left to restore or undo
            if (finalColour.getARGB() == originalColour) {
                // No net change: put back exactly what was there (a preview may have nudged it) and record no undo
                // step.
                doc->setTrackColour(track, originalColour);
                return;
            }
            doc->setTrackColour(track, originalColour);
            const auto edit = [doc, track, finalColour] { doc->setTrackColour(track, finalColour.getARGB()); };
            if (performEdit)
                performEdit(edit);
            else
                edit();
        });
}

} // namespace synth::ui
