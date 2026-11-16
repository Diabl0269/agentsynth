// MacroColourGlide.h -- the colours a macro shows (its card and border, and each port's jack) ease between the old and
// the new on an undo or redo and nowhere else (docs/layout/undo-glide.md, "Colours glide too"). One instance per
// canvas, owned by MacroGroupController and bound to the GraphEditor, so the collapsed card, the expanded border and
// the docked port widgets all draw from the same tween and fade in step. Message thread only.
#pragma once

#include "MacroSet.h"
#include "UI/Layout/KeyGlide/RestoredColourGlide.h"
#include <functional>
#include <map>

namespace synth::ui {

class MacroColourGlide {
public:
    /** `owner` is the canvas editor (the tween's repaint target); `onDirty` repaints what draws the colours. */
    MacroColourGlide(juce::Component& owner, AppUndoManager* undo, std::function<void()> onDirty);

    /** Re-reads every macro's colour and each port's resolved jack colour (`midiTint` / `cvTint` for a port with no
     *  colour of its own). Called once per macro-card sync. */
    void sync(const synth::MacroSet& macros, juce::Colour midiTint, juce::Colour cvTint);

    /** What to paint for the macro's colour / the port's jack, which rest at `resting`. */
    juce::Colour macroColour(const juce::String& macroId, juce::Colour resting) const;
    juce::Colour portColour(const juce::String& macroId, const juce::String& portUuid, juce::Colour resting) const;

    RestoredColourGlide& glide() noexcept { return glide_; }
    const RestoredColourGlide& glide() const noexcept { return glide_; }

private:
    static RestoredColourGlide::Key keyFor(const juce::String& id) { return id.hashCode64(); }
    RestoredColourGlide glide_;
    std::map<juce::String, juce::Colour> seen_; // "macroId" and "macroId/portUuid" as drawn-from at the last sync
};

} // namespace synth::ui
