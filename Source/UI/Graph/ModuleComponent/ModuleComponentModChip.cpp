// The modulation hover chip stack -- one "<source> · <+NN%>" row per source under a knob whose routing is
// hover-correlated (a hovered knob-landing cable, or the knob itself) or has a mod dot readout up. Painted in
// paintOverChildren so it sits above the next row's knob labels instead of under them.
// docs/modules/modulation.md#modulation-rings-on-knobs.

#include "ModuleComponentModChip.h"
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModDot/ModDotTooltip.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

void ModuleComponent::paintOverChildren(juce::Graphics& g) { paintModHoverChip(g); }

namespace {
constexpr float kModChipRowHeight = 16.0f;
constexpr float kModChipRowStride = 17.0f;
constexpr float kModChipSwatchSize = 6.0f;
constexpr float kModChipSwatchGap = 4.0f;
constexpr float kModChipPadX = 6.0f;
} // namespace

namespace synth::ui {
namespace {
// The mod dot readout, when one is up on `card`.
const ModDotReadout* readoutOn(GraphEditor& editor, juce::AudioProcessorGraph::NodeID card) {
    const auto* readout = editor.getModDot().getTooltip().readout();
    return readout != nullptr && readout->card == card ? readout : nullptr;
}
} // namespace

bool modHoverChipShows(GraphEditor& editor, juce::AudioProcessorGraph::NodeID card) {
    const auto& hovered = editor.getHoveredModTarget();
    return (hovered.has_value() && hovered->nodeId == card) || readoutOn(editor, card) != nullptr;
}

// Capped so a heavily patched knob's stack never grows past the card; the chosen source stays visible.
std::vector<ModChipRow> modHoverChipRows(const std::vector<KnobModSource>& sources,
                                         juce::AudioProcessorGraph::NodeID chosen, int maxRows) {
    std::vector<const KnobModSource*> listed;
    for (const auto& source : sources)
        if (!source.bypassed && source.sourceName.isNotEmpty())
            listed.push_back(&source);
    const int total = static_cast<int>(listed.size());
    const int cap = juce::jmax(1, maxRows);
    std::vector<ModChipRow> rows;
    if (total > cap) {
        const auto chosenAt = std::find_if(listed.begin(), listed.end(),
                                           [&chosen](const KnobModSource* s) { return s->attenuverterId == chosen; });
        if (chosenAt != listed.end() && std::distance(listed.begin(), chosenAt) >= cap)
            std::swap(listed[static_cast<size_t>(cap - 1)], *chosenAt);
        listed.resize(static_cast<size_t>(cap));
    }
    for (const auto* source : listed)
        rows.push_back({formatModHoverChipText(source->sourceName, source->amount),
                        source->attenuverterId == chosen && chosen.uid != 0, source->amount >= 0.0f, false});
    if (total > cap)
        rows.push_back({juce::String("+") + juce::String(total - cap) + " more", false, true, true});
    return rows;
}
} // namespace synth::ui

namespace {
void paintChipRow(juce::Graphics& g, const synth::theme::Theme& theme, const synth::ui::ModChipRow& row,
                  const juce::Rectangle<float>& bounds, const juce::Font& font, float alpha) {
    const auto& colors = theme.colors;
    // Opaque even when a theme's surfaceHi is translucent -- the chip sits over the next row's knob label and must
    // hide it, not show it through.
    g.setColour(colors.surface.overlaidWith(colors.surfaceHi).withAlpha(1.0f).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, theme.metrics.cornerRadiusSmall);
    auto textArea = bounds;
    if (!row.isMore) {
        const auto swatch =
            juce::Rectangle<float>(kModChipSwatchSize, kModChipSwatchSize)
                .withCentre({bounds.getX() + kModChipPadX + kModChipSwatchSize * 0.5f, bounds.getCentreY()});
        g.setColour((row.positive ? colors.modRingPositive : colors.modRingNegative).withMultipliedAlpha(alpha));
        g.fillEllipse(swatch);
        textArea = textArea.withTrimmedLeft(kModChipPadX + kModChipSwatchSize + kModChipSwatchGap);
    }
    g.setColour(colors.textPrimary.withMultipliedAlpha(alpha));
    g.setFont(font);
    g.drawText(row.text, textArea, row.isMore ? juce::Justification::centred : juce::Justification::centredLeft, true);
}
} // namespace

namespace synth::ui {
ModChipStack modChipStack(GraphEditor& owner, juce::AudioProcessorGraph::NodeID nodeId, ModuleBase* mod,
                          const std::function<int(const ModulationTarget&)>& sliderFor) {
    ModChipStack stack;
    if (!modHoverChipShows(owner, nodeId))
        return stack;
    if (mod == nullptr)
        return stack;

    // A dot readout names the knob it adjusts, wherever the pointer is; otherwise it is the hovered knob.
    const auto* readout = owner.getModDot().getTooltip().readout();
    if (readout != nullptr && readout->card != nodeId)
        readout = nullptr;
    const auto& hovered = owner.getHoveredModTarget();
    const bool hoveredHere = hovered.has_value() && hovered->nodeId == nodeId;
    const int channel = readout != nullptr ? readout->destChannel : hovered->channel;
    // While the readout fades the chip fades with it, unless the pointer still rests on the knob.
    stack.alpha = (hoveredHere && hovered->channel == channel) ? 1.0f : owner.getModDot().getTooltip().getOpacity();

    for (const auto& t : mod->getModulationTargets())
        if (t.channelIndex == channel) {
            stack.sliderIndex = sliderFor(t);
            break;
        }
    if (stack.sliderIndex < 0)
        return stack;

    // The source the dot's drag adjusts (the last one chosen), named the way the Mod Matrix names it:
    // the real module behind any macro port, not the first routing's raw source. A dragged source shows
    // its live amount, which the editor's cached routings only catch up with on the next tick.
    const auto chosen =
        readout != nullptr ? readout->attenuverter : owner.getModDot().chosenAttenuverter(nodeId, channel);
    auto sources = knobModSources(owner, nodeId, channel);
    if (readout != nullptr)
        for (auto& source : sources)
            if (source.attenuverterId == readout->attenuverter)
                source.amount = readout->amount;
    stack.rows = modHoverChipRows(sources, chosen);
    return stack;
}
} // namespace synth::ui

namespace {
float chipRowWidth(const juce::Font& font, const synth::ui::ModChipRow& row) {
    return juce::GlyphArrangement::getStringWidth(font, row.text) + kModChipPadX * 2.0f +
           (row.isMore ? 0.0f : kModChipSwatchSize + kModChipSwatchGap);
}

juce::Font chipFont(const synth::theme::AppLookAndFeel& lf, bool bold) {
    return juce::Font(
        juce::FontOptions(lf.getTheme().type.monoFamily, 11.0f, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace

namespace synth::ui {
// A stack under the knob is clipped by the card's bottom edge when the knob sits in the last row, so it flips above
// the knob, and when the card is too short for that too it takes the lowest position that still fits.
std::vector<juce::Rectangle<float>> modChipRowRects(juce::Rectangle<float> knob, const std::vector<float>& widths,
                                                    juce::Rectangle<float> card) {
    std::vector<juce::Rectangle<float>> rects;
    if (widths.empty())
        return rects;
    const float height =
        kModChipRowStride * static_cast<float>(widths.size()) - (kModChipRowStride - kModChipRowHeight);
    float top = knob.getBottom() + 2.0f;
    if (top + height > card.getBottom()) {
        const float above = knob.getY() - 2.0f - height;
        top = above >= card.getY() ? above : juce::jmax(card.getY(), card.getBottom() - height);
    }
    float y = top;
    for (const float width : widths) {
        const float x = juce::jlimit(card.getX(), juce::jmax(card.getX(), card.getRight() - width),
                                     knob.getCentreX() - width * 0.5f);
        rects.emplace_back(x, y, width, kModChipRowHeight);
        y += kModChipRowStride;
    }
    return rects;
}
} // namespace synth::ui

namespace synth::ui {
std::vector<juce::Rectangle<float>> modHoverChipRects(const juce::Component& card, const ModChipStack& stack,
                                                      juce::Rectangle<int> knob) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&card.getLookAndFeel());
    if (lf == nullptr || stack.rows.empty() || stack.sliderIndex < 0)
        return {};
    std::vector<float> widths;
    for (const auto& row : stack.rows)
        widths.push_back(chipRowWidth(chipFont(*lf, row.bold), row));
    return modChipRowRects(knob.toFloat(), widths, card.getLocalBounds().toFloat());
}
} // namespace synth::ui

void ModuleComponent::paintModHoverChip(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;
    const auto stack =
        synth::ui::modChipStack(owner, nodeId, dynamic_cast<ModuleBase*>(module),
                                [this](const ModulationTarget& t) { return sliderIndexForModTarget(t); });
    if (stack.alpha <= 0.0f)
        return;
    const auto rects = synth::ui::modHoverChipRects(
        *this, stack, stack.sliderIndex < 0 ? juce::Rectangle<int>() : sliders[stack.sliderIndex]->getBounds());
    for (size_t i = 0; i < rects.size(); ++i) {
        const auto& row = stack.rows[i];
        paintChipRow(g, lf->getTheme(), row, rects[i], chipFont(*lf, row.bold), stack.alpha);
    }
}
