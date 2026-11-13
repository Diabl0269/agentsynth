#include "TagHue.h"

#include "UI/Theme/Theme.h"
#include <cmath>

namespace synth::ui {

const std::vector<TagStyle>& tagTable() {
    static const std::vector<TagStyle> table = {
        {"Module", TagHue::Module},  {"Track", TagHue::Track},         {"Snippet", TagHue::Module},
        {"Preset", TagHue::Preset},  {"Track preset", TagHue::Preset}, {"Bus preset", TagHue::Preset},
        {"Marker", TagHue::Marker},  {"Action", TagHue::Action},       {"Project", TagHue::AppChrome},
        {"View", TagHue::AppChrome}, {"Setting", TagHue::AppChrome},   {"New", TagHue::New},
    };
    return table;
}

TagHue tagHueOf(const juce::String& tag) {
    for (const auto& style : tagTable())
        if (tag == style.tag)
            return style.hue;
    return TagHue::Unknown;
}

juce::Colour tagColour(TagHue hue, const theme::Colors& c) {
    const auto onPage = [&c](juce::Colour hueColour) {
        // On a light theme the category hues are deepened so the 11 px label keeps its contrast on white.
        return c.bg0.getPerceivedBrightness() > 0.5f ? hueColour.darker(0.3f) : hueColour;
    };
    // Indexes of synth::ui::ModuleCategory in c.cableCategory (see Theme.h).
    constexpr size_t kSources = 0, kSequencing = 1, kEnvelopes = 2, kFilters = 3, kModFx = 4, kUtility = 7;
    switch (hue) {
    case TagHue::Module:
        return onPage(c.cableCategory[kFilters]);
    case TagHue::Track:
        return onPage(c.cableCategory[kEnvelopes]);
    case TagHue::Preset:
        return onPage(c.cableCategory[kModFx]);
    case TagHue::Marker:
        return onPage(c.cableCategory[kSources]);
    case TagHue::Action:
        return onPage(c.cableCategory[kSequencing]);
    case TagHue::AppChrome:
        return onPage(c.cableCategory[kUtility]);
    case TagHue::New:
        return c.textPrimary;
    case TagHue::Unknown:
        break;
    }
    return c.textMuted;
}

juce::Colour tagColour(const juce::String& tag, const theme::Colors& colors) {
    return tagColour(tagHueOf(tag), colors);
}

int tagWidth(const juce::String& label) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(juce::FontOptions(kTagFontSize, juce::Font::bold)), label, 0.0f, 0.0f);
    return (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()) + 12;
}

void drawTag(juce::Graphics& g, juce::Rectangle<float> box, const juce::String& label, const theme::Colors& colors) {
    const auto hue = tagColour(label, colors);
    g.setColour(hue.withAlpha(kTagWashAlpha));
    g.fillRoundedRectangle(box.reduced(0.5f), 4.0f);
    g.setColour(hue);
    g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);
    g.setFont(juce::Font(juce::FontOptions(kTagFontSize, juce::Font::bold)));
    g.drawText(label, box, juce::Justification::centred, false);
}

} // namespace synth::ui
