#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth::theme {
struct Colors;
}

// TagHue (docs/layout/theming.md#colour-code-kinds): the one place that says which hue a row's type tag ("Module",
// "Track", "Preset", ...) is drawn in, for the + Add lists and Cmd+K alike. Every kind has a hue, outline and text in
// it with a faint wash of the same hue behind; the hues are theme tokens (the eight category hues the cables use, tuned
// per theme for contrast), so all four themes stay readable. An unknown tag falls back to the muted text colour, which
// the tests treat as a failure for any tag the app can set.
namespace synth::ui {

constexpr float kTagFontSize = 11.0f;
constexpr int kTagHeight = 17; // the tag's box; the row is 24 tall
constexpr float kTagWashAlpha = 0.12f;

/** The hue slots a tag can take. Kinds that mean nearly the same thing share one slot (Module and Snippet; the three
 * preset tags; View, Setting and Project, all the app's own); every other kind has its own. */
enum class TagHue { Module, Track, Preset, Marker, Action, AppChrome, New, Unknown };

struct TagStyle {
    const char* tag;
    TagHue hue;
};

/** Every tag the app sets, with its hue slot: the table docs/layout/theming.md prints. */
const std::vector<TagStyle>& tagTable();

TagHue tagHueOf(const juce::String& tag);

/** The slot's colour in this theme. */
juce::Colour tagColour(TagHue hue, const theme::Colors& colors);

/** The tag's colour in this theme; the muted text colour for a tag that is not in the table. */
juce::Colour tagColour(const juce::String& tag, const theme::Colors& colors);

/** The width of a tag's box for this label (text plus padding). */
int tagWidth(const juce::String& label);

/** Draws the tag in `box`: a faint wash, the outline and the label, all in the tag's hue. The only place a tag's
 *  outline is drawn. */
void drawTag(juce::Graphics& g, juce::Rectangle<float> box, const juce::String& label, const theme::Colors& colors);

} // namespace synth::ui
