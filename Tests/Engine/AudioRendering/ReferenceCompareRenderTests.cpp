// Black-box A/B render harness: the reference synth (loaded through our own hosting layer) against AgentSynth's
// Oscillator. Gated on AGENTSYNTH_REFERENCE_COMPARE_DIR (output folder) and AGENTSYNTH_REFERENCE_PLUGIN (path of the
// reference synth's VST3) so CI, which has neither, skips it. Comparison tooling, not a regression test.

#include "ReferenceCompareRig.h"

using namespace referencecompare;

TEST(ReferenceCompareRender, DumpReferenceParams) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    dir.createDirectory();
    synth::DefaultHostedPluginBackend backend;
    auto module = loadHosted(referencePath(), 48000.0, 256, backend);
    ASSERT_NE(module, nullptr) << "The reference synth did not load";
    auto* inst = module->getActiveInstanceForEditor();
    juce::String out;
    out << "# Reference synth: " << inst->getName() << " ins=" << inst->getTotalNumInputChannels()
        << " outs=" << inst->getTotalNumOutputChannels() << "\n";
    out << "# index | name | default(norm) | text at current(default) | texts at norm 0/.25/.5/.75/1\n";
    for (auto* p : inst->getParameters()) {
        out << p->getParameterIndex() << " | " << p->getName(100) << " | " << p->getDefaultValue() << " | "
            << p->getText(p->getValue(), 100) << " (cur " << p->getValue() << ") | ";
        for (float v : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            out << p->getText(v, 100) << " ; ";
        out << "\n";
    }
    if (auto* type = [&]() -> juce::AudioProcessorParameter* {
            for (auto* p : inst->getParameters())
                if (p->getName(100) == "Fil Type")
                    return p;
            return nullptr;
        }()) {
        out << "# Fil Type value list (step i of 95 -> text)\n";
        for (int i = 0; i < 96; ++i)
            out << i << " = " << type->getText((float)i / 95.0f, 100) << "\n";
    }
    EXPECT_TRUE(dir.getChildFile("reference_params.txt").replaceWithText(out));
}

TEST(ReferenceCompareRender, RenderAB) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    dir.createDirectory();
    synth::DefaultHostedPluginBackend backend;
    const int total = samples(3.5);

    // Which wavetable positions are a sine and a saw in the init patch's wavetable: probed by tone ratios at A4.
    float sineNorm = 0.0f, sawNorm = 0.0f; // only for the probe table
    {
        ReferenceRig probe(backend);
        ASSERT_TRUE(probe.ok());
        juce::String table = "# frame | norm | H2/H1 | H3/H1 | H1\n";
        double bestSine = 1.0e9, bestSaw = 1.0e9;
        int sineFrame = 0, sawFrame = 0;
        for (int frame = 1; frame <= 256; ++frame) {
            const float norm = (float)(frame - 1) / 255.0f;
            probe.setNorm("A WTPos", norm);
            probe.setNorm("A RandPhase", 0.0f);
            probe.setNorm("A Phase", 0.0f);
            const auto b = probe.render({{0, samples(0.5), 69}}, samples(0.6), frame == 1);
            const int from = samples(0.2), to = samples(0.5);
            const double h1 = toneLevel(b, from, to, 440.0), h2 = toneLevel(b, from, to, 880.0),
                         h3 = toneLevel(b, from, to, 1320.0);
            table << frame << " | " << norm << " | " << h2 / std::max(h1, 1e-9) << " | " << h3 / std::max(h1, 1e-9)
                  << " | " << h1 << "\n";
            if (h1 < 1.0e-6)
                continue;
            const double sineScore = (h2 + h3) / h1;
            const double sawScore = std::abs(h2 / h1 - 0.5) + std::abs(h3 / h1 - 1.0 / 3.0);
            if (sineScore < bestSine) {
                bestSine = sineScore;
                sineNorm = norm;
                sineFrame = frame;
            }
            if (sawScore < bestSaw) {
                bestSaw = sawScore;
                sawNorm = norm;
                sawFrame = frame;
            }
        }
        table << "# sine frame " << sineFrame << " (norm " << sineNorm << "), saw frame " << sawFrame << " (norm "
              << sawNorm << ")\n";
        dir.getChildFile("reference_wtpos_probe.txt").replaceWithText(table);
    }

    std::vector<Case> cases;
    auto add = [&](const juce::String& name, std::vector<NoteSpan> notes, int len, AgentSetup agent,
                   std::function<void(ReferenceRig&)> reference, bool filter = false) {
        cases.push_back({name, std::move(notes), len, std::move(agent), std::move(reference), filter});
    };
    auto sawA = [] {
        AgentSetup a;
        a.osc["waveform"] = 2;
        return a;
    };
    auto sineA = [] {
        AgentSetup a;
        a.osc["waveform"] = 0;
        return a;
    };
    // The init patch's wavetable has a single frame and it is a saw (see reference_wtpos_probe.txt), so the saw cases
    // leave the position alone. A sine needs a different wavetable, which no exposed parameter selects.
    auto referenceSaw = [](ReferenceRig&) {};
    auto referenceSine = [](ReferenceRig&) {};
    const juce::String noSine = "The reference synth's init wavetable is a single-frame saw and the wavetable choice "
                                "is not an exposed parameter";
    juce::ignoreUnused(referenceSine);
    auto referenceUnison = [=](ReferenceRig& r, int n) {
        referenceSaw(r);
        r.setNumeric("A Unison", n);
    };

    add("saw_c3", single(48), total, sawA(), referenceSaw);
    add("saw_a4", single(69), total, sawA(), referenceSaw);
    add("saw_c7", single(96), total, sawA(), referenceSaw);
    add("sine_a4", single(69), total, sineA(), referenceSine);
    cases.back().referenceSkipReason = noSine;
    {
        auto a5 = sawA();
        a5.osc["unison"] = 5;
        add("uni5_default_chord", chord(), total, a5, [=](ReferenceRig& r) { referenceUnison(r, 5); });
        auto a7 = sawA();
        a7.osc["unison"] = 7;
        add("uni7_default_chord", chord(), total, a7, [=](ReferenceRig& r) { referenceUnison(r, 7); });
        // The reference synth's default Detune (norm 0.5) spreads its outer voices +-35.4 cents (measured per voice by
        // RenderUnisonDetuneMap), and AgentSynth's Detune is those outer cents, so the same spread is 35.4 here.
        auto m = a7;
        m.osc["detune"] = kReferenceDefaultDetuneCents;
        m.osc["blend"] = 0.75;
        add("uni7_matched_chord", chord(), total, m, [=](ReferenceRig& r) { referenceUnison(r, 7); });
        auto mw = m;
        mw.osc["width"] = 1.0;
        add("uni7_matched_width_chord", chord(), total, mw, [=](ReferenceRig& r) { referenceUnison(r, 7); });
    }
    add("uni1_chord", chord(), total, sawA(), [=](ReferenceRig& r) { referenceUnison(r, 1); });

    // Env cases: four 200 ms notes 300 ms apart, phase pinned on both synths.
    std::vector<NoteSpan> four;
    for (int i = 0; i < 4; ++i)
        four.push_back({samples(0.5 + 0.3 * i), samples(0.7 + 0.3 * i), 69});
    const int envLen = samples(2.5);
    auto envAgent = [&](AgentSetup a, double phase) {
        a.osc["phase"] = phase;
        a.osc["randomPhase"] = 0.0;
        a.adsr = {{"attack", 0.001}, {"decay", 0.0}, {"sustain", 1.0}, {"release", 0.015}};
        return a;
    };
    auto envReference = [=](float wtNorm, double phase) {
        return [=](ReferenceRig& r) {
            r.setNumeric("A RandPhase", 0.0);
            r.setNumeric("A Phase", phase);
            // Env time is 32 s * x^5: 1 ms attack, 0 decay, 0 dB sustain, release left at its 15 ms default.
            r.setNorm("Env1 Atk", std::pow(0.001f / 32.0f, 0.2f), "time = 32 s * x^5, 1 ms");
            r.setNorm("Env1 Dec", 0.0f);
            r.setNorm("Env1 Sus", 1.0f, "0 dB (100%)");
        };
    };
    add("env_a1_r15_sine", four, envLen, envAgent(sineA(), 0.0), envReference(0, 0.0));
    cases.back().referenceSkipReason = noSine;
    add("env_a1_r15_sine_p270", four, envLen, envAgent(sineA(), 270.0), envReference(0, 270.0));
    cases.back().referenceSkipReason = noSine;
    add("env_a1_r15_saw", four, envLen, envAgent(sawA(), 0.0), envReference(0, 0.0));

    for (int res : {0, 50, 90}) {
        AgentSetup a = sawA();
        a.useFilter = true;
        a.filter = {{"filterType", 0}, {"cutoff", 1000.0}, {"resonance", res / 100.0}};
        add("filter_lp24_r" + juce::String(res), single(36), total, a, [=](ReferenceRig& r) {
            referenceSaw(r);
            r.setNorm("Filter On", 1.0f);
            r.setText("Fil Type", "MG Low 24");
            r.setNumeric("Fil Cutoff", 1000.0);
            r.setNumeric("Fil Reso", res);
        });
    }
    add("noise_floor", {}, samples(3.0), sawA(), [](ReferenceRig&) {});

    auto* settingsAll = new juce::DynamicObject();
    for (const auto& c : cases) {
        juce::var entry;
        ASSERT_TRUE(renderCase(dir, backend, c, entry)) << "The reference synth failed to load for " << c.name;
        settingsAll->setProperty(c.name, entry);
    }
    auto* top = new juce::DynamicObject();
    top->setProperty("sample_rate", kSr);
    top->setProperty("block_size", kBlock);
    top->setProperty("midi_velocity", 127);
    top->setProperty("cases", juce::var(settingsAll));
    EXPECT_TRUE(dir.getChildFile("settings.json").replaceWithText(juce::JSON::toString(juce::var(top))));
}

// Extra renders: the reference synth sine from the Sub oscillator, and the alias sweep. Merges into the existing
// settings.json.
TEST(ReferenceCompareRender, RenderEnvSweep) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    synth::DefaultHostedPluginBackend backend;
    std::vector<NoteSpan> sweep;
    for (int i = 0; i < 16; ++i)
        sweep.push_back({samples(0.5 + 0.3 * i), samples(0.7 + 0.3 * i), 60 + i});
    // The 1 ms attack is the case compared everywhere; the longer attacks show how much of the onset's broadband
    // content belongs to the attack ramp.
    for (const double attack : {0.001, 0.002, 0.004}) {
        AgentSetup env;
        env.osc["waveform"] = 0;
        env.osc["phase"] = 0.0;
        env.osc["randomPhase"] = 0.0;
        env.adsr = {{"attack", attack}, {"decay", 0.0}, {"sustain", 1.0}, {"release", 0.015}};
        const juce::String name =
            attack == 0.001 ? "env_sweep_sine" : "env_sweep_sine_a" + juce::String(juce::roundToInt(attack * 1000.0));
        Case c{name,
               sweep,
               samples(5.6),
               env,
               [=](ReferenceRig& r) {
                   r.setNorm("Osc A On", 0.0f);
                   r.setNorm("Osc S On", 1.0f);
                   r.setText("SubOscShape", "Sine");
                   r.setNumeric("SubOscOctave", 0.0);
                   r.setNumeric("Sub Osc Level", 100.0);
                   r.setNorm("Env1 Atk", std::pow(static_cast<float>(attack) / 32.0f, 0.2f), "time = 32 s * x^5");
                   r.setNorm("Env1 Dec", 0.0f);
                   r.setNorm("Env1 Sus", 1.0f, "0 dB (100%)");
               },
               false,
               {}};
        juce::var entry;
        ASSERT_TRUE(renderCase(dir, backend, c, entry));
    }
}

TEST(ReferenceCompareRender, RenderExtra) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    synth::DefaultHostedPluginBackend backend;
    const auto settingsFile = dir.getChildFile("settings.json");
    juce::var top = juce::JSON::parse(settingsFile);
    ASSERT_TRUE(top.isObject()) << "run RenderAB first";
    auto* casesObj = top.getProperty("cases", {}).getDynamicObject();
    ASSERT_NE(casesObj, nullptr);

    std::vector<Case> cases;
    // Reference synth sine from the Sub oscillator (shape Sine, octave 0, level 100) with Osc A off. The Sub has no
    // phase control.
    auto referenceSub = [](ReferenceRig& r) {
        r.setNorm("Osc A On", 0.0f);
        r.setNorm("Osc S On", 1.0f);
        r.setText("SubOscShape", "Sine");
        r.setNumeric("SubOscOctave", 0.0);
        r.setNumeric("Sub Osc Level", 100.0);
    };
    {
        AgentSetup sine;
        sine.osc["waveform"] = 0;
        cases.push_back({"sine_a4", single(69), samples(3.5), sine, referenceSub, false, {}});
        std::vector<NoteSpan> four;
        for (int i = 0; i < 4; ++i)
            four.push_back({samples(0.5 + 0.3 * i), samples(0.7 + 0.3 * i), 69});
        AgentSetup env = sine;
        env.osc["phase"] = 0.0;
        env.osc["randomPhase"] = 0.0;
        env.adsr = {{"attack", 0.001}, {"decay", 0.0}, {"sustain", 1.0}, {"release", 0.015}};
        cases.push_back({"env_a1_r15_sine",
                         four,
                         samples(2.5),
                         env,
                         [=](ReferenceRig& r) {
                             referenceSub(r);
                             r.setNorm("Env1 Atk", std::pow(0.001f / 32.0f, 0.2f), "time = 32 s * x^5, 1 ms");
                             r.setNorm("Env1 Dec", 0.0f);
                             r.setNorm("Env1 Sus", 1.0f, "0 dB (100%)");
                         },
                         false,
                         {}});
    }

    // Unison stack shape: one low-partial note, long enough to resolve each voice's frequency.
    {
        const std::vector<NoteSpan> held{{samples(0.5), samples(10.5), 69}};
        for (const auto& [voices, rand] :
             {std::pair<int, double>{3, 100.0}, {5, 100.0}, {7, 100.0}, {5, 0.0}, {7, 0.0}}) {
            AgentSetup a;
            a.osc["waveform"] = 2;
            a.osc["unison"] = voices;
            a.osc["detune"] = kReferenceDefaultDetuneCents;
            a.osc["blend"] = 0.75;
            a.osc["width"] = 1.0;
            a.osc["randomPhase"] = rand;
            a.osc["phase"] = 180.0;
            const juce::String name = "stack_" + juce::String(voices) + (rand == 0.0 ? "_r0" : "");
            cases.push_back({name,
                             held,
                             samples(11.0),
                             a,
                             [=](ReferenceRig& r) {
                                 r.setNumeric("A Unison", voices);
                                 r.setNumeric("A RandPhase", rand);
                             },
                             false,
                             {}});
        }
    }

    // Alias sweep: one note, 1.5 s held, no unison, rand phase pinned to 0 on both synths.
    const std::vector<NoteSpan> none;
    for (int midi : {60, 72, 84, 96, 108}) {
        const std::vector<NoteSpan> note{{samples(0.5), samples(2.0), midi}};
        const int len = samples(3.0);
        for (const auto& [wave, index] : {std::pair<const char*, int>{"saw", 2}, {"square", 1}, {"triangle", 3}}) {
            AgentSetup a;
            a.osc["waveform"] = index;
            a.osc["randomPhase"] = 0.0;
            a.osc["unison"] = 1;
            const juce::String name = juce::String("alias_") + wave + "_" + juce::String(midi);
            const bool isSaw = juce::String(wave) == "saw";
            cases.push_back(
                {name, note, len, a,
                 [=](ReferenceRig& r) {
                     if (isSaw)
                         r.setNumeric("A RandPhase", 0.0);
                 },
                 false,
                 isSaw ? juce::String()
                       : juce::String("The reference synth has only the init saw table (no wavetable choice)")});
        }
        AgentSetup wt;
        wt.wavetable = true;
        wt.osc["randomPhase"] = 0.0;
        wt.osc["unison"] = 1;
        cases.push_back({"alias_wt_" + juce::String(midi), note, len, wt, [](ReferenceRig&) {}, false,
                         "The reference synth has no counterpart for the Wavetable module case"});
    }
    for (const auto& c : cases) {
        juce::var entry;
        ASSERT_TRUE(renderCase(dir, backend, c, entry)) << c.name;
        casesObj->setProperty(c.name, entry);
    }
    EXPECT_TRUE(settingsFile.replaceWithText(juce::JSON::toString(top)));
}

namespace {

AgentSetup sawStack(int voices, double detuneCents, double rand) {
    AgentSetup a;
    a.osc["waveform"] = 2;
    a.osc["unison"] = voices;
    a.osc["detune"] = detuneCents;
    a.osc["blend"] = 0.75;
    a.osc["width"] = 1.0;
    a.osc["randomPhase"] = rand;
    a.osc["phase"] = 180.0; // the reference synth's default start phase
    return a;
}

} // namespace

// One held A4 per Detune setting, long enough for the FFT to resolve each voice: detmap_<n> sets the reference's
// Detune to normalised position n/1000 with 7 voices. AgentSynth is rendered at 25 cents only to keep the pair.
TEST(ReferenceCompareRender, RenderUnisonDetuneMap) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    dir.createDirectory();
    synth::DefaultHostedPluginBackend backend;
    const std::vector<NoteSpan> held{{samples(0.5), samples(10.5), 69}};
    for (const int mille : {0, 125, 250, 375, 500, 625, 750, 875, 1000}) {
        Case c{"detmap_" + juce::String(mille),
               held,
               samples(11.0),
               sawStack(7, 25.0, 0.0),
               [=](ReferenceRig& r) {
                   r.setNumeric("A Unison", 7);
                   r.setNorm("A UniDet", (float)mille / 1000.0f, "normalised position");
                   r.setNumeric("A RandPhase", 0.0);
               },
               false,
               {}};
        juce::var entry;
        ASSERT_TRUE(renderCase(dir, backend, c, entry)) << c.name;
    }
}

// Unison settings grid against the reference synth's init unison: which of Detune, Rand and Phase closes the gap.
TEST(ReferenceCompareRender, RenderUnisonGrid) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    dir.createDirectory();
    synth::DefaultHostedPluginBackend backend;
    for (double detune : {25.0, 35.0, kReferenceDefaultDetuneCents})
        for (double rand : {0.0, 100.0}) {
            AgentSetup a;
            a.osc["waveform"] = 2;
            a.osc["unison"] = 7;
            a.osc["detune"] = detune;
            a.osc["blend"] = 0.75;
            a.osc["width"] = 1.0;
            a.osc["randomPhase"] = rand;
            a.osc["phase"] = 180.0;
            const juce::String name = "grid_d" + juce::String((int)detune) +
                                      (detune == kReferenceDefaultDetuneCents ? "m" : "") + "_r" +
                                      juce::String((int)rand);
            Case c{name,
                   chord(),
                   samples(3.5),
                   a,
                   [=](ReferenceRig& r) {
                       r.setNumeric("A Unison", 7);
                       r.setNumeric("A RandPhase", rand);
                   },
                   false,
                   {}};
            juce::var entry;
            ASSERT_TRUE(renderCase(dir, backend, c, entry)) << c.name;
        }
}
