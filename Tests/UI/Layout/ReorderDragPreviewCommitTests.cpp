// ReorderDragPreviewCommitTests.cpp -- one real-mouse test per reorder drag: the order drawn after the LAST drag event
// is the order the drop commits (the dock tabs, the mixer columns, a strip's send rows, the timeline's track rows).
// The drag goes through the component's own mouse handlers (ReorderDragPreviewCommit.h); the drawn order is read from
// where the items stand, the committed order from the model the drop wrote. The other reorder drags have the same test
// next to their rigs: CardLayoutEditorGripPreviewCommitTests.cpp and AutomationLanesReorderPreviewCommitTests.cpp.
#include "../../TestSettingsHelpers.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "BottomDockActiveTabResetGuard.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "ReorderDragPreviewCommit.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>

#include <algorithm>

namespace {

using namespace reorder_preview_commit;
using Pt = juce::Point<float>;

class MockProviderRDPC : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockRDPC"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

// ---- Dock tabs ----------------------------------------------------------------------------------------------------

using DockTab = synth::ui::BottomDockComponent::Tab;

struct DockRig {
    synth::test::PersistedKeysGuard keys{juce::StringArray{"bottomDockTabOrder", "bottomDockActiveTab"}};
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderRDPC>()};

    DockRig() {
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        mc.simulateToggleBottomPanelClick();
    }
    synth::ui::BottomDockComponent& dock() { return mc.getBottomDock(); }
};

// Drags the tab `pressed` so that the lifted tab sits on slot `toSlot`, and compares the strip's drawn order with
// the saved one. The tab buttons are the same objects before and after the drop.
void dragDockTab(DockRig& rig, int fromSlot, int toSlot) {
    auto& dock = rig.dock();
    const auto order = dock.getTabOrderForTest();
    const auto buttons = dock.getTabButtons();
    ASSERT_EQ(buttons.size(), order.size());
    std::vector<juce::Component*> offeredButtons; // the tabs the strip offers: the others keep stale bounds
    for (const auto& strip : dock.getStripTabs())
        offeredButtons.push_back(strip.button);
    std::vector<std::pair<DockTab, juce::Component*>> tabs; // in slot order, before the drag
    for (size_t i = 0; i < order.size(); ++i)
        if (std::find(offeredButtons.begin(), offeredButtons.end(), buttons[i]) != offeredButtons.end())
            tabs.emplace_back(order[i], buttons[i]);
    ASSERT_GE(tabs.size(), 3u) << "the strip offers at least three tabs";
    ASSERT_LT(std::max(fromSlot, toSlot), (int)tabs.size());
    juce::Component* button = tabs[(size_t)fromSlot].second;

    const float grab = 10.0f;
    const Pt press((float)button->getX() + grab, 8.0f);
    const auto slot = tabs[(size_t)toSlot].second->getBounds();
    const Pt drop((float)slot.getX() + grab, 8.0f); // the lifted tab's left edge on the slot's left edge
    const auto local = [&](Pt p) { return button->getLocalPoint(&dock, p); };

    Probe<DockTab> probe;
    probe.send = [&](Phase phase, Pt p) {
        if (phase == Phase::Down)
            button->mouseDown(makeClickEvent(*button, local(p)));
        else if (phase == Phase::Drag)
            button->mouseDrag(makeDragEvent(*button, local(p), local(press)));
        else
            button->mouseUp(makeClickEvent(*button, local(p)));
    };
    probe.shown = [&] {
        std::vector<std::pair<DockTab, int>> items;
        for (const auto& [tab, c] : tabs)
            items.emplace_back(tab, c->getX());
        return orderedByPosition(items);
    };
    probe.committed = [&] { // the offered tabs, in saved order (main-view and detached tabs keep their slots)
        std::vector<DockTab> saved;
        for (const DockTab tab : dock.getTabOrderForTest())
            for (const auto& [t, c] : tabs)
                if (t == tab)
                    saved.push_back(tab);
        return saved;
    };

    std::vector<DockTab> before;
    for (const auto& [tab, c] : tabs)
        before.push_back(tab);
    expectCommitsWhatItPreviewed(dragAndRelease(probe, press, straightPath(press, drop)), before);
}

// ---- Mixer columns ------------------------------------------------------------------------------------------------

constexpr int kColumnPitch = 144; // column width 140 + gap 4

struct ColumnRig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderRDPC>()};
    synth::ui::MixerPanelComponent* panel = nullptr;

    explicit ColumnRig(int tracks) {
        mc.setSize(1400, 900);
        mc.getBottomDock().getMixerPanel().setVisible(true); // a hidden mixer defers its rebuilds
        mc.newPatchForTest();
        for (int i = 0; i < tracks; ++i)
            mc.simulateAddAudioTrackClick();
        panel = &mc.getBottomDock().getMixerPanel();
        panel->setSize(1400, 300);
        panel->rebuild();
    }

    std::vector<unsigned> columnNodes(int count) {
        std::vector<unsigned> ids;
        for (int i = 0; i < count; ++i)
            ids.push_back((unsigned)panel->getStripColumnForTest(i)->getNodeId().uid);
        return ids;
    }
};

// Presses column `from`'s header and drags so the lifted column's left edge lands on slot `to`.
void dragColumn(ColumnRig& rig, int count, int from, int to) {
    std::vector<std::pair<unsigned, synth::ui::MixerColumnComponent*>> columns;
    for (int i = 0; i < count; ++i) {
        auto* column = rig.panel->getStripColumnForTest(i);
        columns.emplace_back((unsigned)column->getNodeId().uid, column);
    }
    auto* pressed = columns[(size_t)from].second;
    auto& header = pressed->getHeaderForTest();
    auto& content = *pressed->getParentComponent();
    const float pressX = (float)(pressed->getX() + header.getX() + 3);
    const float grab = pressX - (float)pressed->getX();
    const Pt press(pressX, 12.0f);
    const Pt drop((float)(to * kColumnPitch) + grab, 12.0f);
    const auto local = [&](Pt p) { return header.getLocalPoint(&content, p); };

    Probe<unsigned> probe;
    probe.send = [&](Phase phase, Pt p) {
        if (phase == Phase::Down)
            header.mouseDown(makeClickEvent(header, local(p)));
        else if (phase == Phase::Drag)
            header.mouseDrag(makeDragEvent(header, local(p), local(press)));
        else
            header.mouseUp(makeClickEvent(header, local(p)));
    };
    probe.shown = [&] {
        std::vector<std::pair<unsigned, int>> items;
        for (const auto& [id, column] : columns)
            items.emplace_back(id, column->getX());
        return orderedByPosition(items);
    };
    probe.committed = [&] {
        columns.clear(); // the drop rebuilds the columns
        return rig.columnNodes(count);
    };

    std::vector<unsigned> before;
    for (const auto& [id, column] : columns)
        before.push_back(id);
    const auto outcome = dragAndRelease(probe, press, straightPath(press, drop));
    expectCommitsWhatItPreviewed(outcome, before);
}

// ---- Send rows ----------------------------------------------------------------------------------------------------

struct SendRig {
    std::unique_ptr<MainComponent> mc;
    juce::AudioProcessorGraph::NodeID buses[3];

    SendRig() {
        mc = std::make_unique<MainComponent>(std::make_unique<MockProviderRDPC>());
        mc->setSize(1400, 900);
        mc->getBottomDock().getMixerPanel().setVisible(true);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->newPatchForTest();
        mc->simulateAddAudioTrackClick();
        for (auto& bus : buses)
            bus = panel().createBus();
        panel().rebuild();
        // The column is re-fetched between sends: each one rebuilds every column.
        for (const auto& bus : buses)
            column()->getSendListForTest().addSendTo(bus);
        panel().rebuild();
        panel().setSize(1400, 300);
        panel().resized();
    }
    synth::ui::MixerPanelComponent& panel() { return mc->getBottomDock().getMixerPanel(); }
    synth::ui::MixerColumnComponent* column() { return panel().getStripColumnForTest(0); }

    std::vector<unsigned> sendTargets() {
        std::vector<unsigned> ids;
        auto* col = column();
        for (int slot = 0; slot < col->getSendListForTest().getRowCountForTest(); ++slot)
            ids.push_back((unsigned)synth::findSendTarget(mc->getAudioEngine().getGraph(), col->getNodeId(), slot).uid);
        return ids;
    }
};

// Presses send row `from`'s name and drags so the lifted row's top lands on row `to`'s slot.
void dragSendRow(SendRig& rig, int from, int to) {
    auto& list = rig.column()->getSendListForTest();
    const auto before = rig.sendTargets();
    ASSERT_EQ(before.size(), 3u);
    const float kNameX = 5.0f; // left of the M button, the knobs and the PRE/POST toggle
    const float grab = (float)list.getKnobForTest(from)->getBounds().getCentreY() - (float)list.getRowTopForTest(from);
    const Pt press(kNameX, (float)list.getRowTopForTest(from) + grab);
    const Pt drop(kNameX, (float)list.getRowTopForTest(to) + grab);
    const auto event = [&](Pt p, bool dragged) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, &list, &list, juce::Time::getCurrentTime(), press, juce::Time::getCurrentTime(),
                                1, dragged);
    };

    Probe<unsigned> probe;
    probe.send = [&](Phase phase, Pt p) {
        if (phase == Phase::Down)
            list.mouseDown(event(p, false));
        else if (phase == Phase::Drag)
            list.mouseDrag(event(p, true));
        else
            list.mouseUp(event(p, true));
    };
    probe.shown = [&] {
        std::vector<std::pair<unsigned, int>> items;
        for (int row = 0; row < (int)before.size(); ++row)
            items.emplace_back(before[(size_t)row], list.getRowTopForTest(row));
        return orderedByPosition(items);
    };
    probe.committed = [&] {
        rig.panel().rebuild();
        return rig.sendTargets();
    };
    const auto outcome = dragAndRelease(probe, press, straightPath(press, drop));
    expectCommitsWhatItPreviewed(outcome, before);
}

// ---- Track rows ---------------------------------------------------------------------------------------------------

struct TrackRig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderRDPC>()};
    synth::ui::TimelinePanelComponent* panel = nullptr;

    explicit TrackRig(int tracks) {
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        for (int i = 0; i < tracks; ++i)
            mc.simulateAddAudioTrackClick();
        panel = &mc.getTimelinePanel();
        panel->setSize(1400, 700); // tall: the pointer stays clear of the autoscroll edge zones
    }

    std::vector<synth::TrackId> trackOrder() {
        std::vector<synth::TrackId> ids;
        for (const auto& track : mc.getTimelineDoc().getTracks())
            ids.push_back(track.id);
        return ids;
    }
};

// Presses track row `from` and drags so the lifted row's top lands on row `to`'s slot.
void dragTrackRow(TrackRig& rig, int from, int to) {
    const auto before = rig.trackOrder();
    std::vector<std::pair<synth::TrackId, synth::ui::TimelineTrackHeaderComponent*>> rows;
    for (int i = 0; i < (int)before.size(); ++i)
        rows.emplace_back(before[(size_t)i], rig.panel->getTrackHeaderAt(i));
    auto* row = rows[(size_t)from].second;
    auto& list = rig.panel->getTrackHeaderListForTest();
    const int rowHeight = row->getHeight();
    const float grab = 10.0f;
    const Pt press(3.0f, (float)row->getY() + grab);
    const Pt drop(3.0f, (float)(to * rowHeight) + grab);
    const auto local = [&](Pt p) { return row->getLocalPoint(&list, p); };

    Probe<synth::TrackId> probe;
    probe.send = [&](Phase phase, Pt p) {
        if (phase == Phase::Down)
            row->mouseDown(makeClickEvent(*row, local(p)));
        else if (phase == Phase::Drag)
            row->mouseDrag(makeDragEvent(*row, local(p), local(press)));
        else
            row->mouseUp(makeClickEvent(*row, local(p)));
    };
    probe.shown = [&] {
        std::vector<std::pair<synth::TrackId, int>> items;
        for (const auto& [id, header] : rows)
            items.emplace_back(id, header->getY());
        return orderedByPosition(items);
    };
    probe.committed = [&] { return rig.trackOrder(); };
    const auto outcome = dragAndRelease(probe, press, straightPath(press, drop));
    expectCommitsWhatItPreviewed(outcome, before);
}

} // namespace

TEST(ReorderDragPreviewCommit, ADockTabDropCommitsTheOrderTheLastDragEventShowed) {
    DockRig rig;
    dragDockTab(rig, 0, 2); // the first tab right past two others
    dragDockTab(rig, 2, 0); // and one back to the front
}

TEST(ReorderDragPreviewCommit, AMixerColumnDropCommitsTheOrderTheLastDragEventShowed) {
    ColumnRig rig(4);
    dragColumn(rig, 4, 0, 2);
    dragColumn(rig, 4, 3, 1);
}

TEST(ReorderDragPreviewCommit, ASendRowDropCommitsTheOrderTheLastDragEventShowed) {
    SendRig rig;
    dragSendRow(rig, 0, 2);
    dragSendRow(rig, 2, 1);
}

TEST(ReorderDragPreviewCommit, ATrackRowDropCommitsTheOrderTheLastDragEventShowed) {
    TrackRig rig(4);
    dragTrackRow(rig, 0, 2);
    dragTrackRow(rig, 3, 1);
}
