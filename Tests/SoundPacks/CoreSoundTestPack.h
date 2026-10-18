#pragma once

// A tiny fake "core" sound pack in a temp folder: every instrument the song planner can name, each one region over a
// short WAV, with the sound pack library pointed at it for the life of the object.
#include "../Modules/Sampler/SamplerTestWavs.h"
#include "Modules/Sampler/SoundPackLibrary.h"

namespace core_sound_test {

class FakeCorePack {
public:
    /** `installed` false points the library at an empty folder, as on a machine that never downloaded the pack. */
    explicit FakeCorePack(bool installed = true) {
        if (installed)
            install();
        synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{root_.path()});
    }
    ~FakeCorePack() { synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt); }

    /** Writes the pack, as if the download had finished. */
    void install() {
        const auto pack = root_.file("core");
        sampler_test::writeWav(pack.getChildFile("x.wav"), 2000, 1, 44100.0,
                               [](int, int i) { return 0.01f * (float)(i % 10); });
        pack.getChildFile("x.sfz").replaceWithText("<region> sample=x.wav");
        juce::String instruments;
        for (const char* id : {"upright-piano", "string-section", "trumpet", "french-horn", "flute", "clarinet",
                               "double-bass-plucked", "electric-guitar", "acoustic-kit"})
            instruments += juce::String(instruments.isEmpty() ? "" : ",") + "{\"id\":\"" + id + "\",\"name\":\"" + id +
                           "\",\"sfz\":\"x.sfz\",\"category\":\"Test\"}";
        pack.getChildFile("pack.json")
            .replaceWithText("{\"id\":\"core\",\"name\":\"Core\",\"version\":\"1.0.0\",\"instruments\":[" +
                             instruments + "]}");
    }

    const juce::File& path() const { return root_.path(); }

private:
    sampler_test::TempDir root_;
};

} // namespace core_sound_test
