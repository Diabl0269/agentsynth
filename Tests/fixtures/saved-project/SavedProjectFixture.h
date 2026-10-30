#pragma once

// SavedProjectFixture.h -- the shared "real saved project" for tests (header-only; not compiled on its own and not
// registered in Tests/CMakeLists.txt). A bug that only shows in a project a person saved and reopened (several
// tracks, macros open, folded and nested, modulators, a hosted plugin) is invisible on a fresh synthetic graph, so
// any test of code that looks up tracks, macros or cables can open this instead of building its own.
//
// The project is made by the app itself: a real MainComponent adds the tracks, groups and nests the macros and wires
// the LFO (modules are added to the graph directly, as the rest of the tests do), then ProjectBundle writes it, so the
// file can never drift from what the app saves. Opening it is the app's own
// openProjectForTest. What the project holds (names are the macro/track names the lookups below take):
//   - Audio 1, Audio 2, Audio 3: each in its own channel macro (Gate, EQ, Compressor, Channel Strip).
//   - Audio 1's macro is FOLDED and has an LFO on its Compressor threshold and an ADSR beside the LFO, both inside it.
//   - Audio 2's macro is NESTED: the macro "Nested parent" holds it (folded) and an Oscillator.
//   - Audio 3's macro is OPEN.
//   - "Plain group" is a folded macro no track plays: an Oscillator, a Filter and a Hosted Plugin stand-in
//     (Tests/StubPluginInstance.h's stub, the only kind of plugin this repo can load; open() installs the stub as the
//     default backend while it opens, so the plugin reloads instead of staying an unresolved placeholder).
// Use: `synth::test::SavedProjectFixture project; auto mc = project.open(); project.trackMacroId(*mc, 1)`.

#include "../../StubPluginInstance.h"
#include "../../UniqueTempDir.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>
#include <memory>

namespace synth::test {

/** An AI provider that answers nothing; MainComponent needs one to exist. */
class SavedProjectFixtureProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "FixtureProvider"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"FixtureModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model_ = name; }
    juce::String getCurrentModel() const override { return model_; }
    void setRequestTimeoutMs(int timeoutMs) override { timeoutMs_ = timeoutMs; }
    int getRequestTimeoutMs() const override { return timeoutMs_; }

private:
    juce::String model_ = "FixtureModel";
    int timeoutMs_ = 240000;
};

class SavedProjectFixture {
public:
    static constexpr const char* kNestedParent = "Nested parent";
    static constexpr const char* kPlainGroup = "Plain group";

    /** Builds the project in a throwaway MainComponent and saves it; the bundle is deleted with the fixture. */
    SavedProjectFixture() {
        bundle_ = synth::test::uniqueTempPath("saved-project-fixture", ".agsproj");
        MainComponent builder{std::make_unique<SavedProjectFixtureProvider>()};
        builder.setSize(1400, 900);
        builder.getAudioEngine().suspendDeviceCallback();
        builder.newPatchForTest();
        for (int i = 0; i < 3; ++i)
            builder.simulateAddAudioTrackClick();
        build(builder);
        saved_ = builder.saveProjectForTest(bundle_);
    }
    ~SavedProjectFixture() { bundle_.deleteRecursively(); }
    SavedProjectFixture(const SavedProjectFixture&) = delete;
    SavedProjectFixture& operator=(const SavedProjectFixture&) = delete;

    bool wasSaved() const { return saved_; }
    const juce::File& bundle() const { return bundle_; }

    /** A fresh MainComponent with the project opened the way the app opens a file. Null when the open failed. */
    std::unique_ptr<MainComponent> open() const {
        auto mc = std::make_unique<MainComponent>(std::make_unique<SavedProjectFixtureProvider>());
        mc->setSize(1400, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->getTimelinePanel().setSize(1400, 360);
        HostedPluginBackend::ScopedDefault stub(&backend());
        if (!mc->openProjectForTest(bundle_))
            return nullptr;
        const auto deadline = juce::Time::getMillisecondCounter() + 3000;
        while (juce::Time::getMillisecondCounter() < deadline && !pluginsLoaded(*mc))
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        return mc;
    }

    /** The macro the 0-based `trackIndex`'s bound node sits in directly (its channel macro). */
    static juce::String trackMacroId(MainComponent& mc, int trackIndex) {
        const auto uuid = mc.getTimelineDoc().getTracks()[(size_t)trackIndex].bindingUuid;
        const auto* macro = mc.getGraphEditor().getMacros().findByMember(uuid);
        return macro != nullptr ? macro->id : juce::String();
    }

    /** The id of the macro called `name`, or empty. */
    static juce::String macroIdNamed(MainComponent& mc, const juce::String& name) {
        for (const auto& macro : mc.getGraphEditor().getMacros().getAll())
            if (macro.name == name)
                return macro.id;
        return {};
    }

    /** The uuid of the first member (nested ones too) of `macroId` whose name starts with `type`, so "Compressor"
     * finds "Compressor 3". */
    static juce::String memberUuidOfType(MainComponent& mc, const juce::String& macroId, const juce::String& type) {
        const auto* macro = mc.getGraphEditor().getMacros().find(macroId);
        if (macro == nullptr)
            return {};
        const auto members = mc.getGraphEditor().getMacros().descendantMembers(macroId);
        for (auto* node : mc.getAudioEngine().getGraph().getNodes()) {
            const auto uuid = synth::AIStateMapper::ensureNodeUuid(node);
            if (members.count(uuid) != 0 && node->getProcessor()->getName().startsWith(type))
                return uuid;
        }
        return {};
    }

private:
    static StubBackend& backend() {
        static StubBackend stub;
        return stub;
    }
    static bool pluginsLoaded(MainComponent& mc) {
        for (auto* node : mc.getAudioEngine().getGraph().getNodes())
            if (auto* plugin = dynamic_cast<HostedPluginModule*>(node->getProcessor());
                plugin && !plugin->hasInstance())
                return false;
        return true;
    }
    static juce::AudioProcessorGraph::Node::Ptr addNode(MainComponent& mc, std::unique_ptr<juce::AudioProcessor> proc,
                                                        int x, int y) {
        auto node = mc.getAudioEngine().getGraph().addNode(std::move(proc));
        const auto uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        node->properties.set("x", x);
        node->properties.set("y", y);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);
        return node;
    }

    static void build(MainComponent& mc) {
        auto& editor = mc.getGraphEditor();
        auto& macros = editor.getMacros();
        const auto macroOf = [&](int track) { return trackMacroId(mc, track); };
        const auto first = macroOf(0), second = macroOf(1), third = macroOf(2);

        // Audio 1: an LFO on the Compressor threshold and an ADSR, both pulled into the track's macro.
        auto lfo = addNode(mc, synth::AIStateMapper::createModule("LFO"), 40, 40);
        auto adsr = addNode(mc, synth::AIStateMapper::createModule("ADSR"), 40, 240);
        editor.updateComponents();
        const auto compressor = memberUuidOfType(mc, first, "Compressor");
        for (auto* node : mc.getAudioEngine().getGraph().getNodes())
            if (node->properties["uuid"].toString() == compressor) {
                const int raw = editor.modulationChannelFor(node->nodeID, "threshold");
                editor.connectModulationSource(lfo->nodeID, 0, node->nodeID, raw, 0.5f);
            }
        macros.addMember(first, lfo->properties["uuid"].toString());
        macros.addMember(first, adsr->properties["uuid"].toString());

        // "Plain group": no track plays it.
        auto osc = addNode(mc, std::make_unique<OscillatorModule>(), 100, 700);
        auto filter = addNode(mc, std::make_unique<FilterModule>(), 500, 700);
        auto plugin = std::make_unique<HostedPluginModule>();
        auto* pluginRaw = plugin.get();
        auto pluginNode = addNode(mc, std::move(plugin), 900, 700);
        juce::PluginDescription description;
        description.name = "Fixture Plugin";
        description.pluginFormatName = "VST3";
        description.uniqueId = description.deprecatedUid = 0xF1D001;
        description.fileOrIdentifier = "/nonexistent/test/path/FixturePlugin.vst3";
        pluginRaw->loadPlugin(description, backend());
        // The stub answers through the message loop; let it, so no load is still in flight when the builder goes.
        const auto deadline = juce::Time::getMillisecondCounter() + 3000;
        while (!pluginRaw->hasInstance() && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        editor.updateComponents();
        editor.setSelectedNodes({osc->nodeID, filter->nodeID, pluginNode->nodeID});
        const auto plain = editor.getMacroController().groupSelectionIntoMacro();
        macros.find(plain)->name = kPlainGroup;

        // "Nested parent": Audio 2's macro and an Oscillator under one parent.
        auto nestedOsc = addNode(mc, std::make_unique<OscillatorModule>(), 100, 1000);
        editor.updateComponents();
        editor.getMacroController().selectMacro(second, false);
        auto selected = editor.getSelectedNodes();
        selected.push_back(nestedOsc->nodeID);
        editor.setSelectedNodes(selected);
        const auto parent = editor.getMacroController().groupSelectionIntoMacro();
        macros.find(parent)->name = kNestedParent;

        editor.getMacroController().setMacroCollapsed(first, true);
        editor.getMacroController().setMacroCollapsed(second, true);
        editor.getMacroController().setMacroCollapsed(plain, true);
        editor.getMacroController().setMacroCollapsed(third, false);
        editor.clearSelection();
    }

    juce::File bundle_;
    bool saved_ = false;
};

} // namespace synth::test
