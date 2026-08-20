// validateTimeline, the untrusted gate for AI/tool-supplied timeline data.
//
// Every case starts from a REAL document's toVar() output and breaks exactly one thing, so a
// failure means the validator classified the intended defect rather than something incidental
// (the same idiom as Tests/AIPatchValidationTests.cpp). Asserting the exact enumerator is the
// point: the timeline tools feed the message back to the model and branch on the code, so a
// rejection landing in the wrong bucket would send the wrong correction.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Timeline/TimelineValidator.h"
#include <functional>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include <vector>

using synth::AutomationLane;
using synth::MidiNote;
using synth::TimelineDoc;
using synth::TimelineValidationError;
using synth::TrackKind;
using synth::validateTimeline;

namespace {

// The live parameter the lane in every fixture document automates: FilterModule's "cutoff",
// whose real range is 20 .. 20000 Hz. Restated here because the value-bounds cases depend on
// those exact numbers.
constexpr double kCutoffMin = 20.0;
constexpr double kCutoffMax = 20000.0;

AutomationLane::RangeSnapshot makeRange(float minValue, float maxValue, float defaultValue) {
    AutomationLane::RangeSnapshot range;
    range.minValue = minValue;
    range.maxValue = maxValue;
    range.defaultValue = defaultValue;
    return range;
}

MidiNote makeNote(double startBeat, int pitch) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = 1.0;
    note.pitch = pitch;
    note.velocity = 100;
    note.channel = 1;
    return note;
}

// -- var navigation ---------------------------------------------------------------------------
// juce::var shares its DynamicObject/array by reference, so these hand back the live objects
// inside the parsed document: a case mutates the real thing, it does not build a parallel copy.

juce::DynamicObject& obj(const juce::var& v) {
    auto* o = v.getDynamicObject();
    jassert(o != nullptr);
    return *o;
}

juce::Array<juce::var>& arrayProp(juce::DynamicObject& owner, const char* key) {
    auto* arr = owner.getProperty(key).getArray();
    jassert(arr != nullptr);
    return *arr;
}

juce::DynamicObject& trackObj(juce::var& root, int index = 0) { return obj(root["tracks"][index]); }
juce::DynamicObject& clipObj(juce::var& root, int trackIndex = 0, int clipIndex = 0) {
    return obj(arrayProp(trackObj(root, trackIndex), "clips").getReference(clipIndex));
}
juce::DynamicObject& noteObj(juce::var& root, int noteIndex = 0) {
    return obj(arrayProp(clipObj(root), "notes").getReference(noteIndex));
}
juce::DynamicObject& laneObj(juce::var& root, int laneIndex = 0) {
    return obj(arrayProp(trackObj(root), "lanes").getReference(laneIndex));
}
juce::DynamicObject& pointObj(juce::var& root, int pointIndex = 0) {
    return obj(arrayProp(laneObj(root), "points").getReference(pointIndex));
}

// Pads an array out to `size` by repeating its first element. Only ever used for the cap cases,
// where the cap is checked before the elements are, so aliased duplicates are exactly as good as
// distinct ones and a great deal cheaper.
void growTo(juce::Array<juce::var>& arr, int size) {
    const juce::var element = arr.getFirst();
    while (arr.size() < size)
        arr.add(element);
}

// The smallest legal note: everything but the id takes its documented default (beat 0, length 1,
// pitch 60, velocity 100, channel 1). Ids must be unique doc-wide, so the total-notes case cannot
// use growTo().
juce::var makeNoteVar(int id) {
    juce::DynamicObject::Ptr n = new juce::DynamicObject();
    n->setProperty("id", id);
    return juce::var(n.get());
}

juce::var makeClipVar(int id, double startBeat, int firstNoteId, int noteCount) {
    juce::DynamicObject::Ptr c = new juce::DynamicObject();
    c->setProperty("id", id);
    c->setProperty("startBeat", startBeat);
    c->setProperty("lengthBeats", 4.0);
    juce::Array<juce::var> notes;
    for (int i = 0; i < noteCount; ++i)
        notes.add(makeNoteVar(firstNoteId + i));
    c->setProperty("notes", juce::var(notes));
    return juce::var(c.get());
}

struct Case {
    TimelineValidationError expected;
    const char* what;                        // what this case breaks
    std::function<void(juce::var&)> breakIt; // mutates (or replaces) the valid document
};

std::vector<Case> makeCases() {
    return {
        {TimelineValidationError::MalformedRoot, "root is a JSON array, not an object",
         [](juce::var& root) { root = juce::JSON::parse(R"([1,2,3])"); }},

        {TimelineValidationError::MalformedRoot, "an unknown top-level key rides along",
         [](juce::var& root) { obj(root).setProperty("timelineExtras", "surprise"); }},

        {TimelineValidationError::MalformedRoot, "\"version\" is newer than this build's format",
         [](juce::var& root) { obj(root).setProperty("version", TimelineDoc::kFormatVersion + 1); }},

        {TimelineValidationError::TooManyTracks, "one track over kMaxTracks",
         [](juce::var& root) { growTo(arrayProp(obj(root), "tracks"), TimelineDoc::kMaxTracks + 1); }},

        {TimelineValidationError::TooManyClips, "one clip over kMaxClipsPerTrack",
         [](juce::var& root) { growTo(arrayProp(trackObj(root), "clips"), TimelineDoc::kMaxClipsPerTrack + 1); }},

        {TimelineValidationError::TooManyNotes, "one note over kMaxNotesPerClip in a single clip",
         [](juce::var& root) { growTo(arrayProp(clipObj(root), "notes"), TimelineDoc::kMaxNotesPerClip + 1); }},

        {TimelineValidationError::TooManyNotes, "every clip is legal but the document total is over budget",
         [](juce::var& root) {
             // Five clips of 13108 notes: each well under kMaxNotesPerClip (16384), 65540 in total,
             // which is over kMaxTotalNotesUntrusted (65536). Ids stay unique doc-wide.
             auto& clips = arrayProp(trackObj(root), "clips");
             clips.clear();
             constexpr int perClip = 13108;
             for (int i = 0; i < 5; ++i)
                 clips.add(makeClipVar(1000 + i, i * 4.0, 100000 + i * perClip, perClip));
         }},

        {TimelineValidationError::TooManyLanes, "one lane over kMaxLanesPerTrack",
         [](juce::var& root) { growTo(arrayProp(trackObj(root), "lanes"), TimelineDoc::kMaxLanesPerTrack + 1); }},

        {TimelineValidationError::TooManyBreakpoints, "one breakpoint over kMaxBreakpointsPerLane",
         [](juce::var& root) { growTo(arrayProp(laneObj(root), "points"), TimelineDoc::kMaxBreakpointsPerLane + 1); }},

        {TimelineValidationError::BeatOutOfBounds, "a clip starts past kMaxPpqUntrusted",
         [](juce::var& root) { clipObj(root).setProperty("startBeat", synth::kMaxPpqUntrusted + 1.0); }},

        {TimelineValidationError::BeatOutOfBounds, "a clip has a zero length",
         [](juce::var& root) { clipObj(root).setProperty("lengthBeats", 0.0); }},

        {TimelineValidationError::BeatOutOfBounds, "a note starts at a negative beat",
         [](juce::var& root) { noteObj(root).setProperty("startBeat", -1.0); }},

        {TimelineValidationError::BeatOutOfBounds, "a breakpoint sits past kMaxPpqUntrusted",
         [](juce::var& root) { pointObj(root).setProperty("beat", synth::kMaxPpqUntrusted * 2.0); }},

        {TimelineValidationError::NoteOutOfRange, "a note's pitch is 128",
         [](juce::var& root) { noteObj(root).setProperty("pitch", 128); }},

        {TimelineValidationError::NoteOutOfRange, "a note's velocity is 0 (not a note-on)",
         [](juce::var& root) { noteObj(root).setProperty("velocity", 0); }},

        {TimelineValidationError::NoteOutOfRange, "a note's channel is 17",
         [](juce::var& root) { noteObj(root).setProperty("channel", 17); }},

        {TimelineValidationError::ValueOutOfParamRange, "a breakpoint is above the live parameter's maximum",
         [](juce::var& root) { pointObj(root).setProperty("value", kCutoffMax * 2.0); }},

        {TimelineValidationError::UnresolvableBinding, "a lane names a node uuid that is not in the graph",
         [](juce::var& root) { laneObj(root).setProperty("nodeUuid", "no-such-node"); }},

        {TimelineValidationError::UnresolvableBinding, "a lane names a real node but a parameter it does not have",
         [](juce::var& root) { laneObj(root).setProperty("paramId", "notAParameter"); }},

        {TimelineValidationError::UnresolvableBinding, "a track is bound to a node uuid that is not in the graph",
         [](juce::var& root) { trackObj(root).setProperty("bindingUuid", "no-such-node"); }},

        {TimelineValidationError::AssetNotAllowed, "a clip carries an audio assetRef",
         [](juce::var& root) { clipObj(root).setProperty("assetRef", "Audio/take-1.wav"); }},

        {TimelineValidationError::RecordModeNotAllowed, "a lane asks for Touch",
         [](juce::var& root) {
             laneObj(root).setProperty("recordMode", static_cast<int>(synth::LaneRecordMode::Touch));
         }},

        {TimelineValidationError::ReservedKindNotAllowed, "a track uses a reserved TrackKind",
         [](juce::var& root) { trackObj(root).setProperty("kind", 7); }},

        {TimelineValidationError::InternalError, "the loader refuses a next-id counter this gate does not model",
         [](juce::var& root) { obj(root).setProperty("nextClipId", 0); }},
    };
}

} // namespace

class TimelineValidatorTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    TimelineDoc doc;

    void SetUp() override {
        auto osc = graph.addNode(std::make_unique<OscillatorModule>());
        osc->properties.set("uuid", "osc-uuid");
        auto filter = graph.addNode(std::make_unique<FilterModule>());
        filter->properties.set("uuid", "filter-uuid");

        const auto track = doc.addTrack(TrackKind::Midi, "Lead");
        doc.setTrackBinding(track, "osc-uuid");

        const auto clip = doc.addClip(track, 0.0, 4.0, "Verse");
        doc.addNote(clip, makeNote(0.0, 60));
        doc.addNote(clip, makeNote(1.0, 64));

        const auto lane =
            doc.addLane(track, "filter-uuid", "cutoff",
                        makeRange(static_cast<float>(kCutoffMin), static_cast<float>(kCutoffMax), 440.0f));
        doc.addBreakpoint(lane, 0.0, 440.0);
        doc.addBreakpoint(lane, 4.0, 8000.0);
    }

    // A fresh serialisation each time: the cases mutate the var in place, so they must never
    // share one.
    juce::var validDocument() { return doc.toVar(); }
};

// =============================================================================
// 1. The happy path — and that a pass really does mean "fromVar will take it"
// =============================================================================

TEST_F(TimelineValidatorTest, ValidDocumentPassesAndThenApplies) {
    const juce::var document = validDocument();

    const auto result = validateTimeline(document, graph);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.error, TimelineValidationError::None);
    EXPECT_TRUE(result.message.isEmpty());

    // The contract: the caller applies only after this passes, and the apply cannot then fail.
    TimelineDoc target;
    ASSERT_TRUE(target.fromVar(document));
    ASSERT_EQ(target.getTracks().size(), 1u);
    const auto& track = target.getTracks().front();
    EXPECT_EQ(track.name, "Lead");
    ASSERT_EQ(track.clips.size(), 1u);
    EXPECT_EQ(track.clips.front().notes.size(), 2u);
    ASSERT_EQ(track.lanes.size(), 1u);
    EXPECT_EQ(track.lanes.front().points.size(), 2u);
}

TEST_F(TimelineValidatorTest, EmptyTimelinePasses) {
    TimelineDoc empty;
    EXPECT_TRUE(validateTimeline(empty.toVar(), graph).ok) << "a document with no tracks is legal, just inert";
}

TEST_F(TimelineValidatorTest, ValidationNeverMutatesItsInput) {
    juce::var document = validDocument();
    const juce::String before = juce::JSON::toString(document);

    ASSERT_TRUE(validateTimeline(document, graph).ok);
    EXPECT_EQ(juce::JSON::toString(document), before) << "the gate inspects, it never repairs";

    // Nor the doc it was serialised from, nor the graph.
    EXPECT_EQ(doc.getTracks().size(), 1u);
    EXPECT_EQ(graph.getNumNodes(), 2);
}

// =============================================================================
// 2. Every rejection lands in its own bucket
// =============================================================================

TEST_F(TimelineValidatorTest, EachMalformedDocumentYieldsItsExactError) {
    for (const auto& testCase : makeCases()) {
        juce::var document = validDocument();
        testCase.breakIt(document);

        const auto result = validateTimeline(document, graph);

        EXPECT_FALSE(result.ok) << "expected rejection: " << testCase.what;
        EXPECT_EQ(result.error, testCase.expected)
            << "case: " << testCase.what << "\n  expected " << synth::timelineValidationErrorName(testCase.expected)
            << " but got " << synth::timelineValidationErrorName(result.error) << " (\"" << result.message << "\")";
        EXPECT_TRUE(result.message.isNotEmpty()) << "case: " << testCase.what << " — a rejection must explain itself";
    }
}

// Guards the table against the enum growing underneath it: a new TimelineValidationError with no
// case here fails this test rather than quietly going unexercised.
TEST_F(TimelineValidatorTest, EveryErrorValueIsCovered) {
    std::set<TimelineValidationError> covered;
    for (const auto& testCase : makeCases())
        covered.insert(testCase.expected);

    for (int i = 0; i <= 63; ++i) {
        const auto value = static_cast<TimelineValidationError>(i);
        const auto name = synth::timelineValidationErrorName(value);
        if (name == "Unknown")
            break;
        if (value == TimelineValidationError::None)
            continue;

        EXPECT_TRUE(covered.count(value) > 0) << "TimelineValidationError::" << name << " has no case in makeCases()";
    }
}

TEST_F(TimelineValidatorTest, ErrorNamesAreDistinctAndNonEmpty) {
    std::set<juce::String> names;
    for (int i = 0; i <= 63; ++i) {
        const auto name = synth::timelineValidationErrorName(static_cast<TimelineValidationError>(i));
        if (name == "Unknown")
            break;
        EXPECT_TRUE(name.isNotEmpty());
        EXPECT_TRUE(names.insert(name).second) << "duplicate error name: " << name;
    }
    EXPECT_GT(names.size(), 10u);
}

// =============================================================================
// 3. The live parameter is the truth, not the lane's own range snapshot
// =============================================================================

// THE rule this validator turns on: a lane's RangeSnapshot is data the sender wrote, so it can
// neither authorise a value the parameter cannot take nor forbid one it can. A value inside the
// LIVE range is accepted even though the document's own snapshot says otherwise.
TEST_F(TimelineValidatorTest, ValueInsideTheLiveRangeButOutsideTheDeclaredSnapshotPasses) {
    juce::var document = validDocument();
    auto& lane = laneObj(document);

    juce::DynamicObject::Ptr narrowed = new juce::DynamicObject();
    narrowed->setProperty("minValue", 0.0);
    narrowed->setProperty("maxValue", 1.0);
    narrowed->setProperty("defaultValue", 0.0);
    lane.setProperty("range", juce::var(narrowed.get()));

    pointObj(document).setProperty("value", 5000.0); // far outside 0..1, comfortably inside 20..20000

    const auto result = validateTimeline(document, graph);
    EXPECT_TRUE(result.ok) << "the live cutoff range (20..20000) decides, not the document's claim of 0..1: "
                           << result.message;
}

// The mirror image: a value the document's own (wide) snapshot would happily hold is still
// rejected when the real parameter cannot take it.
TEST_F(TimelineValidatorTest, ValueOutsideTheLiveRangeIsRejectedHoweverWideTheSnapshotClaimsToBe) {
    juce::var document = validDocument();
    auto& lane = laneObj(document);

    juce::DynamicObject::Ptr widened = new juce::DynamicObject();
    widened->setProperty("minValue", -1.0e6);
    widened->setProperty("maxValue", 1.0e6);
    widened->setProperty("defaultValue", 0.0);
    lane.setProperty("range", juce::var(widened.get()));

    pointObj(document).setProperty("value", 500000.0);

    const auto result = validateTimeline(document, graph);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, TimelineValidationError::ValueOutOfParamRange);
    EXPECT_TRUE(result.message.contains("cutoff")) << "the message must name the parameter: " << result.message;
}

// Both ends are bounded, and the bounds themselves are inclusive.
TEST_F(TimelineValidatorTest, ValueBelowTheLiveMinimumIsRejectedAndTheBoundsAreInclusive) {
    juce::var atBounds = validDocument();
    pointObj(atBounds, 0).setProperty("value", kCutoffMin);
    pointObj(atBounds, 1).setProperty("value", kCutoffMax);
    EXPECT_TRUE(validateTimeline(atBounds, graph).ok) << "a value exactly at the parameter's limit is legal";

    juce::var belowMin = validDocument();
    pointObj(belowMin).setProperty("value", kCutoffMin - 1.0);
    const auto result = validateTimeline(belowMin, graph);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, TimelineValidationError::ValueOutOfParamRange);
}

// =============================================================================
// 4. Untrusted-strict: refused where the trusted loader repairs
// =============================================================================

// TimelineDoc::addBreakpoint and fromVar both CLAMP tension into [-1, 1]. Untrusted input gets no
// such courtesy — a value we would have to correct is a value the sender did not mean.
TEST_F(TimelineValidatorTest, OutOfRangeTensionIsRejectedRatherThanClamped) {
    juce::var document = validDocument();
    pointObj(document).setProperty("tension", 4.0);

    const auto result = validateTimeline(document, graph);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, TimelineValidationError::MalformedRoot);

    // The loader on its own would have taken it (clamped to 1) — which is exactly why the gate
    // has to run first.
    TimelineDoc target;
    EXPECT_TRUE(target.fromVar(document)) << "pins the difference this gate exists to make";
}

// Off (0) is the one other mode untrusted input may name: it makes a lane inert, it cannot capture
// anything.
TEST_F(TimelineValidatorTest, RecordModeOffAndReadAreBothAccepted) {
    for (const int mode :
         {static_cast<int>(synth::LaneRecordMode::Off), static_cast<int>(synth::LaneRecordMode::Read)}) {
        juce::var document = validDocument();
        laneObj(document).setProperty("recordMode", mode);
        EXPECT_TRUE(validateTimeline(document, graph).ok) << "record mode " << mode << " must be accepted";
    }

    for (const int mode :
         {static_cast<int>(synth::LaneRecordMode::Latch), static_cast<int>(synth::LaneRecordMode::Write), 99}) {
        juce::var document = validDocument();
        laneObj(document).setProperty("recordMode", mode);
        const auto result = validateTimeline(document, graph);
        EXPECT_FALSE(result.ok) << "record mode " << mode << " must be refused";
        EXPECT_EQ(result.error, TimelineValidationError::RecordModeNotAllowed);
    }
}

// An audio TRACK is a legal shape (it is just an empty row until the app records into it); what is
// refused is the asset itself, on whichever kind of track the clip sits.
TEST_F(TimelineValidatorTest, AudioTrackKindIsLegalButItsAssetsAreNot) {
    juce::var assetFree = validDocument();
    trackObj(assetFree).setProperty("kind", static_cast<int>(TrackKind::Audio));
    EXPECT_TRUE(validateTimeline(assetFree, graph).ok) << "an audio track with no asset-bearing clip is fine";

    juce::var withAsset = validDocument();
    trackObj(withAsset).setProperty("kind", static_cast<int>(TrackKind::Audio));
    clipObj(withAsset).setProperty("assetRef", "Audio/take-1.wav");
    const auto result = validateTimeline(withAsset, graph);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, TimelineValidationError::AssetNotAllowed);

    // Even a bundle-relative path TimelineDoc itself would accept: this rule is stricter than
    // isValidAssetRef, not the same rule restated.
    EXPECT_TRUE(TimelineDoc::isValidAssetRef("Audio/take-1.wav"));
}

TEST_F(TimelineValidatorTest, AutomationTrackKindIsAccepted) {
    juce::var document = validDocument();
    trackObj(document).setProperty("kind", static_cast<int>(TrackKind::Automation));
    EXPECT_TRUE(validateTimeline(document, graph).ok);
}

// =============================================================================
// 5. The two-door model: this gate opening does not open the patch grammar
// =============================================================================

// The "timeline" key's refusal on the patch grammar is permanent. A timeline this very validator
// accepts must STILL be refused when
// it is smuggled in as a patch property — the doors are separate, and only one of them is open.
TEST_F(TimelineValidatorTest, PatchGrammarStillRefusesTimelineData) {
    const juce::var document = validDocument();
    ASSERT_TRUE(validateTimeline(document, graph).ok) << "the very same data this gate accepts";

    juce::DynamicObject::Ptr patch = new juce::DynamicObject();
    patch->setProperty("nodes", juce::Array<juce::var>());
    patch->setProperty("connections", juce::Array<juce::var>());
    patch->setProperty("timeline", document);

    juce::AudioProcessorGraph emptyGraph;
    const auto result = synth::AIStateMapper::validatePatch(juce::var(patch.get()), emptyGraph,
                                                            /*clearExisting=*/true, /*trusted=*/false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, synth::PatchValidationError::TimelineNotAllowed)
        << "timeline data never rides the patch grammar, however valid it is on its own";
}
