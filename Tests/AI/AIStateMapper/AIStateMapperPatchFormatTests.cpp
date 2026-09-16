// ---------------------------------------------------------------------------------------------
// Patch format: type-name fidelity, paramID stability, schemaVersion + node uuid,
// the authorable-module allowlist, and the reserved "timeline" key.
// ---------------------------------------------------------------------------------------------
#include "AIStateMapperTestHelpers.h"
#include <gtest/gtest.h>
#include <map>

namespace {

// The only factory key that cannot round-trip, and why: "Mod Slot" is a SECOND registration of
// AttenuverterModule under the name the modulation UI uses. The two produce an identical
// processor with nothing on it to tell them apart, so a Mod Slot necessarily serializes as
// "Attenuverter" — and, since applyJSONToGraph builds attenuverter chains itself from the
// "modulations" array, nothing is lost by that.
//
// Nothing else belongs here. A new entry means some module's saved type string does not name a
// module the factory can rebuild, which is the class of bug that silently turned every saved Poly
// Sequencer into a mono Sequencer (issue #196).
const std::map<juce::String, juce::String> kFactoryTypeNameAliases = {{"Mod Slot", "Attenuverter"}};

juce::String sortedParamIds(juce::AudioProcessor& processor) {
    juce::StringArray ids;
    for (auto* param : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            ids.add(withId->paramID);
    ids.sort(false);
    return ids.joinIntoString(", ");
}

juce::var nodeWithId(const juce::var& patch, int id) {
    if (auto* nodes = patch.getDynamicObject()->getProperty("nodes").getArray())
        for (const auto& n : *nodes)
            if ((int)n.getDynamicObject()->getProperty("id") == id)
                return n;
    return {};
}

// Navigates to schema.properties.nodes.items.properties. The schema var must outlive the returned
// pointer — every step here borrows from the object the caller is holding.
juce::DynamicObject* schemaNodeProperties(const juce::var& schema) {
    return schema.getDynamicObject()
        ->getProperty("properties")
        .getDynamicObject()
        ->getProperty("nodes")
        .getDynamicObject()
        ->getProperty("items")
        .getDynamicObject()
        ->getProperty("properties")
        .getDynamicObject();
}

juce::StringArray uuidsOf(const juce::var& patch) {
    juce::StringArray uuids;
    if (auto* nodes = patch.getDynamicObject()->getProperty("nodes").getArray())
        for (const auto& n : *nodes)
            uuids.add(n.getDynamicObject()->getProperty("uuid").toString());
    uuids.sort(false);
    return uuids;
}

} // namespace

// Every module the factory can build must serialize under the key that rebuilds it. graphToJSON
// writes getFactoryTypeName() and applyJSONToGraph feeds that string straight back to
// createModule, so any mismatch is silent data loss on every save/load AND on every structural
// undo (which replays the same JSON) — exactly what ModuleType::PolySequencer → "Sequencer" did.
TEST(AIStateMapperTest, FactoryTypeNamesRoundTrip) {
    for (const auto& key : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto module = synth::AIStateMapper::createModule(key);
        ASSERT_NE(module, nullptr) << "factory key \"" << key << "\" produced nothing";

        const juce::String serialized = synth::AIStateMapper::getFactoryTypeName(module.get());
        const auto alias = kFactoryTypeNameAliases.find(key);
        const juce::String expected = alias == kFactoryTypeNameAliases.end() ? key : alias->second;

        EXPECT_EQ(serialized, expected)
            << "createModule(\"" << key << "\") serializes as \"" << serialized
            << "\" — a saved patch would rebuild it as that instead. Fix getFactoryTypeName, or "
               "record the alias in kFactoryTypeNameAliases if the two really are one module.";

        // The serialized name must itself be rebuildable, or the node vanishes on load.
        EXPECT_NE(synth::AIStateMapper::createModule(serialized), nullptr)
            << "\"" << serialized << "\" is not resolvable by createModule";
    }
}

// The regression that motivated the fix: a Poly Sequencer used to come back as a mono Sequencer
// from every save/load and every undo (issue #196).
TEST(AIStateMapperTest, PolySequencerSurvivesRoundTrip) {
    juce::AudioProcessorGraph graph;
    ASSERT_NE(graph.addNode(synth::AIStateMapper::createModule("Poly Sequencer")), nullptr);

    juce::var json = synth::AIStateMapper::graphToJSON(graph);
    EXPECT_EQ(nodeWithId(json, (int)graph.getNodes().getUnchecked(0)->nodeID.uid)
                  .getDynamicObject()
                  ->getProperty("type")
                  .toString(),
              "Poly Sequencer");

    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, reloaded, /*clearExisting=*/true, /*trusted=*/true));
    ASSERT_EQ(reloaded.getNumNodes(), 1);
    EXPECT_EQ(reloaded.getNodes().getUnchecked(0)->getProcessor()->getName(), "Poly Sequencer");
}

// Golden: the exact paramIDs every factory module exposes.
//
// paramIDs are this app's real parameter ABI — presets, undo snapshots, AI patches and (next)
// automation lanes all address parameters by these strings, and nothing else pins them. Renaming
// one compiles cleanly and silently drops that value from every patch already saved with it.
//
// TO UPDATE: read the failure, which prints the actual line in pasteable form, and replace the
// matching row below — but only once you have confirmed the change is intentional and that any
// RENAME ships with migration for existing presets. Adding a module adds a row; adding or
// removing a parameter edits one row.
TEST(AIStateMapperTest, ParamIdsGolden) {
    const std::map<juce::String, juce::String> golden = {
        {"ADSR", "attack, attackCurve, bypassed, decay, decayCurve, gateThreshold, hold, muted, poly, release, "
                 "releaseCurve, sustain"},
        {"Amp Env", "attack, attackCurve, bypassed, decay, decayCurve, gateThreshold, hold, muted, poly, release, "
                    "releaseCurve, sustain"},
        {"Attenuverter", "amount, bypassed"},
        // Audio Input is a ModuleBase, so it has ModuleBase's bypass parameter. Audio
        // Output is still the graph's raw IO node and still has none.
        {"Audio Input", "bypassed"},
        {"Audio Output", ""},
        {"Bitcrusher", "bypassed, depth, dither, dualIO, mix, muted, outputLevel, rate"},
        // The mixer's strip (P9-2). Solo is deliberately NOT here: it is a render-time gate kept
        // in trusted extra state, never a parameter (docs/mixer.md §5.3).
        {"Channel Strip", "bypassed, gain, muted, pan, send1Level, send2Level, send3Level, send4Level"},
        {"Chorus", "bypassed, centreDelay, depth, dualIO, feedback, mix, muted, outputLevel, rate"},
        {"Comparator", "bypassed, muted, trigThreshold"},
        {"Compressor", "attack, bypassed, dualIO, makeupGain, muted, ratio, release, threshold"},
        {"Delay", "bypassed, dualIO, feedback, mix, muted, outputLevel, time"},
        {"Distortion", "bypassed, drive, dualIO, mix, muted, outputLevel, oversampling, type"},
        {"Envelope Follower", "attack, bypassed, detection, muted, release, sensitivity"},
        {"External MIDI", "bypassed, channel, deviceIndex"},
        {"Filter", "bypassed, cutoff, drive, dualIO, filterType, muted, outputLevel, poly, resonance"},
        {"Filter Env", "attack, attackCurve, bypassed, decay, decayCurve, gateThreshold, hold, muted, poly, release, "
                       "releaseCurve, sustain"},
        {"Flanger", "bypassed, centreDelay, depth, dualIO, feedback, mix, muted, outputLevel, rate"},
        {"Gate", "attack, bypassed, dualIO, hold, muted, outputLevel, range, release, threshold"},
        // The host module has no parameters of its own beyond bypass/mute — the hosted
        // plugin's own parameters are exposed to the graph separately, as automation lanes.
        {"Hosted Plugin", "bypassed, muted"},
        {"LFO", "bipolar, bypassed, glide, level, mode, muted, rateHz, rateSync, retrig, shape"},
        {"Limiter", "bypassed, dualIO, inputGain, muted, release, threshold"},
        {"MIDI Keyboard", "bypassed, octave"},
        // The four Macro I/O port node types (P8-15): pure pass-throughs, so the inherited
        // "bypassed" is the whole parameter set — same reasoning as Rec Tap/Track In/Track Audio,
        // the reference pattern these follow (no "muted": nothing here for it to silence beyond
        // what bypass already covers).
        {"Macro In", "bypassed"},
        {"Macro MIDI In", "bypassed"},
        {"Macro MIDI Out", "bypassed"},
        {"Macro Out", "bypassed"},
        {"Macros", "bypassed, macro1, macro10, macro11, macro12, macro13, macro14, macro15, macro16, macro2, macro3, "
                   "macro4, macro5, macro6, macro7, macro8, macro9, macroBipolar, macroCount, muted"},
        // The mix bus (P9-2): a fader and a mute, no pan.
        {"Master", "bypassed, gain, muted"},
        {"Math", "bypassed, clip, muted"},
        {"Midi Input", ""},
        {"Mod Slot", "amount, bypassed"},
        {"Noise", "bypassed, color, level, muted, noiseType, poly"},
        {"Oscillator", "bypassed, coarse, detune, dualIO, fine, level, muted, octave, pan, poly, unison, waveform"},
        {"Parametric EQ", "band1Freq, band1Gain, band1On, band1Q, band2Freq, band2Gain, band2On, band2Q, band3Freq, "
                          "band3Gain, band3On, band3Q, band4Freq, band4Gain, band4On, band4Q, bypassed, dualIO, muted, "
                          "outputGain"},
        {"Phaser", "bypassed, centreFreq, depth, dualIO, feedback, mix, muted, outputLevel, rate"},
        {"Pitch Shifter",
         "bypassed, dualIO, feedback, fine, mix, muted, outputLevel, pitch, shiftHz, shiftMode, window"},
        {"Poly MIDI", "bypassed, velToGate, voiceSteal"},
        // A pass-through has nothing to tweak: the inherited bypass is the whole parameter set
        // (and there is deliberately no mute — muting a tap would silence the patch, which is
        // not what a recorder is for).
        {"Rec Tap", "bypassed"},
        {"Poly Sequencer", "Gate 1, Gate 2, Gate 3, Gate 4, Gate 5, Gate 6, Gate 7, Gate 8, Step 1 Chord, Step 1 Root, "
                           "Step 2 Chord, Step 2 Root, Step 3 Chord, Step 3 Root, Step 4 Chord, Step 4 Root, "
                           "Step 5 Chord, Step 5 Root, Step 6 Chord, Step 6 Root, Step 7 Chord, Step 7 Root, "
                           "Step 8 Chord, Step 8 Root, bpm, bypassed, run, syncToTransport"},
        {"Reverb", "bypassed, damping, dry, dualIO, muted, outputLevel, roomSize, wet, width"},
        {"Ring Modulator", "bypassed, character, drive, dualIO, mix, muted, outputLevel, oversampling"},
        {"Sample & Hold", "bypassed, clock, holdMode, level, muted, offset, rate, slew, source, trigThreshold"},
        {"Sampler",
         "bypassed, density, dualIO, grainSize, level, loop, muted, pitch, playMode, rootNote, spray, start"},
        {"Sequencer", "F.Env 1, F.Env 2, F.Env 3, F.Env 4, F.Env 5, F.Env 6, F.Env 7, F.Env 8, Gate 1, Gate 2, "
                      "Gate 3, Gate 4, Gate 5, Gate 6, Gate 7, Gate 8, Pitch 1, Pitch 2, Pitch 3, Pitch 4, Pitch 5, "
                      "Pitch 6, Pitch 7, Pitch 8, bpm, bypassed, run, syncToTransport"},
        // Every playback value it needs (gain, fades, trim) lives on the CLIP, not on the node,
        // so the inherited bypass is the whole parameter set — and there is deliberately no mute,
        // since a muted track is a document state the module already honours.
        {"Track Audio", "bypassed"},
        {"Track In", "bypassed"},
        {"VCA", "bypassed, dualIO, gain, muted, poly"},
        {"Voice Mixer", "bypassed, dualIO, level"},
        {"Wavetable", "blend, bypassed, coarse, detune, dualIO, fine, importMode, interpolation, level, muted, "
                      "octave, pan, phase, poly, position, randomPhase, spread, stack, subLevel, subOctave, "
                      "subShape, syncMode, table, unison, warp, warpAmount, width"},
    };

    const auto keys = synth::AIStateMapper::moduleFactoryTypeNames();
    EXPECT_EQ((int)golden.size(), keys.size()) << "a module was added to or removed from the factory";

    for (const auto& key : keys) {
        auto module = synth::AIStateMapper::createModule(key);
        ASSERT_NE(module, nullptr);
        const juce::String actual = sortedParamIds(*module);

        auto pinned = golden.find(key);
        if (pinned == golden.end()) {
            ADD_FAILURE() << "new module \"" << key << "\" — add this row to the golden:\n        {\"" << key
                          << "\", \"" << actual << "\"},";
            continue;
        }

        EXPECT_EQ(actual, pinned->second)
            << "paramIDs changed for \"" << key << "\". If that is intended, replace its row with:\n        {\"" << key
            << "\", \"" << actual << "\"},";
    }
}

// Golden: exactly which module types the model is allowed to author.
//
// The list is DERIVED from the factory, so registering a module makes it model-authorable by
// default — which is the wrong default for anything that names an external resource or carries
// privileged state (a hosted plugin, a timeline feed). This test exists to make that a decision:
// any registration changes the list and MUST consciously update the golden below, either by adding
// the new type here or by adding it to kNonAuthorableModuleTypes in AIStateMapper/AIStateMapperInternal.h.
TEST(AIStateMapperTest, AuthorableModuleTypesGolden) {
    const juce::StringArray golden = {"ADSR",
                                      "Amp Env",
                                      "Audio Input",
                                      "Audio Output",
                                      "Bitcrusher",
                                      "Chorus",
                                      "Comparator",
                                      "Compressor",
                                      "Delay",
                                      "Distortion",
                                      "Envelope Follower",
                                      "External MIDI",
                                      "Filter",
                                      "Filter Env",
                                      "Flanger",
                                      "Gate",
                                      "LFO",
                                      "Limiter",
                                      "MIDI Keyboard",
                                      "Macros",
                                      "Math",
                                      "Midi Input",
                                      "Noise",
                                      "Oscillator",
                                      "Parametric EQ",
                                      "Phaser",
                                      "Pitch Shifter",
                                      "Poly MIDI",
                                      "Poly Sequencer",
                                      "Reverb",
                                      "Ring Modulator",
                                      "Sample & Hold",
                                      "Sampler",
                                      "Sequencer",
                                      "VCA",
                                      "Voice Mixer",
                                      "Wavetable"};

    const auto actual = synth::AIStateMapper::authorableModuleTypes();
    EXPECT_EQ(actual.joinIntoString(", "), golden.joinIntoString(", "))
        << "the set of model-authorable modules changed — update this golden deliberately";

    // The deliberate exclusions, stated positively so a silent removal of the deny set fails.
    EXPECT_FALSE(actual.contains("Attenuverter"));
    EXPECT_FALSE(actual.contains("Mod Slot"));
    // The timeline feed. Registered in the factory (our own saves round-trip it) but never
    // offered to a model — and refused outright by validatePatch on the untrusted path rather than
    // merely omitted from the schema.
    EXPECT_FALSE(actual.contains("Track In"));
    // The audio-take tap. Registered in the factory (a patch with one has to round-trip)
    // but never offered to a model, and refused outright by validatePatch on the untrusted path —
    // it names a file path on disk, so authoring one is authoring a write target.
    EXPECT_FALSE(actual.contains("Rec Tap"));
    // The audio-track player. Same reasoning from the other direction — it plays whatever
    // clips the track bound to it names, so authoring one is choosing what gets read off disk.
    EXPECT_FALSE(actual.contains("Track Audio"));
    // A hosted third-party plugin. The strongest exclusion on this list — the node's
    // "state" is an opaque byte blob fed straight to AudioPluginInstance::setStateInformation, and
    // its identity selects which binary the host loads. This EXPECT_FALSE is what makes the
    // untrusted-unreachable mechanism cover hosting: the type is refused by validatePatch on the
    // untrusted path, not merely omitted from the schema.
    EXPECT_FALSE(actual.contains("Hosted Plugin"));
    // A Macro's audio/CV inlet/outlet jack (P8-15 Macro I/O). Registered in the factory (our own
    // saves round-trip one) but never offered to a model, and refused outright by validatePatch on
    // the untrusted path — its macro membership is keyed by node uuid, which is trusted-only, so a
    // model-authored one could never resolve to a real macro's port list.
    EXPECT_FALSE(actual.contains("Macro In"));
    EXPECT_FALSE(actual.contains("Macro Out"));
    // A Macro's MIDI inlet/outlet jack — same reasoning as "Macro In"/"Macro Out" from the other
    // signal type.
    EXPECT_FALSE(actual.contains("Macro MIDI In"));
    EXPECT_FALSE(actual.contains("Macro MIDI Out"));
    // The mixer's strip and bus (P9-2, docs/mixer.md §6). "The AI can build a channel" is an
    // app-side action the model invokes, never a node it writes.
    EXPECT_FALSE(actual.contains("Channel Strip"));
    EXPECT_FALSE(actual.contains("Master"));

    // The schema hands the model exactly this list.
    const juce::var schema = synth::AIStateMapper::getPatchSchema(); // held: the chain below points into it
    const juce::var typeDef = schemaNodeProperties(schema)->getProperty("type");
    auto* typeEnum = typeDef.getDynamicObject()->getProperty("enum").getArray();
    ASSERT_NE(typeEnum, nullptr);

    juce::StringArray fromSchema;
    for (const auto& entry : *typeEnum)
        fromSchema.add(entry.toString());
    EXPECT_EQ(fromSchema.joinIntoString(", "), actual.joinIntoString(", "));
}

// "Non-authorable" must mean UNTRUSTED-UNREACHABLE, not just "absent from the
// schema". The schema enum is a hint our own backend enforces as a grammar; a patch can also come
// from a local model, a hand-edited file loaded untrusted, or any future caller that never saw the
// schema — so validatePatch refuses internal-only types itself.
TEST(AIStateMapperTest, UntrustedPatchRejectsInternalOnlyModuleTypes) {
    juce::AudioProcessorGraph graph;

    // "Hosted Plugin" is unrelated to the timeline: hosting is independent of it, so the type is
    // registered — and must therefore be refused — regardless.
    juce::StringArray internalTypes = {"Attenuverter", "Mod Slot", "Hosted Plugin"};
    internalTypes.add("Track In");
    internalTypes.add("Rec Tap");
    internalTypes.add("Track Audio");
    internalTypes.add("Macro In");
    internalTypes.add("Macro Out");
    internalTypes.add("Macro MIDI In");
    internalTypes.add("Macro MIDI Out");
    internalTypes.add("Channel Strip");
    internalTypes.add("Master");

    for (const auto& type : internalTypes) {
        const juce::var json =
            juce::JSON::parse("{\"nodes\":[{\"id\":1,\"type\":\"" + type + "\"}],\"connections\":[]}");
        const auto result = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/true,
                                                                /*trusted=*/false);
        EXPECT_FALSE(result.ok) << type << " must not be creatable from an untrusted patch";
        EXPECT_EQ(result.error, synth::PatchValidationError::InternalModuleNotAllowed)
            << "rejected \"" << type << "\" as " << synth::patchValidationErrorName(result.error);
        EXPECT_TRUE(result.message.contains(type)) << "the rejection must name the offending type";

        // And the rejection is load-bearing: apply must refuse the same patch outright.
        juce::AudioProcessorGraph applyTarget;
        EXPECT_FALSE(synth::AIStateMapper::applyJSONToGraph(json, applyTarget, /*clearExisting=*/true,
                                                            /*trusted=*/false));
        EXPECT_EQ(applyTarget.getNumNodes(), 0);
    }
}

// The other half of the same rule: our OWN saves must still round-trip a Track In node, uuid
// intact — that uuid is what the timeline track binds to, so losing it orphans the track.
TEST(AIStateMapperTest, TrustedApplyRoundTripsTrackInWithStableUuid) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(synth::AIStateMapper::createModule("Track In"));
    ASSERT_NE(node, nullptr);

    const juce::var firstSave = synth::AIStateMapper::graphToJSON(graph);
    const auto uuids = uuidsOf(firstSave);
    ASSERT_EQ(uuids.size(), 1);
    ASSERT_FALSE(uuids[0].isEmpty());

    // graphToJSON's lazy uuid generation must have mirrored into the processor (plumbing piece 2).
    auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
    ASSERT_NE(mb, nullptr);
    EXPECT_EQ(juce::String(mb->getNodeUuid()), uuids[0]);

    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(firstSave, reloaded, /*clearExisting=*/true, /*trusted=*/true));
    ASSERT_EQ(reloaded.getNumNodes(), 1);

    auto reloadedNode = reloaded.getNodes().getUnchecked(0);
    EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(reloadedNode->getProcessor()), "Track In");
    EXPECT_EQ(reloadedNode->properties["uuid"].toString(), uuids[0]);

    auto* reloadedModule = dynamic_cast<ModuleBase*>(reloadedNode->getProcessor());
    ASSERT_NE(reloadedModule, nullptr);
    EXPECT_EQ(juce::String(reloadedModule->getNodeUuid()), uuids[0])
        << "a trusted apply must mirror the adopted uuid into the processor";

    EXPECT_EQ(uuidsOf(synth::AIStateMapper::graphToJSON(reloaded)).joinIntoString(","), uuids.joinIntoString(","));
}

TEST(AIStateMapperTest, GraphToJSONEmitsSchemaVersionAndNodeUuids) {
    juce::AudioProcessorGraph graph;
    createBasicGraph(graph);

    juce::var json = synth::AIStateMapper::graphToJSON(graph);
    ASSERT_NE(json.getDynamicObject(), nullptr);
    EXPECT_EQ((int)json.getDynamicObject()->getProperty("schemaVersion"), synth::AIStateMapper::kSchemaVersion);

    auto uuids = uuidsOf(json);
    EXPECT_EQ(uuids.size(), graph.getNumNodes());
    for (const auto& uuid : uuids)
        EXPECT_FALSE(uuid.isEmpty()) << "every node must carry a uuid";

    juce::StringArray unique = uuids;
    unique.removeDuplicates(false);
    EXPECT_EQ(unique.size(), uuids.size()) << "uuids must be unique within a patch";
}

// An absent schemaVersion means 1 and gates nothing: patches written before the field existed must
// keep loading unchanged.
TEST(AIStateMapperTest, PatchWithoutSchemaVersionStillApplies) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Filter"}],"connections":[]})");
    EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));
    EXPECT_EQ(graph.getNumNodes(), 1);
}

// Save → load → save on the trusted path must reproduce the same identities: this is what lets a
// long-lived reference (an automation lane, a timeline track binding) survive a preset round trip.
TEST(AIStateMapperTest, NodeUuidsAreStableAcrossTrustedRoundTrip) {
    juce::AudioProcessorGraph graph;
    createBasicGraph(graph);

    juce::var firstSave = synth::AIStateMapper::graphToJSON(graph);
    // Saving the same live graph twice must not mint new identities either.
    EXPECT_EQ(uuidsOf(synth::AIStateMapper::graphToJSON(graph)).joinIntoString(","),
              uuidsOf(firstSave).joinIntoString(","));

    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(firstSave, reloaded, /*clearExisting=*/true, /*trusted=*/true));

    juce::var secondSave = synth::AIStateMapper::graphToJSON(reloaded);
    EXPECT_EQ(uuidsOf(secondSave).joinIntoString(","), uuidsOf(firstSave).joinIntoString(","));
}

// Untrusted input must never dictate identity — otherwise a model could hand two nodes the same
// uuid, or claim the uuid of a node something else already points at.
TEST(AIStateMapperTest, UntrustedApplyIgnoresIncomingNodeUuids) {
    const juce::String claimed = "11111111-2222-3333-4444-555555555555";
    juce::String jsonStr = "{\"nodes\":[{\"id\":1,\"type\":\"Filter\",\"uuid\":\"" + claimed +
                           "\"},{\"id\":2,\"type\":\"Oscillator\",\"uuid\":\"" + claimed + "\"}],\"connections\":[]}";
    juce::var json = juce::JSON::parse(jsonStr);

    juce::AudioProcessorGraph untrustedGraph;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, untrustedGraph, /*clearExisting=*/true,
                                                       /*trusted=*/false));
    auto regenerated = uuidsOf(synth::AIStateMapper::graphToJSON(untrustedGraph));
    ASSERT_EQ(regenerated.size(), 2);
    for (const auto& uuid : regenerated) {
        EXPECT_FALSE(uuid.isEmpty());
        EXPECT_NE(uuid, claimed) << "a model must not be able to choose a node's identity";
    }
    EXPECT_NE(regenerated[0], regenerated[1]) << "colliding uuids must not survive the untrusted path";

    // The same JSON on the trusted path (our own preset/undo replay) keeps what it was given.
    juce::AudioProcessorGraph trustedGraph;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, trustedGraph, /*clearExisting=*/true, /*trusted=*/true));
    auto honoured = uuidsOf(synth::AIStateMapper::graphToJSON(trustedGraph));
    ASSERT_EQ(honoured.size(), 2);
    EXPECT_EQ(honoured[0], claimed);
    EXPECT_EQ(honoured[1], claimed);
}

// The model-facing schema is an invitation list: anything named in it is something the model is
// being asked to emit. The three reserved fields are ours to write, never its.
TEST(AIStateMapperTest, SchemaOmitsReservedFields) {
    const juce::var schema = synth::AIStateMapper::getPatchSchema();
    const juce::String schemaText = juce::JSON::toString(schema);

    for (const char* reserved : {"schemaVersion", "uuid", "timeline"})
        EXPECT_FALSE(schemaText.contains(reserved))
            << "\"" << reserved << "\" must not appear anywhere in the model-facing patch schema";

    auto* nodeProperties = schemaNodeProperties(schema);
    ASSERT_NE(nodeProperties, nullptr);
    for (const char* reserved : {"schemaVersion", "uuid", "timeline"})
        EXPECT_FALSE(nodeProperties->hasProperty(reserved)) << "node schema must not offer \"" << reserved << "\"";

    auto* rootProperties = schema.getDynamicObject()->getProperty("properties").getDynamicObject();
    ASSERT_NE(rootProperties, nullptr);
    for (const char* reserved : {"schemaVersion", "uuid", "timeline"})
        EXPECT_FALSE(rootProperties->hasProperty(reserved)) << "root schema must not offer \"" << reserved << "\"";
}

// "timeline" is refused rather than ignored: the validator lets unknown keys through, so a later
// build that starts honouring timeline data would silently begin executing provider-authored
// automation against patches accepted today.
TEST(AIStateMapperTest, TimelineIsRefusedFromUntrustedPatchesOnly) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(
        R"({"nodes":[{"id":1,"type":"Filter"}],"connections":[],"timeline":{"tracks":[{"clips":[]}]}})");

    auto untrusted = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/true, /*trusted=*/false);
    EXPECT_FALSE(untrusted.ok);
    EXPECT_EQ(untrusted.error, synth::PatchValidationError::TimelineNotAllowed);
    EXPECT_TRUE(untrusted.message.containsIgnoreCase("timeline")) << "the model must be told what to remove";
    EXPECT_EQ(synth::patchValidationErrorName(synth::PatchValidationError::TimelineNotAllowed), "TimelineNotAllowed");

    EXPECT_FALSE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/false));
    EXPECT_EQ(graph.getNumNodes(), 0) << "a refused patch must not be partially applied";

    // The SAME JSON is accepted on the trusted path — future project files ride this key.
    auto trusted = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/true, /*trusted=*/true);
    EXPECT_TRUE(trusted.ok) << trusted.message;
    EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));
    EXPECT_EQ(graph.getNumNodes(), 1);
}
