// Concern: the colour picker of one track -- live preview, cancel and the one-undo-step commit.
#include "TrackColourPicker.h"

namespace synth::ui {

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
