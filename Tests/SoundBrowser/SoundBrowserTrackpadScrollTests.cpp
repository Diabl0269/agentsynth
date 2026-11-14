// The Sounds list under a trackpad swipe: an even step per display frame at 60 and 120 Hz with one move per frame, a
// fling into the top stops there without moving back, and a swipe into the top it sits on moves nothing.
#include "../App/MainComponent/MainComponentTestFixture.h"
#include "../UI/Layout/TrackpadSwipeHarness.h"
#include "../UI/Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"

using namespace sound_browser_test;
using namespace trackpad_test;
using synth::ui::SoundBrowserComponent;

namespace {

struct ListSurface {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;
    Surface s;
    ListSurface() {
        tree.fill();
        for (int i = 0; i < 24; ++i)
            writeWav(tree.packs.getChildFile("core/Drums/hit" + juce::String(i) + ".wav"), 0.2);
        index = makeIndex(tree);
        index->setUserFolders({tree.user});
        scanAndWait(*index);
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setAnimateOffscreenForTest(true);
        view->showLocation("core", "Drums", 0);
        // The shortest column where the list still overflows by two rows, so the scroll bar shows.
        for (int h = 120; h <= 900; h += 10) {
            view->setSize(300, h);
            if (view->listArea().getHeight() > 0 &&
                (int)view->getRows().size() * SoundBrowserComponent::kRowHeight >
                    view->listArea().getHeight() + 2 * SoundBrowserComponent::kRowHeight)
                break;
        }
        view->completeFilterMotionForTest();
        view->completeListAnimationForTest();
        view->getWheelForTest().setOffScreenForTest(true);
        s.router = &view->getWheelForTest();
        s.axis = 1;
        s.quantum = 1.0;
        s.send = [this](float d, bool inertial) {
            juce::MouseWheelDetails w{};
            w.deltaY = d;
            w.isSmooth = true;
            w.isInertial = inertial;
            view->mouseWheelMove(makeClickEvent(*view, {100.0f, 60.0f}), w);
        };
        s.position = [this] { return (double)view->getScrollOffset(); };
    }
};

} // namespace

TEST(SoundBrowserTrackpadScrollTest, ListScrollsEvenlyOncePerFrame) {
    std::unique_ptr<ListSurface> f;
    checkEvenAtBothRates([&]() -> Surface& {
        f = std::make_unique<ListSurface>();
        return f->s;
    });
}

TEST(SoundBrowserTrackpadScrollTest, FlingIntoTheTopStopsWithoutBouncing) {
    ListSurface f;
    checkCoastsToAStopAtTheEnd(f.s);
}

TEST(SoundBrowserTrackpadScrollTest, SwipeAgainstTheTopMovesNothing) {
    ListSurface f;
    checkNoMoveNoNotification(f.s);
}
