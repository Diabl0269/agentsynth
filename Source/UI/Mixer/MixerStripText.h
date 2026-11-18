#pragma once

// MixerStripText.h -- the colour a mixer strip's part paints its secondary text in. A selected strip is washed with
// the accent, and the theme's muted grey falls under the readable contrast on that wash in most themes; so the text is
// moved toward white or black until it reads on the plain strip and on the washed one alike (same colour in both, so
// it does not shift while the wash fades in). A part outside a strip (the Master column) keeps the colour it asked for.
#include "UI/Theme/ReadableText.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/** A column whose parts paint on fills of its own (MixerColumnComponent): the plain surface and its selection wash. */
class StripBackdrop {
public:
    virtual ~StripBackdrop() = default;
    virtual std::vector<juce::Colour> backdropFills() const = 0;
};

/** `text` readable on every fill of the strip `part` sits in; unchanged when `part` is in no strip. */
inline juce::Colour readableOnStrip(const juce::Component& part, juce::Colour text) {
    for (auto* p = part.getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (const auto* backdrop = dynamic_cast<const StripBackdrop*>(p))
            return synth::theme::nudgeToReadable(text, backdrop->backdropFills());
    return text;
}

} // namespace synth::ui
