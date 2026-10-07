// GraphEditor.cpp
//
// GraphEditor's constructor/destructor and GraphContentComponent's constructor -- the class's
// core lifecycle. GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in
// this directory hold the rest of the class (cables, connections, smart connections, canvas,
// selection, macros, channels, commands, drag/drop, stereo wiring, persistence).

#include "GraphEditor.h"
#include "AudioEngine/AudioEngine.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/ModDot/ModDotController.h"
// ~GraphEditor()/~GraphContentComponent() are defined here, so the OwnedArrays of these types
// (declared in GraphEditor.h with only a forward declaration) need their full definitions
// wherever the implicit member destructors are instantiated.
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

GraphEditor::GraphEditor(AudioEngine& engine, AppUndoManager* undoMgr)
    : audioEngine(engine)
    , content(*this)
    , modMatrix(engine, undoMgr, this)
    , undoManager(undoMgr)
    , cardKeyboard_(std::make_unique<CanvasCardKeyboard>(*this, undoMgr)) {
    addAndMakeVisible(content);
    addChildComponent(modMatrix); // closed until toggled (isMatrixVisible); a closed matrix does no work
    content.setInterceptsMouseClicks(false, true); // Fallback clicks to parent
    macroController_.setPaintedHullProvider([this](const juce::String& id) { return paintedMacroHullBounds(id); });

    // Minimap (issue #159): visibility is driven by setMinimapVisible(), called by the owner once
    // it has restored the persisted preference — NOT addAndMakeVisible, which would show it before
    // that preference is known.
    addChildComponent(minimap);
    minimapSlide_.snapTo(minimapVisible ? 1.0f : 0.0f);
    minimap.setVisible(minimapVisible);
    minimap.onNavigate = [this](juce::Point<float> p) { centreViewOn(p); };
    minimap.onZoom = [this](float d) { zoomAroundCentre(d); };

    // Tooltips on GraphEditor-owned affordances.
    // The canvas itself hints at pan/zoom. Double-click on an attenuverter knob removes it.
    setTooltip(synth::ui::formatShortcutHint(
        "Patch canvas - drag modules here to build your patch",
        "Swipe or scroll to pan | Pinch or " + platformCommandKeyName() +
            "+scroll to zoom | Drag to pan | Shift+drag to select | Double-click mod knob to remove"));

    // Needed for the canvas-scoped Delete/Escape keys (see keyPressed).
    setWantsKeyboardFocus(true);

    modDot_ = std::make_unique<synth::ui::ModDotController>(*this, content);
    configureCardGlide();
    configureCanvasFrame();
    startTimerHz(30);
}

GraphEditor::~GraphEditor() {
    stopTimer();
    modDot_.reset(); // before `content`, which its tooltip animator is attached to
}

// Lends the glide animator the canvas' cards, the snapshot scale (zoom x display scale, so a glide stays sharp), a
// repaint that also drops the cable memo, a partial one that keeps it, and the VBlank updater that drives it.
void GraphEditor::configureCardGlide() {
    CardGlideAnimator::Hooks hooks;
    hooks.cards = [this] {
        std::vector<CardGlideAnimator::Entry> out;
        for (auto* m : content.getModules())
            if (m != nullptr)
                out.push_back({m, m->getNodeId().uid});
        for (auto* card : content.getMacroCards())
            if (card != nullptr)
                out.push_back({card, 0});
        return out;
    };
    hooks.snapshotScale = [this] {
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
        return zoomLevel * (display != nullptr ? static_cast<float>(display->scale) : 1.0f);
    };
    hooks.repaint = [this] { repaintCanvas(); };
    // A glide frame repaints only what it moves and moves the cables in the memo instead of rebuilding it
    // (CardGlideAnimator::requestFrameRepaint); a memo already dropped means a full repaint is pending anyway.
    hooks.repaintArea = [this](juce::Rectangle<int> area) { content.repaint(area); };
    hooks.liveCables = [this]() -> std::vector<VisibleCable>* { return cablesCacheValid ? &cablesCache : nullptr; };
    hooks.updater = &vblankUpdater;
    hooks.canAnimate = [this] { return isShowing(); };
    hooks.accent = [this] { return synth::theme::themeOf(*this).colors.accent; };
    cardGlide_.setHooks(std::move(hooks));
}

GraphEditor::GraphContentComponent::GraphContentComponent(GraphEditor& ed)
    : editor(ed) {
    // The canvas fills its whole bounds opaquely (bg1 + grid), so tell JUCE not to repaint
    // whatever is behind it on every frame — a real win for zoom/pan and the 30Hz wire animation.
    setOpaque(true);
}

// ---- GraphCanvasHost overrides (the one-line ones -- accessors of owned members -- stay inline in GraphEditor.h) ----
// Out-of-line only because it needs ModuleComponent's full definition (getNodeId()), which
// GraphEditor.h deliberately keeps forward-declared.
juce::AudioProcessorGraph& GraphEditor::graph() { return audioEngine.getGraph(); }

ModuleComponent* GraphEditor::moduleComponentFor(juce::AudioProcessorGraph::NodeID nodeId) {
    for (auto* c : content.getModules())
        if (c != nullptr && c->getNodeId() == nodeId)
            return c;
    return nullptr;
}

// A learn can be armed while its target card is scrolled out of view or the module has since been
// deleted (MainComponent's arm/cancel flow doesn't know either), so this is a best-effort push, not
// a hard dependency -- moduleComponentFor() returning nullptr is the normal "nothing to repaint" case,
// not an error.
void GraphEditor::setMidiLearnArmed(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId) {
    if (auto* module = moduleComponentFor(nodeId))
        module->setMidiLearnArmedParam(paramId);
}

// A ModuleComponent paints from a cached image that only its own repaint() invalidates, so a repaint of
// the canvas would leave every badge as it was; each card is asked directly.
void GraphEditor::repaintMidiLearnBadges() {
    for (auto* c : content.getModules())
        if (c != nullptr)
            c->repaint();
}

void GraphEditor::clearMidiLearnArmed() {
    for (auto* c : content.getModules())
        if (c != nullptr)
            c->setMidiLearnArmedParam({});
}

void GraphEditor::reportStatusMessage(const juce::String& message) {
    if (onStatusMessage)
        onStatusMessage(message);
}

juce::Point<int> GraphEditor::canvasPositionOfLocalPoint(juce::Point<int> pointOnHost) const {
    return content.getLocalPoint(this, pointOnHost).roundToInt();
}

juce::Point<int> GraphEditor::estimateModuleSizeForType(const juce::String& typeName) const {
    return estimateModuleSize(typeName);
}

juce::var GraphEditor::resolveSnippetPayload(const juce::String& name) const {
    return snippetProvider ? snippetProvider(name) : juce::var();
}
