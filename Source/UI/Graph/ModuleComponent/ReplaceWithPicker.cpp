// ReplaceWithPicker.cpp -- the rows and launcher behind a module card's "Replace with...".
#include "ReplaceWithPicker.h"

#include "ModuleComponent.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {
namespace {

struct ModEntry {
    const char* name;
    ModuleType type;
};
struct Category {
    const char* header;
    std::vector<ModEntry> modules;
};

const std::vector<Category>& replaceCategories() {
    static const std::vector<Category> categories = {
        {"Sources",
         {{"Oscillator", ModuleType::Oscillator},
          {"Wavetable", ModuleType::Wavetable},
          {"Noise", ModuleType::Noise},
          {"Sampler", ModuleType::Sampler},
          {"LFO", ModuleType::LFO}}},
        {"Sequencing",
         {{"Sequencer", ModuleType::Sequencer},
          {"Poly Sequencer", ModuleType::PolySequencer},
          {"MIDI Keyboard", ModuleType::MidiKeyboard},
          {"Poly MIDI", ModuleType::PolyMidi},
          {"External MIDI", ModuleType::ExternalMidi}}},
        {"Envelopes & Control",
         {{"ADSR", ModuleType::ADSR}, {"Envelope Follower", ModuleType::EnvelopeFollower}, {"VCA", ModuleType::VCA}}},
        {"Filters", {{"Filter", ModuleType::Filter}, {"Parametric EQ", ModuleType::ParametricEQ}}},
        {"Modulation FX",
         {{"Chorus", ModuleType::Chorus},
          {"Phaser", ModuleType::Phaser},
          {"Flanger", ModuleType::Flanger},
          {"Distortion", ModuleType::Distortion},
          {"Ring Modulator", ModuleType::RingModulator},
          {"Bitcrusher", ModuleType::Bitcrusher},
          {"Pitch Shifter", ModuleType::PitchShifter}}},
        {"Time FX", {{"Delay", ModuleType::Delay}, {"Reverb", ModuleType::Reverb}}},
        {"Dynamics",
         {{"Compressor", ModuleType::Compressor}, {"Limiter", ModuleType::Limiter}, {"Gate", ModuleType::Gate}}},
        {"Utility",
         {{"Sample & Hold", ModuleType::SampleHold},
          {"Comparator", ModuleType::Comparator},
          {"Math", ModuleType::Math}}},
    };
    return categories;
}

// "AudioUnit" reads as "AU", the same short form the library sidebar and the Instrument -> Plugin menu use.
juce::String formatLabel(const juce::String& format) { return format == "AudioUnit" ? juce::String("AU") : format; }

void addItem(ReplaceChoices& out, ReplaceChoice choice, ModMatrixPicker::Item item) {
    out.choices.push_back(std::move(choice));
    item.id = (int)out.choices.size();
    out.items.push_back(std::move(item));
}
} // namespace

ReplaceChoices collectReplaceChoices(ModuleType current, const std::optional<synth::PluginIdentity>& currentPlugin,
                                     const std::vector<synth::PluginIdentity>& plugins) {
    ReplaceChoices out;
    for (const auto& cat : replaceCategories()) {
        for (const auto& mod : cat.modules) {
            if (mod.type == current)
                continue;
            ModMatrixPicker::Item item;
            item.category = cat.header;
            item.text = mod.name;
            addItem(out, {mod.name, std::nullopt}, std::move(item));
        }
    }
    for (const auto& plugin : plugins) {
        if (!plugin.isValid() || (currentPlugin.has_value() && *currentPlugin == plugin))
            continue;
        ModMatrixPicker::Item item;
        item.category = "Plugins";
        item.text = plugin.name;
        item.detail = formatLabel(plugin.format);
        item.searchText = "plugin instrument effect " + plugin.format;
        addItem(out, {{}, plugin}, std::move(item));
    }
    return out;
}

std::unique_ptr<ModMatrixPicker> buildReplacePicker(const ReplaceChoices& choices,
                                                    std::function<void(const ReplaceChoice&)> onPick) {
    auto picker = std::make_unique<ModMatrixPicker>("module", choices.items, 0,
                                                    [list = choices.choices, onPick = std::move(onPick)](int id) {
                                                        if (onPick && id >= 1 && id <= (int)list.size())
                                                            onPick(list[(size_t)(id - 1)]);
                                                    });
    picker->setAccessibleNames("Replace module with", "Search modules and plugins to replace this module with");
    return picker;
}

void applyReplaceChoice(GraphEditor& editor, ModuleComponent* card, const ReplaceChoice& choice) {
    if (card == nullptr)
        return;
    if (!choice.plugin.has_value()) {
        editor.replaceModule(card, choice.moduleType);
        return;
    }
    // The identity is set before the node joins the graph, so it is inside the undo snapshot (see
    // GraphEditor::addHostedPluginAtCanvasPosition): undo and redo remember WHICH plugin.
    const auto identity = *choice.plugin;
    editor.replaceModule(card, "Hosted Plugin", [identity](juce::AudioProcessor& processor) {
        if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(&processor))
            hosted->loadPlugin(identity);
    });
}

void showReplacePicker(GraphEditor& editor, ModuleComponent& card, ModuleType current,
                       const std::optional<synth::PluginIdentity>& currentPlugin) {
    const auto plugins =
        editor.installedPluginsProvider ? editor.installedPluginsProvider() : std::vector<synth::PluginIdentity>{};
    juce::Component::SafePointer<ModuleComponent> safeCard(&card);
    juce::Component::SafePointer<GraphEditor> safeEditor(&editor);
    auto picker = buildReplacePicker(collectReplaceChoices(current, currentPlugin, plugins),
                                     [safeCard, safeEditor](const ReplaceChoice& choice) {
                                         if (safeCard != nullptr && safeEditor != nullptr)
                                             applyReplaceChoice(*safeEditor, safeCard.getComponent(), choice);
                                     });
    if (auto& hook = test_hooks::replacePickerHookForTest()) {
        hook(std::move(picker));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(picker), card.getScreenBounds(), nullptr);
}

namespace test_hooks {
std::function<void(std::unique_ptr<ModMatrixPicker>)>& replacePickerHookForTest() {
    static std::function<void(std::unique_ptr<ModMatrixPicker>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
