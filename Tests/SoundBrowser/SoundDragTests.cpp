// Dragging a Sound out of the Sounds view: the payload, the app-wide drag state, the drop adapter that lets a file-drop
// target take a Sound, and the view's own drag source.
#include "../App/MainComponent/MainComponentTestFixture.h"
#include "Plugin/Hosting/HostedPluginBackend.h"
#include "SnippetManager.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include "UI/Library/SoundBrowser/SoundDropAdapter.h"

using namespace sound_browser_test;
using synth::ui::SoundBrowserComponent;
using synth::ui::SoundDragState;

namespace {

// A component that takes .wav files only, like a card that refuses everything else, plus the adapter.
class WavOnlyTarget
    : public juce::Component
    , public juce::FileDragAndDropTarget
    , public synth::ui::SoundDropAdapter {
public:
    bool isInterestedInFileDrag(const juce::StringArray& files) override { return files[0].endsWith(".wav"); }
    void filesDropped(const juce::StringArray& files, int x, int y) override {
        dropped = files;
        droppedAt = {x, y};
    }
    void fileDragEnter(const juce::StringArray&, int, int) override { ++enters; }
    juce::StringArray dropped;
    juce::Point<int> droppedAt;
    int enters = 0;
};

struct CountingListener : SoundDragState::Listener {
    void soundDragChanged() override { ++calls; }
    int calls = 0;
};

juce::MouseEvent mouseAt(SoundBrowserComponent& v, juce::Point<int> at, juce::Point<int> downAt) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), {}, 0, 0, 0, 0, 0, &v, &v,
                            juce::Time::getCurrentTime(), downAt.toFloat(), juce::Time::getCurrentTime(), 1, true);
}

} // namespace

TEST(SoundDragPayloadTest, RoundTripsAPath) {
    const auto payload = synth::ui::makeSoundPayload("/packs/core/kick-808.wav");
    EXPECT_TRUE(synth::ui::isSoundPayload(payload));
    EXPECT_EQ(synth::ui::soundFileFromPayload(payload).getFullPathName(), "/packs/core/kick-808.wav");
}

TEST(SoundDragPayloadTest, RejectsEveryOtherDragKind) {
    EXPECT_FALSE(synth::ui::isSoundPayload("Oscillator"));
    EXPECT_FALSE(synth::ui::isSoundPayload(synth::SnippetManager::payloadForName("pad")));
    EXPECT_FALSE(synth::ui::isSoundPayload(juce::String(synth::PluginIdentity::kDragPayloadPrefix) + "VST3|1|Diva"));
    EXPECT_FALSE(synth::ui::isSoundPayload("sound:")) << "a prefix with no path is not a sound";
    EXPECT_FALSE(synth::ui::isSoundPayload(juce::var(42)));
    EXPECT_EQ(synth::ui::soundFileFromPayload("Oscillator"), juce::File());
}

TEST(SoundDragPayloadTest, AudioAndInstrumentSoundsAreToldApart) {
    EXPECT_TRUE(synth::ui::isAudioSound(juce::File("/a/b.wav")));
    EXPECT_FALSE(synth::ui::isAudioSound(juce::File("/a/b.sfz")));
    EXPECT_TRUE(synth::ui::isInstrumentSound(juce::File("/a/b.sfz")));
}

TEST(SoundDragStateTest, ListenersHearTheStartAndTheEndOnly) {
    CountingListener listener;
    SoundDragState::addListener(&listener);
    SoundDragState::end();
    EXPECT_EQ(listener.calls, 0) << "ending when nothing is dragged says nothing";
    SoundDragState::begin(juce::File("/a/b.wav"));
    EXPECT_TRUE(SoundDragState::isActive());
    EXPECT_EQ(SoundDragState::file(), juce::File("/a/b.wav"));
    SoundDragState::end();
    EXPECT_FALSE(SoundDragState::isActive());
    EXPECT_EQ(listener.calls, 2);
    SoundDragState::removeListener(&listener);
}

TEST(SoundDropAdapterTest, TakesASoundTheFileTargetWouldTakeAndPassesItAsAFileDrop) {
    WavOnlyTarget target;
    juce::DragAndDropTarget::SourceDetails wav(synth::ui::makeSoundPayload("/a/b.wav"), nullptr, {7, 9});
    juce::DragAndDropTarget::SourceDetails sfz(synth::ui::makeSoundPayload("/a/b.sfz"), nullptr, {7, 9});
    EXPECT_TRUE(target.isInterestedInDragSource(wav));
    EXPECT_FALSE(target.isInterestedInDragSource(sfz));
    target.itemDragEnter(wav);
    EXPECT_EQ(target.enters, 1);
    target.itemDropped(wav);
    EXPECT_EQ(target.dropped[0], "/a/b.wav");
    EXPECT_EQ(target.droppedAt, juce::Point<int>(7, 9));
}

TEST(SoundDropAdapterTest, NeverTakesAModuleSnippetOrPluginDrag) {
    WavOnlyTarget target;
    for (const auto& description : {juce::String("Oscillator"), synth::SnippetManager::payloadForName("pad"),
                                    juce::String(synth::PluginIdentity::kDragPayloadPrefix) + "VST3|1|Diva"}) {
        juce::DragAndDropTarget::SourceDetails details(description, nullptr, {1, 1});
        EXPECT_FALSE(target.isInterestedInDragSource(details)) << description;
    }
}

TEST(SoundDropAdapterTest, OutlineShowsOnlyWhileASoundItWouldTakeIsDragged) {
    WavOnlyTarget target;
    EXPECT_FALSE(target.isShowingSoundOutline());
    SoundDragState::begin(juce::File("/a/b.sfz"));
    EXPECT_FALSE(target.isShowingSoundOutline()) << "it would refuse an .sfz, so it shows nothing";
    SoundDragState::begin(juce::File("/a/b.wav"));
    EXPECT_TRUE(target.isShowingSoundOutline());
    SoundDragState::end();
    EXPECT_FALSE(target.isShowingSoundOutline());
}

namespace {
struct ViewFixture {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;
    ViewFixture() {
        tree.fill();
        index = makeIndex(tree);
        scanAndWait(*index);
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 700);
        view->showLocation("core", "Drums", 0);
        view->completeListAnimationForTest();
    }
    int soundRow() const {
        for (size_t i = 0; i < view->getRows().size(); ++i)
            if (view->getRows()[i].kind == SoundBrowserComponent::RowKind::Sound)
                return (int)i;
        return -1;
    }
};
} // namespace

TEST(SoundBrowserDragSourceTest, ASoundRowStartsADragAndTheStateEndsOnRelease) {
    ViewFixture f;
    const int row = f.soundRow();
    ASSERT_GE(row, 0);
    EXPECT_TRUE(f.view->beginSoundDrag(row));
    EXPECT_TRUE(f.view->isSoundDragging());
    EXPECT_TRUE(SoundDragState::isActive());
    EXPECT_EQ(SoundDragState::file().getFullPathName(), f.view->getRows()[(size_t)row].entry->path);
    f.view->mouseUp(mouseAt(*f.view, {5, 5}, {5, 5}));
    EXPECT_FALSE(SoundDragState::isActive());
    EXPECT_FALSE(f.view->isSoundDragging());
}

TEST(SoundBrowserDragSourceTest, OnlySoundRowsDrag) {
    ViewFixture f;
    f.view->showLocation("", "", 0);
    f.view->completeListAnimationForTest();
    for (size_t i = 0; i < f.view->getRows().size(); ++i)
        EXPECT_FALSE(f.view->beginSoundDrag((int)i));
    EXPECT_FALSE(SoundDragState::isActive());
}

TEST(SoundBrowserDragSourceTest, ThePressMustMovePastTheThresholdFirst) {
    ViewFixture f;
    const int row = f.soundRow();
    ASSERT_GE(row, 0);
    const auto at = f.view->getRowBounds(row).getCentre();
    f.view->mouseDown(mouseAt(*f.view, at, at));
    f.view->mouseDrag(mouseAt(*f.view, at + juce::Point<int>(2, 0), at));
    EXPECT_FALSE(f.view->isSoundDragging());
    f.view->mouseDrag(mouseAt(*f.view, at + juce::Point<int>(8, 0), at));
    EXPECT_TRUE(f.view->isSoundDragging());
    f.view->mouseUp(mouseAt(*f.view, at, at));
    EXPECT_FALSE(SoundDragState::isActive());
}
