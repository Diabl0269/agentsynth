#include "HostedOutputsPanel.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/HostedOutputs/HostedOutputsStrip.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/PopupHost.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kPad = 12;
constexpr int kTitleHeight = 22;
constexpr int kRowHeight = 30;
constexpr int kJoinWidth = 92;
constexpr int kFooterHeight = 26;
constexpr int kMainBus = 0;

// A bus's channels as the plugin numbers its outputs: "1-2", "3-4", "5".
juce::String channelRange(const HostedPluginModule::OutputBusInfo& bus) {
    const int first = bus.firstRawChannel + 1;
    return bus.numChannels <= 1 ? juce::String(first)
                                : juce::String(first) + "-" + juce::String(first + bus.numChannels - 1);
}

bool isMainBus(const HostedPluginModule::OutputBusInfo& bus) { return bus.busIndex == kMainBus; }

// The main bus joins and splits through Dual I/O; every other bus through the module.
void setBusJoined(HostedPluginModule& m, const HostedPluginModule::OutputBusInfo& bus, bool joined) {
    if (!isMainBus(bus)) {
        m.setOutputJoined(bus.busIndex, joined);
        return;
    }
    if (auto* dual = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&m, "dualIO")))
        dual->setValueNotifyingHost(joined ? 0.0f : 1.0f);
}

bool differs(const HostedPluginModule::OutputBusInfo& bus, int footerIndex) {
    switch (footerIndex) {
    case 0:
        return !isMainBus(bus) && !bus.shown; // Show all
    case 1:
        return !isMainBus(bus) && bus.shown; // Main only
    case 2:
        return bus.numChannels == 2 && !bus.joined; // Join all
    default:
        return bus.numChannels == 2 && bus.joined; // Split all
    }
}
} // namespace

void editHostedOutputs(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId,
                       const std::function<void(HostedPluginModule&)>& edit) {
    auto& graph = editor.getAudioEngine().getGraph();
    auto run = [&graph, nodeId, &edit] {
        if (auto* node = graph.getNodeForId(nodeId))
            if (auto* m = dynamic_cast<HostedPluginModule*>(node->getProcessor()))
                edit(*m);
    };
    const auto before = editor.snapshotCablesForRetract();
    if (auto* undo = editor.getUndoManager())
        undo->recordStructuralChange(graph, run);
    else
        run();
    editor.retractCablesGoneSince(before, false);
}

// ---- OutputShowToggle ------------------------------------------------------------------------------------------

OutputShowToggle::OutputShowToggle()
    : glide_(*this) {
    setClickingTogglesState(true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
}

void OutputShowToggle::setRow(const juce::String& name, const juce::String& channels, bool on, bool locked,
                              bool animate) {
    name_ = name;
    setComponentID("outputsShow:" + name);
    channels_ = channels;
    locked_ = locked;
    setEnabled(!locked);
    setToggleState(on, juce::dontSendNotification);
    glide_.retarget(on, animate);
    setTitle(locked ? name + " output, always shown" : "Show " + name + " output");
    setTooltip(locked ? "The main output is always shown"
                      : "Show or hide the jacks of " + name + " (channels " + channels + ")");
    repaint();
}

bool OutputShowToggle::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey) {
        if (isEnabled())
            triggerClick();
        return true;
    }
    return juce::ToggleButton::keyPressed(key);
}

void OutputShowToggle::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    const juce::Rectangle<float> switchArea(0.0f, ((float)getHeight() - SwitchGlide::kHeight) * 0.5f,
                                            (float)SwitchGlide::kWidth, (float)SwitchGlide::kHeight);
    const float fade = locked_ ? 0.55f : 1.0f;
    glide_.paint(
        g, switchArea,
        {colours.accent.withMultipliedAlpha(fade), colours.textMuted.withMultipliedAlpha(fade), colours.surface});
    const auto font = synth::theme::AppLookAndFeel::uiFont(13.0f);
    g.setFont(font);
    g.setColour((highlighted ? colours.textPrimary : colours.textPrimary.withMultipliedAlpha(0.92f)));
    const int textLeft = SwitchGlide::kWidth + SwitchGlide::kGapAfter;
    const int rangeWidth = 44;
    g.drawText(name_, textLeft, 0, getWidth() - textLeft - rangeWidth, getHeight(), juce::Justification::centredLeft,
               true);
    g.setColour(colours.textMuted);
    g.drawText(channels_, getWidth() - rangeWidth, 0, rangeWidth, getHeight(), juce::Justification::centredRight,
               false);
    paintFocusRing(g, getLocalBounds().toFloat().reduced(1.0f), *this, 3.0f);
}

// ---- HostedOutputsPanel ----------------------------------------------------------------------------------------

HostedOutputsPanel::HostedOutputsPanel(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId)
    : editor_(editor)
    , nodeId_(nodeId) {
    setWantsKeyboardFocus(false);
    title_.setInterceptsMouseClicks(false, false);
    title_.setFont(synth::theme::AppLookAndFeel::uiFont(15.0f));
    addAndMakeVisible(title_);

    static const char* const names[] = {"Show all", "Main only", "Join all", "Split all"};
    static const char* const tips[] = {"Show every output of the plugin", "Show only the main output and hide the rest",
                                       "Join every stereo output into one jack",
                                       "Split every stereo output into two jacks"};
    for (int i = 0; i < 4; ++i) {
        auto* button = footer_.add(new juce::TextButton(names[i]));
        button->setTitle(names[i]);
        button->setComponentID(juce::String("outputsFooter:") + names[i]);
        button->setTooltip(tips[i]);
        button->setWantsKeyboardFocus(true);
        button->setMouseClickGrabsKeyboardFocus(false);
        button->onClick = [this, i] { footerAction(i); };
        addAndMakeVisible(*button);
    }
    rebuildRows();
}

HostedPluginModule* HostedOutputsPanel::module() const {
    if (auto* node = editor_.getAudioEngine().getGraph().getNodeForId(nodeId_))
        return dynamic_cast<HostedPluginModule*>(node->getProcessor());
    return nullptr;
}

// One row per bus. Called when the set of buses changes (a different instance); a toggle only re-reads (refresh).
void HostedOutputsPanel::rebuildRows() {
    rows_.clear();
    auto* m = module();
    const int count = m != nullptr ? (int)m->getOutputBuses().size() : 0;
    for (int i = 0; i < count; ++i) {
        const auto bus = m->getOutputBuses()[(size_t)i];
        auto* row = rows_.add(new Row(bus.busIndex));
        addAndMakeVisible(row->show);
        row->show.onClick = [this, busIndex = bus.busIndex] {
            auto* mod = module();
            if (mod == nullptr)
                return;
            bool shown = false;
            for (const auto& b : mod->getOutputBuses())
                if (b.busIndex == busIndex)
                    shown = b.shown;
            editHostedOutputs(editor_, nodeId_,
                              [busIndex, shown](HostedPluginModule& hm) { hm.setOutputShown(busIndex, !shown); });
            refresh(true);
        };
        if (bus.numChannels == 2) {
            row->join =
                std::make_unique<CardSegmentedSwitch>(bus.name + " outputs", juce::StringArray{"Joined", "L/R"});
            row->join->setTooltip("One stereo jack, or a jack per channel");
            row->join->onChange = [this, busIndex = bus.busIndex](int index) {
                editHostedOutputs(editor_, nodeId_, [busIndex, index](HostedPluginModule& hm) {
                    for (const auto& b : hm.getOutputBuses())
                        if (b.busIndex == busIndex)
                            setBusJoined(hm, b, index == 0);
                });
                refresh(true);
            };
            addAndMakeVisible(*row->join);
        }
    }
    setSize(kWidth, kPad * 2 + kTitleHeight + 6 + count * kRowHeight + 8 + kFooterHeight * 2 + 4);
    refresh(false);
    resized();
}

void HostedOutputsPanel::refresh(bool animate) {
    auto* m = module();
    if (m == nullptr)
        return;
    if ((int)m->getOutputBuses().size() != rows_.size()) {
        rebuildRows();
        return;
    }
    for (int i = 0; i < rows_.size(); ++i) {
        const auto& bus = m->getOutputBuses()[(size_t)i];
        auto& row = *rows_[i];
        row.show.setRow(bus.name, channelRange(bus), bus.shown || isMainBus(bus), isMainBus(bus), animate);
        if (row.join != nullptr)
            row.join->setSelectedIndex(bus.joined ? 0 : 1, juce::dontSendNotification);
    }
}

// Each footer action is one undo step; one that would change nothing records nothing.
void HostedOutputsPanel::footerAction(int index) {
    auto* m = module();
    if (m == nullptr)
        return;
    bool any = false;
    for (const auto& bus : m->getOutputBuses())
        any = any || differs(bus, index);
    if (!any)
        return;
    editHostedOutputs(editor_, nodeId_, [index](HostedPluginModule& hm) {
        const auto buses = hm.getOutputBuses(); // a copy: the edits below rebuild the list
        for (const auto& bus : buses) {
            if (!differs(bus, index))
                continue;
            if (index <= 1)
                hm.setOutputShown(bus.busIndex, index == 0);
            else
                setBusJoined(hm, bus, index == 2);
        }
    });
    refresh(true);
}

void HostedOutputsPanel::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme.colors.surface);
    g.fillRoundedRectangle(bounds, theme.metrics.cornerRadius);
    g.setColour(theme.colors.border);
    g.drawRoundedRectangle(bounds, theme.metrics.cornerRadius, 1.0f);
}

void HostedOutputsPanel::resized() {
    auto area = getLocalBounds().reduced(kPad);
    title_.setBounds(area.removeFromTop(kTitleHeight));
    area.removeFromTop(6);
    for (auto* row : rows_) {
        auto line = area.removeFromTop(kRowHeight);
        if (row->join != nullptr)
            row->join->setBounds(line.removeFromRight(kJoinWidth).withSizeKeepingCentre(kJoinWidth, 22));
        line.removeFromRight(8);
        row->show.setBounds(line);
    }
    area.removeFromTop(8);
    for (int pair = 0; pair < 2; ++pair) {
        auto line = area.removeFromTop(kFooterHeight);
        const int half = line.getWidth() / 2;
        footer_[pair * 2]->setBounds(line.removeFromLeft(half).reduced(2, 1));
        footer_[pair * 2 + 1]->setBounds(line.reduced(2, 1));
    }
}

bool HostedOutputsPanel::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey)
        return closeHostingWindow(*this);
    return false;
}

void HostedOutputsPanel::focusFirstControl() {
    for (auto* row : rows_)
        if (row->show.isEnabled()) {
            row->show.grabKeyboardFocus();
            return;
        }
    if (!footer_.isEmpty())
        footer_[0]->grabKeyboardFocus();
}

// ---- entry points ------------------------------------------------------------------------------------------------

void showHostedOutputsPanel(ModuleComponent& card) {
    auto* editor = card.findParentComponentOfClass<GraphEditor>();
    if (editor == nullptr)
        return;
    auto* strip = card.getHostedOutputs();
    if (strip == nullptr)
        return;
    juce::Component* trigger = &strip->routingButton();
    auto* panelRaw = static_cast<HostedOutputsPanel*>(nullptr);
    PopoverOptions options;
    options.trigger = trigger;
    const auto area = trigger->getScreenBounds();
    strip->panelSlot().open(
        [editor, &card, &panelRaw] {
            auto panel = std::make_unique<HostedOutputsPanel>(*editor, card.getNodeId());
            panelRaw = panel.get();
            return panel;
        },
        area, &card, options);
    if (panelRaw != nullptr) {
        juce::Component::SafePointer<HostedOutputsPanel> safe(panelRaw);
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr)
                safe->focusFirstControl();
        });
    }
}

void toggleHostedOutputsFold(ModuleComponent& card) {
    auto* editor = card.findParentComponentOfClass<GraphEditor>();
    if (editor == nullptr)
        return;
    editHostedOutputs(*editor, card.getNodeId(),
                      [](HostedPluginModule& m) { m.setOutputsFolded(!m.getOutputsFolded()); });
}

} // namespace synth::ui
