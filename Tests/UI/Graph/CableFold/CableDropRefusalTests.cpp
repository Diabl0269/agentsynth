// A cable released on a jack that cannot take it makes no cable and says why through GraphEditor::onDropRefused
// (a toast with error severity once the main window wires it). Every drag goes through the editor's drag entry points,
// the same calls a mouse press, drag and release make.
//
// Kinds: silent-disabled (every refusal the drop code can produce reaches the hook, none is swallowed).

#include "../CardBody/CardCanvas.h"
#include "Modules/OscillatorModule.h"

using namespace cardbody_test;

namespace {

struct RefusalRig {
    CardCanvas canvas;
    NodeID aId, bId;
    ModuleComponent* a = nullptr;
    ModuleComponent* b = nullptr;
    std::vector<juce::String> refusals;
    std::vector<juce::String> statuses;

    explicit RefusalRig(bool installHook = true) {
        aId = canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
        bId = canvas.add(std::make_unique<OscillatorModule>(), 600, 0);
        canvas.editor.updateComponents();
        a = canvas.card(aId);
        b = canvas.card(bId);
        if (installHook)
            canvas.editor.onDropRefused = [this](const juce::String& reason) { refusals.push_back(reason); };
        canvas.editor.onStatusMessage = [this](const juce::String& message) { statuses.push_back(message); };
    }

    juce::Point<int> audioOut(ModuleComponent& card) { return card.localPointToGlobal(card.getPortCenter(0, false)); }
    juce::Point<int> audioIn(ModuleComponent& card) {
        return card.localPointToGlobal(card.getPortCenter(card.drawnInputJackIndices().front(), true));
    }
    juce::Point<int> midiIn(ModuleComponent& card) { return card.localPointToGlobal(card.getMidiPortCenter(false)); }

    void drag(ModuleComponent& from, int index, bool fromInput, juce::Point<int> start, juce::Point<int> end) {
        canvas.editor.beginConnectionDrag(&from, index, fromInput, false, start);
        canvas.editor.dragConnection(end);
        canvas.editor.endConnectionDrag(end);
    }
    int connections() { return (int)canvas.engine.getGraph().getConnections().size(); }
};

} // namespace

TEST(CableDropRefusal, AnOutputDroppedOnAnOutputIsRefusedWithItsReason) {
    RefusalRig rig;
    rig.drag(*rig.a, 0, false, rig.audioOut(*rig.a), rig.audioOut(*rig.b));
    ASSERT_EQ(rig.refusals.size(), 1u);
    EXPECT_EQ(rig.refusals[0], "Connect an output to an input");
    EXPECT_EQ(rig.connections(), 0) << "no cable was made";
    EXPECT_TRUE(rig.statuses.empty()) << "the reason goes to the refusal hook, not the status bar";
}

TEST(CableDropRefusal, AudioDroppedOnAMidiInputIsRefusedWithItsReason) {
    RefusalRig rig;
    rig.drag(*rig.a, 0, false, rig.audioOut(*rig.a), rig.midiIn(*rig.b));
    ASSERT_EQ(rig.refusals.size(), 1u);
    EXPECT_TRUE(rig.refusals[0].startsWith("MIDI jacks connect only to MIDI jacks"));
    EXPECT_EQ(rig.connections(), 0);
}

TEST(CableDropRefusal, AnInputDroppedOnAnInputIsRefusedWithItsReason) {
    RefusalRig rig;
    rig.drag(*rig.a, rig.a->drawnInputJackIndices().front(), true, rig.audioIn(*rig.a), rig.audioIn(*rig.b));
    ASSERT_EQ(rig.refusals.size(), 1u);
    EXPECT_EQ(rig.refusals[0], "Connect an output to an input");
    EXPECT_EQ(rig.connections(), 0);
}

TEST(CableDropRefusal, AGoodDropMakesACableAndRefusesNothing) {
    RefusalRig rig;
    rig.drag(*rig.a, 0, false, rig.audioOut(*rig.a), rig.audioIn(*rig.b));
    EXPECT_TRUE(rig.refusals.empty());
    EXPECT_GT(rig.connections(), 0);
}

// Guard for the kind: each refusal the drop code can produce reaches the hook with a non-empty reason, and the hook is
// where the status-bar fallback lives (without an owner's hook the reason still reaches the status message).
TEST(CableDropRefusal, EveryRefusalReachesTheHookAndTheDefaultFallsBackToTheStatusBar) {
    struct Case {
        const char* name;
        std::function<void(RefusalRig&)> drop;
    };
    const std::vector<Case> cases = {
        {"output on output", [](RefusalRig& r) { r.drag(*r.a, 0, false, r.audioOut(*r.a), r.audioOut(*r.b)); }},
        {"audio on MIDI", [](RefusalRig& r) { r.drag(*r.a, 0, false, r.audioOut(*r.a), r.midiIn(*r.b)); }},
        {"input on input",
         [](RefusalRig& r) {
             r.drag(*r.a, r.a->drawnInputJackIndices().front(), true, r.audioIn(*r.a), r.audioIn(*r.b));
         }},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.name);
        RefusalRig rig;
        c.drop(rig);
        ASSERT_EQ(rig.refusals.size(), 1u);
        EXPECT_TRUE(rig.refusals[0].isNotEmpty());
        EXPECT_EQ(rig.connections(), 0);

        RefusalRig fallback(false); // the editor's own default
        c.drop(fallback);
        EXPECT_EQ(fallback.statuses.size(), 1u);
    }
}
