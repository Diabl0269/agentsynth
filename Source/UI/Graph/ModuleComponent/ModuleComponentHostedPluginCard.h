#pragma once

// Private to the ModuleComponent units (ModuleComponent.cpp and ModuleComponentHostedPluginCard.cpp): the
// definition of the hosted-plugin card's binding to its module.
// Nothing outside those units should include this header.

#include "ModuleComponent.h"
#include "Plugin/Hosting/HostedPluginCardLayout.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include <set>

/**
 * What a hosted-plugin card listens to: the module's instance edges, its per-instance layout override and the
 * layout store. Owned by the card; every member is message-thread only.
 */
class ModuleComponent::HostedCardBinding final
    : private synth::HostedPluginModule::InstanceObserver
    , private synth::PluginCardLayoutStore::Listener {
public:
    HostedCardBinding(ModuleComponent& card, synth::HostedPluginModule& module, synth::PluginCardLayoutStore* store);
    ~HostedCardBinding() override;

    /** Leaves every registration made in the constructor. Idempotent; safe once the module is gone. */
    void shutdown();

    /** The module, or null once it has been destroyed. */
    synth::HostedPluginModule* getModule() const { return module_.get(); }

    /** Adds the widget, label and attachment for one slot; skips an orphaned or unresolved slot. */
    void addControl(const synth::ResolvedCardSlot& resolved);

    /** The instance the card's attachments are bound to; null while unbound. */
    juce::AudioPluginInstance* boundInstance = nullptr;
    /** Parameters with a gesture in flight, so overlapping gestures still make one undo step. */
    std::set<const juce::AudioProcessorParameter*> activeGestures;

private:
    void addKnob(const synth::ResolvedCardSlot& resolved, const juce::String& text);
    void addToggle(const synth::ResolvedCardSlot& resolved, const juce::String& text);
    void addChoice(const synth::ResolvedCardSlot& resolved, const juce::String& text);
    void wireGestures(synth::ui::HostedParameterAttachment& attachment, const juce::AudioProcessorParameter& param);

    void hostedInstanceGone() override;
    void hostedInstanceLive() override;
    void layoutChangedForPlugin(const synth::PluginIdentity& identity) override;

    ModuleComponent& card_;
    juce::WeakReference<synth::HostedPluginModule> module_;
    synth::PluginCardLayoutStore* store_;
    bool registered_ = false;
};
