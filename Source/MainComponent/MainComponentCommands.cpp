// MainComponentCommands.cpp — MainComponent's juce::ApplicationCommandTarget implementation:
// paint() plus the getAllCommands/getCommandInfo/perform trio (kept together — see the class's
// own comment on why they must not be split). FRO76: all three are now table lookups over
// commandTable() (MainComponentCommandTable.cpp) rather than a per-command switch — see that
// file for the table itself, the named perform() bodies, and the isActive predicates.
// MainComponent is declared in MainComponent.h; the rest of its implementation lives in the
// sibling MainComponent*.cpp units next to this one.
#include "MainComponent.h"

//==============================================================================
void MainComponent::paint(juce::Graphics& g) {
    // (Our component is opaque, so we must completely fill the background with a
    // solid colour)
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands) {
    for (const auto& spec : commandTable())
        commands.add(spec.id);
}

void MainComponent::getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) {
    for (const auto& spec : commandTable()) {
        if (spec.id != commandID)
            continue;
        // name == nullptr means the block's display name is derived from its actionId (the
        // snap/zoom command blocks, which share one description/category per block) — see
        // ShortcutManager::getActionDescription.
        const juce::String name =
            spec.name != nullptr ? juce::String(spec.name) : ShortcutManager::getActionDescription(spec.actionId);
        result.setInfo(name, spec.description, spec.category, 0);
        if (spec.actionId != nullptr) {
            auto kp = shortcutManager.getBinding(spec.actionId);
            result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        }
        result.setActive(spec.isActive ? spec.isActive(*this) : true);
        return;
    }
}

bool MainComponent::perform(const InvocationInfo& info) {
    for (const auto& spec : commandTable()) {
        if (spec.id != info.commandID)
            continue;
        return spec.run(*this);
    }
    return false;
}
