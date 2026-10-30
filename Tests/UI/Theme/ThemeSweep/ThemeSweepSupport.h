#pragma once

// ThemeSweepSupport.h -- the rendering and measuring helpers the theme sweep shares: draw a component into a software
// image, optionally dump it as a PNG, and list every visible text-drawing control with its text colour and the
// background actually rendered behind it (docs/layout/theming.md, "Theme sweep guard").

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::test::theme_sweep {

/** The WCAG AA bar for body text. */
constexpr double kMinTextContrast = 4.5;

/** One control that draws text: where it is, its text colour and the background rendered behind it. */
struct TextSample {
    juce::String description; // "<Kind> 'text'" or "<Kind> #name"
    juce::Colour text;
    juce::Colour background;
    double ratio = 21.0;
};

/** `root` painted (children included) into a software ARGB image of its size. Pixels nothing draws stay transparent. */
juce::Image renderToImage(juce::Component& root);

/** WCAG contrast of `text` over `background`; a translucent `text` is composited over `background` first. */
double contrastRatio(juce::Colour text, juce::Colour background);

/** Writes `image` to build-ci-local/theme-sweep/<surface>-<theme>.png when AGENTSYNTH_WRITE_THEME_PNG is set. */
void writeSweepPng(const juce::String& surfaceName, const juce::String& themeId, const juce::Image& image);

/** The visible, enabled Labels, TextButtons, ToggleButtons and ComboBoxes under `root` (root included) that have text.
 *  `image` is root's own rendering; a sampled background that is translucent is composited over `underlay`. */
std::vector<TextSample> collectTextSamples(juce::Component& root, const juce::Image& image, juce::Colour underlay);

/** A filesystem-safe name for a surface ("Sampler map" -> "sampler-map"). */
juce::String slug(const juce::String& name);

} // namespace synth::test::theme_sweep
