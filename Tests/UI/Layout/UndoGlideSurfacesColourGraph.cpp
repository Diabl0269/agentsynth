// UndoGlideSurfacesColourGraph.cpp -- the registry fixtures for the colours a macro shows on the graph canvas, each
// changed through its real commit path (one undo step) and undone: the open macro's border and name chip, the
// collapsed macro's card, a port's jack, and the card of a macro that a track's colour picker recolours together with
// the track. drawn() is the red channel (0..255) of the colour as drawn; the two colours differ in red by construction
// (docs/layout/undo-glide.md).
#include "../../Mixer/ChannelFlow/ChannelFlowTestRigs.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/KeyGlide/RestoredColourGlide.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TrackChannelLinkController.h"
#include "UndoGlideSurfaces.h"

namespace undo_glide_test {

namespace {

using synth::ui::RestoredColourGlide;

/** A colour whose red is far from `was`'s. */
juce::Colour farFrom(juce::Colour was) { return juce::Colour::fromRGB(was.getRed() < 128 ? 255 : 0, 90, 200); }

/** Every fixture lets glides run as if their owners were on screen for as long as it lives. */
struct OffScreenGlides {
    OffScreenGlides() { RestoredColourGlide::setAnimateOffScreenForTest(true); }
    ~OffScreenGlides() { RestoredColourGlide::setAnimateOffScreenForTest(false); }
};

/** The editor's colour glide, which the first card sync creates. */
synth::ui::MacroColourGlide& colours(GraphEditor& editor) {
    auto* glide = editor.getMacroController().getColourGlide();
    jassert(glide != nullptr);
    return *glide;
}

juce::AudioProcessorGraph::NodeID addModule(GraphEditor& editor, AudioEngine& engine,
                                            std::unique_ptr<juce::AudioProcessor> processor, int x) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", 100);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

/** A macro of two modules on a canvas with an undo manager, open or collapsed; the surfaces below recolour it. */
class MacroRig {
public:
    explicit MacroRig(bool collapsed) {
        editor_.setSize(1600, 1200);
        auto a = addModule(editor_, engine_, std::make_unique<OscillatorModule>(), 100);
        auto b = addModule(editor_, engine_, std::make_unique<FilterModule>(), 500);
        editor_.setSelectedNodes({a, b});
        macroId_ = editor_.getMacroController().groupSelectionIntoMacro();
        editor_.getMacroController().setMacroCollapsed(macroId_, collapsed);
        editor_.updateComponents();
    }
    GraphEditor& editor() const { return editor_; }
    AppUndoManager& undo() { return undo_; }
    const juce::String& macroId() const { return macroId_; }
    juce::Colour resting() const { return editor_.getMacros().find(macroId_)->colour; }

private:
    AudioEngine engine_;
    AppUndoManager undo_;
    mutable GraphEditor editor_{engine_, &undo_};
    juce::String macroId_;
};

/** A macro's colour from its colour picker's commit: the border and chip of the open macro, or the collapsed card. */
class MacroColourSurface : public Surface {
public:
    explicit MacroColourSurface(bool collapsed)
        : rig_(collapsed) {}

    void move() override {
        auto picker = rig_.editor().createMacroColourPickerForTest(rig_.macroId());
        picker->setCurrentColourForTest(farFrom(rig_.resting()));
        picker->commitForTest();
        colours(rig_.editor()).glide().stepForTest(1000.0);
    }
    bool undo() override { return rig_.undo().undo(); }
    bool redo() override { return rig_.undo().redo(); }
    double drawn() const override {
        return rig_.editor().getMacroController().drawnMacroColour(rig_.macroId(), rig_.resting()).getRed();
    }
    bool gliding() const override { return colours(rig_.editor()).glide().isGliding(); }
    void stepTo(double elapsedMs) override { colours(rig_.editor()).glide().stepForTest(elapsedMs); }
    juce::Component* paintedComponent() override { return &rig_.editor(); }

private:
    static synth::ui::MacroColourGlide& colours(GraphEditor& editor) { return undo_glide_test::colours(editor); }
    OffScreenGlides offScreen_;
    mutable MacroRig rig_;
};

/** A port's jack colour from the Configure I/O swatch's commit, on the collapsed macro's card. */
class MacroPortColourSurface : public Surface {
public:
    MacroPortColourSurface()
        : rig_(true) {
        port_ = rig_.editor().getMacroController().addMacroPort(rig_.macroId(), true, synth::MacroPortKind::AudioCV,
                                                                MacroPortShape::Mono, 1, "In");
        rig_.editor().updateComponents();
    }

    void move() override {
        rig_.editor().changeMacroPortColour(rig_.macroId(), port_, farFrom(resting()));
        colours(rig_.editor()).glide().stepForTest(1000.0);
    }
    bool undo() override { return rig_.undo().undo(); }
    bool redo() override { return rig_.undo().redo(); }
    double drawn() const override {
        return rig_.editor().getMacroController().drawnPortColour(rig_.macroId(), port_, resting()).getRed();
    }
    bool gliding() const override { return colours(rig_.editor()).glide().isGliding(); }
    void stepTo(double elapsedMs) override { colours(rig_.editor()).glide().stepForTest(elapsedMs); }
    juce::Component* paintedComponent() override { return &rig_.editor(); }

private:
    /** The colour the jack rests at: the port's own, else the kind tint. */
    juce::Colour resting() const {
        const auto* macro = rig_.editor().getMacros().find(rig_.macroId());
        for (const auto& port : macro->ports)
            if (port.nodeUuid == port_)
                return port.colour.value_or(synth::theme::themeOf(rig_.editor()).colors.accent);
        return {};
    }
    OffScreenGlides offScreen_;
    mutable MacroRig rig_;
    juce::String port_;
};

/** A track that owns its channel macro, recoloured by the track's colour picker: the track and the macro change in
 *  one undo step, and the macro's card eases in step with the track's header. */
class LinkedMacroColourSurface : public Surface {
public:
    LinkedMacroColourSurface() {
        constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
        auto& graph = patch_.engine.getGraph();
        juce::String inUuid, oscUuid, stripUuid;
        auto* trackIn = addPlainNodeCFT(graph, "Track In", {0, 0}, inUuid);
        auto* osc = addPlainNodeCFT(graph, "Oscillator", {200, 0}, oscUuid);
        auto* strip = addPlainNodeCFT(graph, "Channel Strip", {400, 0}, stripUuid);
        graph.addConnection({{trackIn->nodeID, midi}, {osc->nodeID, midi}});
        const int oscRight = dynamic_cast<ModuleBase*>(osc->getProcessor())->rightAudioLegChannel();
        graph.addConnection({{osc->nodeID, 0}, {strip->nodeID, 0}});
        graph.addConnection({{osc->nodeID, oscRight}, {strip->nodeID, ChannelStripModule::kRightBase}});
        graph.addConnection({{strip->nodeID, 0}, {patch_.output->nodeID, 0}});
        graph.addConnection({{strip->nodeID, ChannelStripModule::kRightBase}, {patch_.output->nodeID, 1}});
        track_ = doc_.addTrack(synth::TrackKind::Midi, "Lead");
        doc_.setTrackBinding(track_, inUuid);
        macroId_ = editor_.getMacroController().addMacroForMembers({inUuid, oscUuid, stripUuid}, "Lead", {0, 0});
        // What MainComponent runs after every undo and redo.
        undo_.setRestoreHooks({}, [this] { link_.reconcileLinkedTracks(); });
        editor_.setSize(1600, 1200);
        editor_.getMacroController().setMacroCollapsed(macroId_, true);
        editor_.updateComponents();
        link_.reconcileLinkedTracks(); // the macro wears its track's colour
    }

    void move() override {
        auto picker = link_.buildOwnedMacroColourPicker(track_, nullptr);
        picker->setCurrentColourForTest(farFrom(resting()));
        picker->commitForTest();
        colours(editor_).glide().stepForTest(1000.0);
    }
    bool undo() override { return undo_.undo(); }
    bool redo() override { return undo_.redo(); }
    double drawn() const override {
        return editor_.getMacroController().drawnMacroColour(macroId_, resting()).getRed();
    }
    bool gliding() const override { return colours(editor_).glide().isGliding(); }
    void stepTo(double elapsedMs) override { colours(editor_).glide().stepForTest(elapsedMs); }
    juce::Component* paintedComponent() override { return &editor_; }

private:
    juce::Colour resting() const { return editor_.getMacros().find(macroId_)->colour; }

    OffScreenGlides offScreen_;
    HostedPatchCFT patch_;
    AppUndoManager undo_;
    synth::TimelineDoc doc_;
    mutable GraphEditor editor_{patch_.engine, &undo_};
    synth::ui::TrackChannelLinkController link_{patch_.engine, doc_, undo_, editor_};
    synth::TrackId track_;
    juce::String macroId_;
};

} // namespace

void appendGraphColourEntries(std::vector<Entry>& list) {
    list.push_back({"Module card colour", false, [] { return std::make_unique<MacroColourSurface>(false); }});
    list.push_back({"Macro card colour", false, [] { return std::make_unique<MacroColourSurface>(true); }});
    list.push_back({"Macro port colour", false, [] { return std::make_unique<MacroPortColourSurface>(); }});
    list.push_back({"Linked macro colour", false, [] { return std::make_unique<LinkedMacroColourSurface>(); }});
}

} // namespace undo_glide_test
