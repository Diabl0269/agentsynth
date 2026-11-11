// ModMatrixPickerFirstRowTests.cpp -- every ModMatrixPicker producer opens its list at the first row.
//
// The picker is built (and highlights its first row) before it has a size; scrolling to that highlight while the view
// had no height pushed the first header and rows out of sight. One guard for the whole kind: every producer builds its
// real picker and is checked after construction, after being sized as setFooter does, and inside its call-out.
//
// A new producer of a ModMatrixPicker joins this list (here when it has a pure builder, otherwise in its own fixture's
// test file, calling first_row_test::expectOpensAtFirstRow):
//   here: add modulator, change source, change/duplicate lane parameter and add automation (both
//         buildAddAutomationPicker), add track, Replace with;
//   ModMatrixPickerTests.cpp: the Mod Matrix source and destination pickers;
//   MixerInsertListAddTests.cpp: the mixer insert picker;
//   SamplerInstrumentCardTests.cpp: the instrument picker;
//   HostedPluginCardModTests.cpp: the hosted mod target picker;
//   AutomationLanesAddMainTests.cpp: the timeline's add-automation picker.
// The command palette hosts its picker in a window of its own and is not a call-out.
#include "ModMatrixPickerFirstRow.h"

#include "UI/Graph/ModuleComponent/ReplaceWithPicker.h"
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationPicker.h"
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/TimelinePanelComponent/AddTrackPicker.h"

namespace {

using first_row_test::expectOpensAtFirstRow;
using synth::ui::ModMatrixPicker;

// Longer than the picker's visible height, so a wrong scroll cannot be clamped back by the view's own limits.
std::vector<synth::ui::TrackHeaderHost::LfoChoice> manySources() {
    std::vector<synth::ui::TrackHeaderHost::LfoChoice> sources;
    for (int i = 0; i < 24; ++i) {
        synth::ui::TrackHeaderHost::LfoChoice source;
        source.uuid = "lfo-" + juce::String(i);
        source.name = (i % 4 == 3 ? "Envelope " : "LFO ") + juce::String(i + 1);
        source.isEnvelope = i % 4 == 3;
        source.targets = {"Filter Cutoff"};
        sources.push_back(source);
    }
    return sources;
}

} // namespace

TEST(ModMatrixPickerFirstRow, AddModulatorOpensAtTheNewHeading) {
    const auto choices = synth::ui::collectAddModulatorChoices(manySources(), "Cutoff");
    auto picker = synth::ui::buildAddModulatorPicker(choices, "Cutoff", {});
    ASSERT_EQ(picker->getVisibleRowNamesForTest().front(), "New");
    expectOpensAtFirstRow(*picker, "add modulator");
}

TEST(ModMatrixPickerFirstRow, ChangeSourceOpensAtItsFirstHeading) {
    const auto choices = synth::ui::collectChangeSourceChoices(manySources(), "lfo-1", 0, "Cutoff");
    auto picker = synth::ui::buildAddModulatorPicker(choices, "Cutoff", {});
    expectOpensAtFirstRow(*picker, "change source");
}

TEST(ModMatrixPickerFirstRow, AddAutomationAndLaneParameterOpenAtTheFirstModuleHeading) {
    synth::ui::AddAutomationChoices choices;
    for (int module = 0; module < 6; ++module)
        for (int param = 0; param < 8; ++param) {
            synth::ui::TrackHeaderHost::AutomatableParameter parameter;
            parameter.moduleTitle = "Module " + juce::String(module);
            parameter.parameterName = "Parameter " + juce::String(param);
            choices.parameters.push_back(parameter);
            ModMatrixPicker::Item item;
            item.id = (int)choices.parameters.size();
            item.category = parameter.moduleTitle;
            item.text = parameter.parameterName;
            choices.items.push_back(item);
        }
    auto picker = synth::ui::buildAddAutomationPicker(choices, "Track 1", {});
    expectOpensAtFirstRow(*picker, "add automation / lane parameter");
}

TEST(ModMatrixPickerFirstRow, AddTrackOpensAtItsFirstHeading) {
    std::vector<ModMatrixPicker::Item> items;
    for (int i = 0; i < 30; ++i) {
        ModMatrixPicker::Item item;
        item.id = i + 1;
        item.category = i < 10 ? "Tracks" : i < 20 ? "Instruments" : "Presets";
        item.text = "Choice " + juce::String(i);
        items.push_back(item);
    }
    auto picker = synth::ui::buildAddTrackPicker(std::move(items), {}, {});
    expectOpensAtFirstRow(*picker, "add track");
}

TEST(ModMatrixPickerFirstRow, ReplaceWithOpensAtTheFirstCategory) {
    const auto choices = synth::ui::collectReplaceChoices(ModuleType::Oscillator, std::nullopt, {});
    auto picker = synth::ui::buildReplacePicker(choices, {});
    expectOpensAtFirstRow(*picker, "replace with");
}
