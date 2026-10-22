#pragma once

// Builds small sound trees in a temp folder for the sound index and Sounds view tests.

#include "SoundBrowser/SoundIndex.h"
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace sound_browser_test {

// Writes a mono 16-bit WAV of `seconds` at 8 kHz; `meta` becomes WAV chunks (acid keys write an acid chunk).
inline void writeWav(const juce::File& file, double seconds = 0.5, const juce::StringPairArray& meta = {}) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    auto* stream = new juce::FileOutputStream(file);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream, 8000.0, 1, 16, meta, 0));
    const int n = (int)(seconds * 8000.0);
    juce::AudioBuffer<float> buffer(1, n);
    buffer.clear();
    for (int i = 0; i < n; ++i)
        buffer.setSample(0, i, 0.25f * std::sin(0.05f * (float)i));
    writer->writeFromAudioSampleBuffer(buffer, 0, n);
}

inline void writePack(const juce::File& root, const juce::String& id, const juce::String& name) {
    root.getChildFile(id).createDirectory();
    root.getChildFile(id)
        .getChildFile("pack.json")
        .replaceWithText("{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"instruments\":[]}");
}

struct TempTree {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("sound-browser-" + juce::Uuid().toDashedString());
    juce::File packs = dir.getChildFile("packs");
    juce::File user = dir.getChildFile("user");
    TempTree() { dir.createDirectory(); }
    ~TempTree() { dir.deleteRecursively(); }
    juce::File cache() const { return dir.getChildFile("SoundBrowser/index.json"); }

    // packs/core: Drums/Kicks/kick-808.wav, Drums/snare_tight.wav, Piano/upright.sfz;  user: loop_120_Am.wav
    void fill() {
        writePack(packs, "core", "Core instruments");
        writeWav(packs.getChildFile("core/Drums/Kicks/kick-808.wav"), 1.2);
        writeWav(packs.getChildFile("core/Drums/snare_tight.wav"), 0.3);
        packs.getChildFile("core/Piano").createDirectory();
        packs.getChildFile("core/Piano/upright.sfz").replaceWithText("<region> sample=a.wav");
        writeWav(user.getChildFile("Field/dusty_keys_120_Am_loop.wav"), 2.0);
    }
};

inline std::unique_ptr<synth::soundbrowser::SoundIndex> makeIndex(const TempTree& tree, bool withCache = false) {
    auto index = std::make_unique<synth::soundbrowser::SoundIndex>(
        withCache ? tree.cache() : juce::File(), [&tree] { return std::vector<juce::File>{tree.packs}; });
    return index;
}

inline void scanAndWait(synth::soundbrowser::SoundIndex& index) {
    index.rescan();
    ASSERT_TRUE(index.waitUntilIdleForTests(20000));
}

} // namespace sound_browser_test
