// A card's header draws its Mute button exactly when the module has a mute parameter: a module with none (a sequencer,
// the Voice Mixer, a MIDI source) shows no Mute that does nothing, and one that has it keeps the button wired and
// named.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"

#include <gtest/gtest.h>

namespace {

juce::DrawableButton* headerButtonNamed(ModuleComponent& card, const juce::String& name) {
    for (auto* child : card.getChildren())
        if (auto* button = dynamic_cast<juce::DrawableButton*>(child); button != nullptr && button->getName() == name)
            return button;
    return nullptr;
}

} // namespace

TEST(ModuleHeaderMute, EveryCardShowsMuteExactlyWhenItsModuleCanMute) {
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames())
        types.addIfNotAlreadyThere(type);

    AudioEngine engine;
    GraphEditor editor(engine);
    int withMute = 0;
    int withoutMute = 0;
    for (const auto& type : types) {
        auto processor = synth::AIStateMapper::createModule(type);
        if (processor == nullptr)
            continue;
        auto* module = dynamic_cast<ModuleBase*>(processor.get());
        if (module == nullptr)
            continue; // not a ModuleBase (the graph's Audio Output)
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
        if (headerButtonNamed(card, "Bypass") == nullptr)
            continue; // a card with no header chrome at all (Attenuverter, macro ports)
        auto* mute = headerButtonNamed(card, "Mute");
        EXPECT_EQ(mute != nullptr, module->hasMuteParameter()) << type;
        if (mute != nullptr) {
            ++withMute;
            EXPECT_EQ(mute->getTooltip(), "Mute") << type;
        } else {
            ++withoutMute;
        }
    }
    EXPECT_GT(withMute, 20) << "the guard saw the cards that keep Mute";
    EXPECT_GE(withoutMute, 5) << "the guard saw the cards that lose it";
}
