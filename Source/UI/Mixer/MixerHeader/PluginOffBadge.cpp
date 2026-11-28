// Concern: the words and the fade of the "plugin switched off after a crash" badge (PluginOffBadge.h).
#include "PluginOffBadge.h"

#include "UI/Layout/HelperTooltip.h"

namespace synth::ui {

PluginOffBadge::PluginOffBadge()
    : MixerIconButton("pluginOffBadge") {
    setIcon(synth::theme::Icon::ModuleBypass);
    setComponentID("pluginOffBadge");
    // It reports something nothing else on the track shows, so its tip keeps showing when info tooltips are off.
    markHelperTooltip(*this);
    setVisible(false);
}

juce::String PluginOffBadge::wordsFor(const juce::StringArray& pluginTitles) {
    if (pluginTitles.isEmpty())
        return {};
    return (pluginTitles.size() == 1 ? "Plugin switched off after a crash: " : "Plugins switched off after a crash: ") +
           pluginTitles.joinIntoString(", ") + ". Press to show it, then use Turn on on its card.";
}

void PluginOffBadge::setPlugins(const juce::StringArray& pluginTitles, bool snap) {
    const bool show = !pluginTitles.isEmpty();
    if (pluginTitles != titles_) {
        titles_ = pluginTitles;
        const auto words = wordsFor(titles_);
        // Kept while the badge fades out, so the last words never flash empty.
        if (show) {
            setTitle(words);
            setDescription(words);
            setTooltip(words);
        }
    }
    if (snap)
        fade_.snapTo(show);
    else
        fade_.setShown(show);
}

} // namespace synth::ui
