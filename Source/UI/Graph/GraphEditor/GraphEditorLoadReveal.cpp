// GraphEditorLoadReveal.cpp -- the project-open reveal (ProjectLoad/LoadRevealAnimator.h): the canvas it is lent. The
// paint passes ask it how much of each cable and border to draw (GraphEditorCables.cpp). GraphEditor is declared in
// GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ProjectLoad/LoadRevealAnimator.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

// Built on first use: most editors (every headless test) never open a project on screen. Only visible cards take
// part; a module hidden inside a collapsed macro is part of that macro's card.
LoadRevealAnimator& GraphEditor::getLoadReveal() {
    if (loadReveal_ != nullptr)
        return *loadReveal_;
    loadReveal_ = std::make_unique<LoadRevealAnimator>();
    LoadRevealAnimator::Hooks hooks;
    hooks.cards = [this] {
        std::vector<LoadRevealAnimator::Card> out;
        for (auto* m : content.getModules())
            if (m != nullptr && m->isVisible())
                out.push_back({m, m->getNodeId().uid, {}});
        for (auto* card : content.getMacroCards())
            if (card != nullptr && card->isVisible())
                out.push_back({card, 0, card->getMacroId()});
        return out;
    };
    hooks.graph = [this]() -> juce::AudioProcessorGraph& { return audioEngine.getGraph(); };
    hooks.macros = [this]() -> const synth::MacroSet& { return macros; };
    hooks.isOutputNode = [this](uint32_t uid) { return isOutputDockNode(juce::AudioProcessorGraph::NodeID(uid)); };
    hooks.cables = [this]() -> const std::vector<VisibleCable>& { return buildVisibleCables(); };
    hooks.hullBounds = [this](const juce::String& id) { return paintedMacroHullBounds(id); };
    // A frame repaints only what it changed and keeps the cable memo (docs/layout/rendering.md).
    hooks.repaintArea = [this](juce::Rectangle<int> area) { content.repaint(area); };
    hooks.repaintAll = [this] { content.repaint(); };
    hooks.setCanvasAlpha = [this](float alpha) { content.setAlpha(alpha); };
    hooks.rasterScale = [this] {
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
        return zoomLevel * (display != nullptr ? static_cast<float>(display->scale) : 1.0f);
    };
    hooks.outlineColour = [this] { return synth::theme::themeOf(*this).colors.textPrimary; };
    hooks.reportStatus = [this](const juce::String& message) { reportStatusMessage(message); };
    hooks.updater = &vblankUpdater;
    loadReveal_->setHooks(std::move(hooks));
    return *loadReveal_;
}
