// CaptionFitDefaultLayoutTests.cpp
//
// No caption on a default card is ellipsised: for every type with a code default, each caption label's text
// (in the font the label draws with, inside the label's border) fits the label the way
// AppLookAndFeel::drawLabel -> Graphics::drawFittedText fits it: the text is squashed horizontally down to the
// minimum scale (0.7 unless the label sets its own) before it is ellipsised, so a caption truncates only when
// its width times that scale still exceeds the label. A long parameter name gets a short label override in the default;
// the full name stays in the tooltip.
//
// Also here: the Sync caption button (CardWidget::Sync) on the Delay, LFO and ADSR, which sits at the end of a
// caption: its look, click, row and Tab stop, and the column-first Tab of the lane cards.

#include "../../../../TestWait.h"
#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"

#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include <algorithm>

using namespace cardbody_test;

TEST(CaptionFitDefaultLayout, NoCaptionOnADefaultCardTruncates) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const char* type : {"Filter",        "Compressor",     "Limiter",       "Gate",      "Delay",
                             "Reverb",        "Chorus",         "Flanger",       "Phaser",    "Distortion",
                             "Bitcrusher",    "Ring Modulator", "Pitch Shifter", "VCA",       "Envelope Follower",
                             "Sample & Hold", "Math",           "Voice Mixer",   "Poly MIDI", "Oscillator",
                             "Noise",         "Sampler",        "Drum Kit",      "Pluck",     "Mallet",
                             "LFO",           "Wavetable"}) {
        SCOPED_TRACE(type);
        ASSERT_NE(defaults.find(type), nullptr);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        auto* body = card->getCardBody();
        ASSERT_NE(body, nullptr);
        for (const auto& item : body->getPlan().items) {
            auto* label = dynamic_cast<juce::Label*>(item.label);
            if (label == nullptr || !label->isVisible())
                continue;
            const auto area = label->getBorderSize().subtractedFrom(label->getLocalBounds());
            const float textWidth = juce::GlyphArrangement::getStringWidth(label->getFont(), label->getText());
            const float scale = label->getMinimumHorizontalScale() > 0.0f
                                    ? label->getMinimumHorizontalScale()
                                    : juce::Font::getDefaultMinimumHorizontalScaleFactor();
            EXPECT_LT(textWidth * scale, (float)area.getWidth())
                << "caption \"" << label->getText().toStdString() << "\" truncates in a " << area.getWidth()
                << " px cell";
        }
    }
}

namespace {

struct SyncCase {
    const char* type;
    const char* syncId;      // the bool the button writes
    const char* hostId;      // the control whose caption carries the button
    const char* shownHostId; // the one of its swap that is drawn by default
};

const SyncCase kSyncCases[] = {{"Delay", "tempoSync", "time", "time"},
                               {"LFO", "mode", "rateHz", "rateSync"}, // synced by default: the division is drawn
                               {"ADSR", "tempoSync", "attack", "attack"}};

juce::ToggleButton* syncButton(CardCanvas& canvas, NodeID id, const juce::String& paramId) {
    return dynamic_cast<juce::ToggleButton*>(canvas.card(id)->getCardBody()->findWidget(paramId));
}

void setBool(juce::AudioProcessor& module, const juce::String& id, bool on) {
    auto* param = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&module, id));
    ASSERT_NE(param, nullptr) << id;
    param->setValueNotifyingHost(on ? 1.0f : 0.0f);
}

// A click is delivered through the message queue, so it has landed once the queue has run.
void click(juce::Button& button) {
    button.triggerClick();
    synth::test::flushMessageQueue();
}

} // namespace

TEST(SyncCaptionButton, ItIsATogglePillWithTheWordSyncAScreenReaderTitleAndATooltip) {
    for (const auto& c : kSyncCases) {
        SCOPED_TRACE(c.type);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(c.type), 0, 0);
        canvas.editor.updateComponents();
        auto* button = syncButton(canvas, id, c.syncId);
        ASSERT_NE(button, nullptr) << "a toggle, no longer a Free/Sync switch";
        EXPECT_EQ(button->getButtonText(), "Sync");
        EXPECT_EQ(button->getTitle(), "Sync");
        EXPECT_EQ(button->getTooltip(), synth::cardbody::kSyncTooltip);
        EXPECT_EQ(juce::String(synth::cardbody::kSyncTooltip), "Sync to tempo");
        EXPECT_TRUE(button->isVisible());
        EXPECT_TRUE(canvas.card(id)->getLocalBounds().contains(button->getBounds()));
    }
}

TEST(SyncCaptionButton, AClickTogglesTheParameterAndALitButtonMeansSynced) {
    for (const auto& c : kSyncCases) {
        SCOPED_TRACE(c.type);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(c.type), 0, 0);
        canvas.editor.updateComponents();
        auto* button = syncButton(canvas, id, c.syncId);
        ASSERT_NE(button, nullptr);
        auto* param = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(canvas.processor(id), c.syncId));
        ASSERT_NE(param, nullptr);
        const bool before = param->get();
        EXPECT_EQ(button->getToggleState(), before) << "lit means synced";
        click(*button);
        EXPECT_EQ(param->get(), !before) << "the click writes the parameter";
        EXPECT_EQ(button->getToggleState(), !before);
        click(*button);
        EXPECT_EQ(param->get(), before);
        setBool(*canvas.processor(id), c.syncId, !before); // an outside write moves the button
        EXPECT_EQ(button->getToggleState(), !before);
    }
}

TEST(SyncCaptionButton, TheCaptionCarriesTheButtonOnItsRowAfterItsWords) {
    for (const auto& c : kSyncCases) {
        SCOPED_TRACE(c.type);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(c.type), 0, 0);
        canvas.editor.updateComponents();
        auto* body = canvas.card(id)->getCardBody();
        auto* button = syncButton(canvas, id, c.syncId);
        ASSERT_NE(button, nullptr);
        const auto& plan = body->getPlan();
        const int sync = plan.findParam(c.syncId);
        ASSERT_GE(sync, 0);
        EXPECT_TRUE(plan.items[(size_t)sync].isLaneSync(plan)) << "a lane's Sync has no cell of its own";
        ASSERT_FALSE(plan.items[(size_t)sync].syncHosts.empty());
        const auto& host = plan.items[(size_t)plan.items[(size_t)sync].syncHosts.front()];
        ASSERT_EQ(host.param->paramID, juce::String(c.hostId));
        ASSERT_NE(host.label, nullptr);
        EXPECT_NEAR(button->getBounds().getCentreY(), host.label->getBounds().getCentreY(), 4)
            << "the button is on the caption's row";
        EXPECT_LE(host.label->getBounds().getRight() - 2, button->getX()) << "at the end of the caption";
        EXPECT_LT(host.label->getBounds().getX(), button->getX());
    }
}

TEST(SyncCaptionButton, FlippingSyncOpensNoGapAndKeepsTheCardsSize) {
    for (const char* type : {"LFO", "Delay"}) {
        SCOPED_TRACE(type);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        auto* button = syncButton(canvas, id, juce::String(type) == "LFO" ? "mode" : "tempoSync");
        ASSERT_NE(button, nullptr);
        const auto bounds = card->getBounds();
        std::vector<juce::Rectangle<int>> cells;
        for (const auto& item : card->getCardBody()->getPlan().items)
            if (item.widget != nullptr && item.widget->isVisible() && item.widget != button)
                cells.push_back(item.widget->getBounds());
        click(*button);
        card->getCardBody()->flushPendingConditionUpdate();
        EXPECT_EQ(card->getBounds(), bounds) << "no gap opens and nothing resizes";
        std::vector<juce::Rectangle<int>> after;
        for (const auto& item : card->getCardBody()->getPlan().items)
            if (item.widget != nullptr && item.widget->isVisible() && item.widget != button)
                after.push_back(item.widget->getBounds());
        ASSERT_EQ(cells.size(), after.size()) << "the swap shows one control for another";
        // No control moved: the switch that used to take a row is gone, so the Rate cell never shifts.
        auto tops = [](const std::vector<juce::Rectangle<int>>& rects) {
            std::vector<int> ys;
            for (const auto& r : rects)
                ys.push_back(r.getY());
            std::sort(ys.begin(), ys.end());
            return ys;
        };
        EXPECT_EQ(tops(cells), tops(after)) << "every control keeps its row";
    }
}

TEST(SyncCaptionButton, TabReachesTheButtonJustAfterTheControlItSyncs) {
    for (const auto& c : kSyncCases) {
        SCOPED_TRACE(c.type);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(c.type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        auto* button = syncButton(canvas, id, c.syncId);
        ASSERT_NE(button, nullptr);
        EXPECT_TRUE(button->getWantsKeyboardFocus()) << "a Tab stop";
        const auto stops = card->getKeyboardControls();
        const auto at = std::find(stops.begin(), stops.end(), static_cast<juce::Component*>(button));
        ASSERT_NE(at, stops.end()) << "the card's Tab walk includes the Sync button";
        auto* host = card->getCardBody()->findWidget(c.shownHostId);
        ASSERT_NE(at, stops.begin());
        EXPECT_EQ(*(at - 1), host) << "Sync comes right after the control it syncs";
    }
}

// The new lane cards (LFO, ADSR, Amp Env, Filter Env, VCA, Envelope Follower, Sample & Hold, Math, Voice Mixer, Poly
// MIDI): the mode row first, then each lane column by column, the Out lane last.
TEST(SyncCaptionButton, TabGoesColumnFirstOnEveryNewLaneCard) {
    for (const char* type : {"LFO", "ADSR", "Amp Env", "Filter Env", "VCA", "Envelope Follower", "Sample & Hold",
                             "Math", "Voice Mixer", "Poly MIDI"}) {
        SCOPED_TRACE(type);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        const auto& plan = card->getCardBody()->getPlan();
        std::vector<std::array<int, 3>> order;
        bool sawLane = false;
        for (auto* stop : card->getKeyboardControls())
            for (const auto& item : plan.items)
                if (item.param != nullptr && item.widget == stop) {
                    const auto& spot = item.laneSpot;
                    if (spot.lane < 0)
                        EXPECT_FALSE(sawLane) << item.param->paramID << ": the mode row comes before the lanes";
                    else
                        sawLane = true;
                    order.push_back({spot.lane, spot.column, spot.row});
                }
        ASSERT_FALSE(order.empty());
        EXPECT_TRUE(sawLane);
        for (size_t i = 0; i + 1 < order.size(); ++i)
            EXPECT_LE(order[i], order[i + 1]) << "stop " << i << " breaks column-first order";
        // The Out lane is last: the final lane index belongs to the Out section where the card has one.
        for (const auto& section : plan.sections)
            if (section.outLane)
                EXPECT_EQ(&section, &plan.sections.back());
    }
}
