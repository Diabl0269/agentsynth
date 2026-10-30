#include "UI/Graph/GraphEditor/CableEndName.h"

#include "Plugin/Hosting/HostedPluginModule.h"

namespace synth::ui {

juce::String cableEndName(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID dest, int channel) {
    auto* node = graph.getNodeForId(dest);
    auto* hosted = node != nullptr ? dynamic_cast<HostedPluginModule*>(node->getProcessor()) : nullptr;
    return hosted != nullptr ? hosted->modSlotParamName(channel) : juce::String();
}

void paintCableEndName(juce::Graphics& g, juce::Point<float> dot, const juce::String& name,
                       const synth::theme::Theme& theme, float alpha) {
    constexpr float kFontSize = 10.0f;
    constexpr float kHeight = 14.0f;
    constexpr float kPadding = 4.0f;
    constexpr float kGapBeforeDot = 8.0f;
    const juce::Font font{juce::FontOptions(kFontSize)};
    const float width = juce::GlyphArrangement::getStringWidth(font, name) + 2.0f * kPadding;
    const juce::Rectangle<float> pill(dot.x - kGapBeforeDot - width, dot.y - kHeight * 0.5f, width, kHeight);
    g.setColour(theme.colors.surface.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(pill, kHeight * 0.5f);
    g.setColour(theme.colors.textPrimary.withMultipliedAlpha(alpha));
    g.setFont(font);
    g.drawText(name, pill, juce::Justification::centred, false);
}

} // namespace synth::ui
