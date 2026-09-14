#pragma once

// Shared test helpers for SnippetManager*Tests.cpp: graph-building shorthands, parameter
// normalise/denormalise helpers, and a minimal valid WAV writer for Sampler-holding tests.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "MacroSet.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include "Modules/VCAModule.h"
#include "SnippetManager.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;
using synth::Macro;
using synth::MacroPort;
using synth::MacroPortKind;
using synth::MacroSet;
using synth::SnippetManager;

static juce::AudioProcessorGraph::Node::Ptr addAt(juce::AudioProcessorGraph& graph,
                                                  std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
    auto node = graph.addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    return node;
}

static const juce::Array<juce::var>* arrayOf(const juce::var& v, const char* key) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty(key))
        return nullptr;
    return obj->getProperty(key).getArray();
}

static int countOfType(juce::AudioProcessorGraph& graph, const juce::String& name) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            ++count;
    return count;
}

static juce::RangedAudioParameter* findParam(juce::AudioProcessor* processor, const juce::String& paramId) {
    for (auto* param : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (ranged->paramID == paramId)
                return ranged;
    return nullptr;
}

static void setDenormalised(juce::AudioProcessor* processor, const juce::String& paramId, float value) {
    auto* param = findParam(processor, paramId);
    ASSERT_NE(param, nullptr);
    param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(value));
}

/** A minimal valid WAV, so a Sampler has something real to have loaded. */
static bool writeSilentWav(const juce::File& file, int numFrames) {
    file.deleteFile();
    juce::AudioBuffer<float> buffer(1, numFrames);
    buffer.clear();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), 44100.0, 1, 32, {}, 0));
    if (writer == nullptr)
        return false;

    stream.release(); // the writer owns it now
    writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
    writer.reset(); // flush
    return file.existsAsFile();
}

static float getDenormalised(juce::AudioProcessor* processor, const juce::String& paramId) {
    auto* param = findParam(processor, paramId);
    if (param == nullptr)
        return std::numeric_limits<float>::quiet_NaN();
    return param->getNormalisableRange().convertFrom0to1(param->getValue());
}
