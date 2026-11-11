// AIStateMapper -- the Markdown module table the system prompt carries (getModuleSchema).
//
// Read from the same module factory as the catalog and the schema, so the model is told exactly the parameter ids,
// ranges and choice names a patch accepts. The class itself is declared in AIStateMapper.h.

#include "AIStateMapper.h"

#include "AIStateMapperInternal.h"

namespace synth {

namespace {

// The steps of a sequencer are the node's "state", not parameters, so the parameter table above cannot show them.
juce::String sequencerStateNote(bool poly) {
    return juce::String(
               "Steps are the node's `state`, e.g. `{\"steps\": [{\"pitch\": 48}, {\"pitch\": 55, "
               "\"gate\": 0.3}]}` (`steps` above = how many play). Per step, only what you change: pitch 0-127, "
               "on, gate 0.05-1, velocity 1-127, chance 0-100, repeat 1-8, nudge -0.5-0.5, condition "
               "\"2/4\", ") +
           (poly ? "chord (Unison, Major, Minor, Maj7, Min7, 5ths, Octs, Random, Scale Triad, Scale Seventh, Dom7, "
                   "Dim, Aug, Sus2, Sus4, ...)."
                 : "slide, filter 0-1.") +
           "\n\n";
}

// Bands are added, not slots to fill: the model is told to turn on as many as the sound needs. The names come from
// the module's own choices, so the text can never drift from what a patch may say.
juce::String parametricEqNote(juce::AudioProcessor& processor) {
    juce::StringArray types, slopes;
    for (auto* param : processor.getParameters())
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
            if (choice->paramID == "band1Type")
                types = choice->choices;
            else if (choice->paramID == "band1Slope")
                slopes = choice->choices;
        }
    return "Bands are ADDED, not slots to fill: the EQ has up to 24 bands (`band1On` ... `band24On`, each with "
           "`bandNFreq`, `bandNGain`, `bandNQ`, `bandNType`, `bandNSlope`). Turn on as many as the sound needs, "
           "starting from the first free (off) slot, and set each one's `bandNType` and, for a Low Cut or High Cut, "
           "its `bandNSlope`. A band you leave out stays off. `bandNType` is one of: " +
           types.joinIntoString(", ") + ". `bandNSlope` is one of: " + slopes.joinIntoString(", ") +
           ". Gain applies to Low Shelf, Bell, High Shelf and Tilt; a cut, Notch or Band Pass ignores it. Example, a "
           "steep low cut and a notch: `{\"band1On\": true, \"band1Type\": \"Low Cut\", \"band1Freq\": 80, "
           "\"band1Slope\": \"48 dB/oct\", \"band2On\": true, \"band2Type\": \"Notch\", \"band2Freq\": 2500, "
           "\"band2Q\": 8}`.\n\n";
}

} // namespace

juce::String AIStateMapper::getModuleSchema() {
    juce::String schema = "### Available Modules and Parameters\n\n";

    for (const auto& entry : detail::moduleFactory()) {
        // Hide non-authorable modules from AI — modulation uses the "modulations" array instead
        if (detail::isInternalOnlyModule(entry.first))
            continue;

        auto processor = entry.second();
        if (!processor)
            continue;

        schema += "#### " + entry.first + "\n";
        schema += "| Parameter ID | Name | Range / Options | Default |\n";
        schema += "| :--- | :--- | :--- | :--- |\n";

        for (auto* param : processor->getParameters()) {
            if (auto* p = detail::modelVisibleParam(*processor, param)) {
                juce::String rangeStr;
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                    rangeStr = "Choice: [" + choice->choices.joinIntoString(", ") + "]";
                } else if (dynamic_cast<juce::AudioParameterBool*>(param)) {
                    rangeStr = "Boolean (0 or 1)";
                } else {
                    auto range = p->getNormalisableRange();
                    rangeStr = juce::String(range.start) + " to " + juce::String(range.end);
                }

                schema += "| `" + p->paramID + "` | " + p->name + " | " + rangeStr + " | " +
                          juce::String(p->getDefaultValue()) + " |\n";
            }
        }
        schema += "\n";
        if (entry.first == "Sequencer" || entry.first == "Poly Sequencer")
            schema += sequencerStateNote(entry.first == "Poly Sequencer");
        if (entry.first == "Parametric EQ")
            schema += parametricEqNote(*processor);
    }

    // Modulation targets section
    schema += "### Modulation Targets\n\n";
    schema += "Use the `modulations` array to route modulation sources to these targets.\n\n";
    schema += "| Module | Target | Port |\n";
    schema += "| :--- | :--- | :--- |\n";

    for (const auto& entry : detail::moduleFactory()) {
        auto processor = entry.second();
        if (!processor)
            continue;
        if (auto* mb = dynamic_cast<ModuleBase*>(processor.get())) {
            auto targets = mb->getModulationTargets();
            for (const auto& t : targets) {
                schema += "| " + entry.first + " | " + t.name + " | " + juce::String(t.channelIndex) + " |\n";
            }
        }
    }

    schema += "\n**Modulation Sources**: LFO, ADSR, Amp Env, Filter Env, Oscillator, Sequencer\n\n";

    return schema;
}

} // namespace synth
