// Tools/ui-verify/fixtures/cross-track-lanes.agsproj: the shared saved project (three audio tracks in channel macros,
// one folded, one nested, one open; Tests/fixtures/saved-project) with a Compressor lane on each track and an LFO on
// Audio 1's Ratio lane, for the scenarios that move a modulator row to another track (by drag and by Alt+Down).
// One test proves the committed fixture still loads with those lanes and the LFO's one routing; a disabled one
// regenerates it through the app's real save path:
//   build-ci-local/Tests/Tests --gtest_also_run_disabled_tests
//   --gtest_filter=UiVerifyCrossTrackLanesFixture.DISABLED_WritesCrossTrackLanesProject
// then minify its project.json to one line (the file-size guard caps a committed file at 1,000 lines):
//   python3 -c "import json,sys;p=sys.argv[1];open(p,'w').write(json.dumps(json.load(open(p)),separators=(',',':')))" \
//     Tools/ui-verify/fixtures/cross-track-lanes.agsproj/project.json

#include "../fixtures/saved-project/SavedProjectFixture.h"
#include "ProjectBundle.h"
#include "TestPaths.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

namespace {

juce::File fixtureBundle() {
    return juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("cross-track-lanes.agsproj");
}

} // namespace

TEST(UiVerifyCrossTrackLanesFixture, DISABLED_WritesCrossTrackLanesProject) {
    using synth::test::SavedProjectFixture;
    SavedProjectFixture project;
    auto mc = project.open();
    ASSERT_NE(mc, nullptr);
    auto& doc = mc->getTimelineDoc();
    const auto& tracks = doc.getTracks();
    ASSERT_EQ(tracks.size(), 3u);
    const synth::AutomationLane::RangeSnapshot range{0.0f, 1.0f, 0.5f};
    juce::String compressor[3];
    for (int t = 0; t < 3; ++t)
        compressor[t] = SavedProjectFixture::memberUuidOfType(*mc, SavedProjectFixture::trackMacroId(*mc, t), "Compr");
    doc.addLane(tracks[0].id, compressor[0], "ratio", range);
    doc.addLane(tracks[0].id, compressor[0], "attack", range);
    doc.addLane(tracks[1].id, compressor[1], "threshold", range);
    doc.addLane(tracks[2].id, compressor[2], "threshold", range);
    ASSERT_TRUE(mc->trackHeaderHostForTest().addNewModulator(compressor[0], "ratio", "LFO", 0).isNotEmpty());

    const auto bundle = fixtureBundle();
    bundle.deleteRecursively();
    bundle.getParentDirectory().createDirectory();
    ASSERT_TRUE(mc->saveProjectForTest(bundle));
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));
}

TEST(UiVerifyCrossTrackLanesFixture, TheCommittedProjectHoldsALaneOnEachTrackAndAnLfoOnAudio1sRatio) {
    const auto bundle = fixtureBundle();
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle)) << bundle.getFullPathName();
    MainComponent mc{std::make_unique<synth::test::SavedProjectFixtureProvider>()};
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_TRUE(mc.openProjectForTest(bundle));
    const auto& tracks = mc.getTimelineDoc().getTracks();
    ASSERT_EQ(tracks.size(), 3u);
    const auto compressor = synth::test::SavedProjectFixture::memberUuidOfType(
        mc, synth::test::SavedProjectFixture::trackMacroId(mc, 0), "Compr");
    const auto rows = mc.trackHeaderHostForTest().getModulators(compressor, "ratio");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows.front().isLfo);
    for (const auto& track : tracks)
        EXPECT_FALSE(track.lanes.empty()) << track.name;
}
