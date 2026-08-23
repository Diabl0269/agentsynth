#include "IconLibrary.h"

#ifdef HAS_FONT_ASSETS
#include "BinaryData.h"
#endif

namespace synth::theme {

//==============================================================================
IconLibrary::IconLibrary() {
    for (int i = 0; i < (int)Icon::kCount; ++i) {
        const auto [data, size] = binaryDataForIcon(static_cast<Icon>(i));
        if (data == nullptr || size == 0)
            continue; // headless / missing asset → leave both arrays null at this index

        originals_[(size_t)i] = loadSVG(data, size); // pure-white master copy
        if (originals_[(size_t)i] != nullptr)
            drawables_[(size_t)i] = originals_[(size_t)i]->createCopy(); // white until first tint
    }
}

//==============================================================================
void IconLibrary::setTintColour(Icon id, juce::Colour c) {
    const auto idx = static_cast<size_t>(id);
    if (originals_[idx] == nullptr)
        return;

    // Always clone the UNTINTED original, then tint the clone. This ensures the 2nd, 3rd, ...
    // theme switch produces the correct colour (not a re-tint of an already-tinted drawable).
    auto clone = originals_[idx]->createCopy();
    clone->replaceColour(juce::Colours::white, c);
    drawables_[idx] = std::move(clone);
}

std::unique_ptr<juce::Drawable> IconLibrary::getDrawable(Icon id) const {
    const auto idx = static_cast<size_t>(id);
    if (drawables_[idx] == nullptr)
        return nullptr;
    return drawables_[idx]->createCopy();
}

const juce::Drawable* IconLibrary::peekDrawable(Icon id) const noexcept {
    return drawables_[static_cast<size_t>(id)].get();
}

//==============================================================================
std::pair<const void*, int> IconLibrary::binaryDataForIcon(Icon id) {
#ifdef HAS_FONT_ASSETS
    // Exhaustive lookup table — if Icon::kCount changes, this array must be updated.
    // The static_assert below guards the count.
    //
    // NOTE: JUCE's binary-data name mangler STRIPS hyphens (and concatenates the resulting
    // tokens) rather than converting them to underscores, so 'transport-play.svg' becomes
    // BinaryData::transportplay_svg (verified against the generated BinaryData.h). The dot
    // before the extension is the only separator that becomes '_'.
    static const std::pair<const void*, int> kTable[(size_t)Icon::kCount] = {
        {BinaryData::transportplay_svg, BinaryData::transportplay_svgSize},
        {BinaryData::transportstop_svg, BinaryData::transportstop_svgSize},
        {BinaryData::actionundo_svg, BinaryData::actionundo_svgSize},
        {BinaryData::actionredo_svg, BinaryData::actionredo_svgSize},
        {BinaryData::actionsave_svg, BinaryData::actionsave_svgSize},
        {BinaryData::actionload_svg, BinaryData::actionload_svgSize},
        {BinaryData::actionnew_svg, BinaryData::actionnew_svgSize},
        {BinaryData::actionsettings_svg, BinaryData::actionsettings_svgSize},
        {BinaryData::actionautoarrange_svg, BinaryData::actionautoarrange_svgSize},
        {BinaryData::toggleai_svg, BinaryData::toggleai_svgSize},
        {BinaryData::togglematrix_svg, BinaryData::togglematrix_svgSize},
        {BinaryData::togglelibrary_svg, BinaryData::togglelibrary_svgSize},
        {BinaryData::themetoggle_svg, BinaryData::themetoggle_svgSize},
        {BinaryData::modulebypass_svg, BinaryData::modulebypass_svgSize},
        {BinaryData::modulemute_svg, BinaryData::modulemute_svgSize},
        {BinaryData::moduledelete_svg, BinaryData::moduledelete_svgSize},
        {BinaryData::catsources_svg, BinaryData::catsources_svgSize},
        {BinaryData::catsequencing_svg, BinaryData::catsequencing_svgSize},
        {BinaryData::catenvelopes_svg, BinaryData::catenvelopes_svgSize},
        {BinaryData::catfilters_svg, BinaryData::catfilters_svgSize},
        {BinaryData::catmodulationfx_svg, BinaryData::catmodulationfx_svgSize},
        {BinaryData::cattimefx_svg, BinaryData::cattimefx_svgSize},
        {BinaryData::catdynamics_svg, BinaryData::catdynamics_svgSize},
        {BinaryData::catutility_svg, BinaryData::catutility_svgSize},
        // Waveform glyphs (Phase 4).
        {BinaryData::waveformsine_svg, BinaryData::waveformsine_svgSize},
        {BinaryData::waveformsaw_svg, BinaryData::waveformsaw_svgSize},
        {BinaryData::waveformsquare_svg, BinaryData::waveformsquare_svgSize},
        {BinaryData::waveformtriangle_svg, BinaryData::waveformtriangle_svgSize},
        // Minimap toggle (issue #159).
        {BinaryData::toggleminimap_svg, BinaryData::toggleminimap_svgSize},
        {BinaryData::moduledualio_svg, BinaryData::moduledualio_svgSize},
        // Timeline edit-tool strip (Cubase-style tools; see Source/UI/EditTool.h).
        {BinaryData::toolselect_svg, BinaryData::toolselect_svgSize},
        {BinaryData::toolsplit_svg, BinaryData::toolsplit_svgSize},
        {BinaryData::toolglue_svg, BinaryData::toolglue_svgSize},
        {BinaryData::toolerase_svg, BinaryData::toolerase_svgSize},
        {BinaryData::toolmute_svg, BinaryData::toolmute_svgSize},
        {BinaryData::tooldraw_svg, BinaryData::tooldraw_svgSize},
        // Timeline track-header kind glyphs + the panel's follow-playhead toggle.
        {BinaryData::trackmidi_svg, BinaryData::trackmidi_svgSize},
        {BinaryData::trackaudio_svg, BinaryData::trackaudio_svgSize},
        {BinaryData::trackautomation_svg, BinaryData::trackautomation_svgSize},
        {BinaryData::followplayhead_svg, BinaryData::followplayhead_svgSize},
    };
    static_assert(std::size(kTable) == (size_t)Icon::kCount,
                  "kTable size does not match Icon::kCount — update binaryDataForIcon lookup table");

    return kTable[static_cast<size_t>(id)];
#else
    juce::ignoreUnused(id);
    return {nullptr, 0}; // headless fallback (no asset library linked)
#endif
}

std::unique_ptr<juce::Drawable> IconLibrary::loadSVG(const void* data, int size) {
    if (data == nullptr || size == 0)
        return nullptr;

    auto xml = juce::parseXML(juce::String::createStringFromData(data, size));
    if (xml == nullptr)
        return nullptr;

    return juce::Drawable::createFromSVG(*xml);
}

} // namespace synth::theme
