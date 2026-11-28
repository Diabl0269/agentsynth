#pragma once

// A real LFO cabled to a VCA's Level knob, the graph processed offline, with the themed look counting knob paints.
// Header-only; not compiled on its own.

#include "../CardBody/CardBodyTestHelpers.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/LFOModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

// The themed look, counting how many times a knob was painted.
struct CountingLnf : synth::theme::AppLookAndFeel {
    int knobPaints = 0;
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider& s) override {
        ++knobPaints;
        AppLookAndFeel::drawRotarySlider(g, x, y, w, h, pos, start, end, s);
    }
};

// A real LFO cabled to a VCA's Level knob, the graph processed offline.
struct KnobRing {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    CountingLnf lnf;
    ModuleComponent* card = nullptr;
    juce::Slider* knob = nullptr;
    synth::ui::ZoomFrozenCachedImage* cache = nullptr;
    juce::AudioProcessorGraph::NodeID lfoId, vcaId;
    int channel = -1;

    KnobRing() {
        engine.setAudioDeviceDisabled(true);
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        auto& graph = engine.getGraph();
        auto lfo = std::make_unique<LFOModule>();
        if (auto* mode = findParameterByID(lfo.get(), "mode"))
            mode->setValueNotifyingHost(0.0f);
        if (auto* rate = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(lfo.get(), "rateHz")))
            rate->setValueNotifyingHost(rate->convertTo0to1(1.0f));
        lfoId = graph.addNode(std::move(lfo))->nodeID;
        auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
        vcaId = vcaNode->nodeID;
        channel = dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->modulationChannelForParam("gain");
        synth::setCardLayoutOverride(
            graph, nullptr, vcaId,
            cardbody_test::automaticLayoutWith(*vcaNode->getProcessor(), {{"gain", synth::CardWidget::Knob}}));
        editor->updateComponents();
        editor->connectModulationSource(lfoId, 0, vcaId, channel, 1.0f);
        editor->updateComponents();
        editor->timerCallback();
        card = findModuleComp(*editor, vcaNode->getProcessor());
        if (card != nullptr) {
            card->setLookAndFeel(&lnf);
            knob = dynamic_cast<juce::Slider*>(card->getCardBody()->findWidget("gain"));
            cache = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(card->getCachedComponentImage());
        }
        graph.setPlayConfigDetails(0, 2, kRate, kBlock);
        graph.prepareToPlay(kRate, kBlock);
    }
    ~KnobRing() {
        if (card != nullptr)
            card->setLookAndFeel(nullptr);
        editor.reset();
        engine.shutdown();
    }

    // A second LFO on the same knob, and both LFOs held still (muted: a steady 0).
    void addSecondLfoAndMuteBoth() {
        auto& graph = engine.getGraph();
        const auto second = graph.addNode(std::make_unique<LFOModule>())->nodeID;
        editor->updateComponents();
        editor->connectModulationSource(second, 0, vcaId, channel, 1.0f);
        editor->updateComponents();
        for (const auto id : {lfoId, second})
            dynamic_cast<ModuleBase*>(graph.getNodeForId(id)->getProcessor())->setMuted(true);
        card = findModuleComp(*editor, graph.getNodeForId(vcaId)->getProcessor());
    }

    // Runs the graph `blocks` blocks, then lets the card see the new reading.
    void process(int blocks) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        for (int i = 0; i < blocks; ++i) {
            buffer.clear();
            engine.getGraph().processBlock(buffer, midi);
        }
        editor->timerCallback();
        card->timerCallback();
    }

    void paintCache() {
        juce::Image target(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(target);
        cache->paint(g);
    }

    juce::Image paintCard() {
        juce::Image image(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        card->paintEntireComponent(g, true);
        return image;
    }
};

} // namespace
