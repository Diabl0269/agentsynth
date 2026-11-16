// UndoGlideSurfacesController.cpp -- the registry fixture for the Controllers tab's surface: a controller cell dragged
// to another grid place glides back on undo and forward on redo (docs/layout/undo-glide.md).
#include "../../TestWait.h"
#include "../MidiRemote/ControllerSurfaceTestHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "Modules/FilterModule.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/MidiRemote/MidiRemotePanel/MidiRemotePanelComponent.h"
#include "UndoGlideSurfaces.h"

namespace undo_glide_test {

namespace {

using midiremote_surface_test::DragDriver;
using synth::ui::ControllerSurfaceComponent;

/** Knob 1 dragged three columns right through the real mouse path; the panel refreshes after the history step. */
class ControllerCellSurface : public Surface {
public:
    ControllerCellSurface() {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-undoglide-controller-" + juce::Uuid().toString());
        engine_ = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
        editor_ = std::make_unique<GraphEditor>(*engine_);
        controller_ = std::make_unique<synth::midi::MidiLearnController>(
            *engine_, *editor_, remote_, doc_, undo_, statusBar_, synth::ControllerProfileStore(root_));
        panel_.configure(*engine_, remote_, *controller_, doc_, *editor_);
        controller_->onChanged = [this] { panel_.scheduleLiveRefresh(); };

        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.controls = {knob("k1", 100, 0, 0), knob("k2", 101, 4, 4)};
        controller_->addProfile(profile);
        panel_.selectForTest("p1", {});
        synth::test::flushMessageQueue();

        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
        surface().forceMoveGlideAnimateForTest(true);
        surface().getMoveGlideOwnerForTest().setSize(500, 400); // the walkers render the painted component whole
    }
    ~ControllerCellSurface() override {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        remote_.endAllGestures();
        root_.deleteRecursively();
    }

    void move() override {
        auto* cell = surface().findCellForTest("k1");
        DragDriver driver(*cell);
        driver.stepToOffset(2, 0);
        driver.stepToOffset(3, 0);
        driver.end();
        synth::test::flushMessageQueue();
    }
    bool undo() override { return step(controller_->undoProfileEdit()); }
    bool redo() override { return step(controller_->redoProfileEdit()); }
    double drawn() const override { return surface().drawnCellBoundsForTest("k1").getX(); }
    bool gliding() const override { return surface().isMoveGlidingForTest(); }
    void stepTo(double elapsedMs) override { surface().stepMoveGlideForTest(elapsedMs); }
    float alpha() const override { return surface().drawnCellAlphaForTest("k1"); }
    juce::Component* paintedComponent() override { return &surface().getMoveGlideOwnerForTest(); }

private:
    static synth::Control knob(const juce::String& id, int cc, int col, int row) {
        synth::Control c;
        c.id = id;
        c.name = id;
        c.message.type = synth::MessageType::cc;
        c.message.channel = 1;
        c.message.number = cc;
        c.layout.col = col;
        c.layout.row = row;
        return c;
    }
    bool step(bool did) {
        synth::test::flushMessageQueue(); // the panel's deferred refresh of the surface
        return did;
    }
    ControllerSurfaceComponent& surface() const {
        return const_cast<ControllerCellSurface*>(this)->panel_.getSurfaceForTest();
    }

    juce::File root_;
    synth::midi::RemoteEngine remote_;
    synth::MidiRemoteProjectDoc doc_;
    AppUndoManager undo_;
    StatusBarComponent statusBar_;
    std::unique_ptr<AudioEngine> engine_;
    std::unique_ptr<GraphEditor> editor_;
    std::unique_ptr<synth::midi::MidiLearnController> controller_;
    synth::ui::MidiRemotePanelComponent panel_;
};

} // namespace

std::unique_ptr<Surface> makeControllerSurfaceCellMove() { return std::make_unique<ControllerCellSurface>(); }

} // namespace undo_glide_test
