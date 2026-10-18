// NoteLengthChoiceTests.cpp -- the note-length choices of a built-in card (the ADSR stage divisions, the Delay's
// time division, the LFO's sync rate) are a NoteLengthBox bound to their parameter like any combo, and every other
// choice stays a plain combo box.

#include "CardBodyTestHelpers.h"
#include "Modules/ADSRModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/LFOModule.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthBox.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;
using synth::ui::NoteLengthBox;

namespace {

juce::AudioParameterChoice* choice(juce::AudioProcessor& module, const juce::String& id) {
    for (auto* param : module.getParameters())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(param); c != nullptr && c->paramID == id)
            return c;
    return nullptr;
}

NoteLengthBox* boxOf(ModuleComponent& card, const juce::String& id) {
    auto* body = card.getCardBody();
    return body != nullptr ? dynamic_cast<NoteLengthBox*>(body->findWidget(id)) : nullptr;
}

} // namespace

TEST(NoteLengthChoice, EveryNoteLengthParameterGetsTheBoxBoundToItsChoices) {
    CardCanvas canvas;
    const auto delay = canvas.add(std::make_unique<DelayModule>(), 600, 0);
    const auto lfo = canvas.add(std::make_unique<LFOModule>(), 1200, 0);
    canvas.editor.updateComponents();

    struct Case {
        NodeID node;
        const char* param;
    };
    const Case cases[] = {{delay, "timeDiv"}, {lfo, "rateSync"}};
    for (const auto& c : cases) {
        SCOPED_TRACE(c.param);
        auto* card = canvas.card(c.node);
        ASSERT_NE(card, nullptr);
        auto* box = boxOf(*card, c.param);
        ASSERT_NE(box, nullptr) << "a note length is a NoteLengthBox";
        auto* param = choice(*canvas.processor(c.node), c.param);
        ASSERT_NE(param, nullptr);
        EXPECT_EQ(box->getChoices(), param->choices);
        EXPECT_EQ(box->getCurrentName(), param->getCurrentChoiceName()) << "it shows the parameter's value";
        EXPECT_EQ(box->getText(), param->getCurrentChoiceName());
        EXPECT_EQ(card->findMidiLearnableParamForTest(box), param) << "MIDI Learn works on it as on a combo";
        EXPECT_FALSE(box->getTitle().isEmpty()) << "a screen reader gets the parameter's name";
    }
}

TEST(NoteLengthChoice, APickWritesTheParameterAndAnAutomationWriteMovesTheBox) {
    CardCanvas canvas;
    const auto delay = canvas.add(std::make_unique<DelayModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(delay);
    ASSERT_NE(card, nullptr);
    auto* box = boxOf(*card, "timeDiv");
    auto* param = choice(*canvas.processor(delay), "timeDiv");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(param, nullptr);

    box->setCurrentName("1/8D", juce::sendNotificationSync);
    EXPECT_EQ(param->getCurrentChoiceName(), "1/8D");

    const int target = param->choices.indexOf("1/16T");
    card->reflectParameterValue(param, param->convertTo0to1((float)target));
    EXPECT_EQ(box->getCurrentName(), "1/16T") << "automation, undo and presets reach the box through the card";
}

TEST(NoteLengthChoice, OtherChoicesStayPlainComboBoxes) {
    CardCanvas canvas;
    const auto lfo = canvas.add(std::make_unique<LFOModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(lfo);
    ASSERT_NE(card, nullptr);
    auto* shape = card->getCardBody()->findWidget("shape");
    ASSERT_NE(shape, nullptr);
    EXPECT_NE(dynamic_cast<juce::ComboBox*>(shape), nullptr);
    EXPECT_EQ(dynamic_cast<NoteLengthBox*>(shape), nullptr);
}

TEST(NoteLengthChoice, AnAdsrStageDivisionIsACompactBoxThatFitsItsCellAndShowsTheFullName) {
    CardCanvas canvas;
    const auto adsr = canvas.add(std::make_unique<ADSRModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(adsr);
    ASSERT_NE(card, nullptr);
    for (const char* id : {"attackDiv", "holdDiv", "decayDiv", "releaseDiv"}) {
        SCOPED_TRACE(id);
        auto* param = choice(*canvas.processor(adsr), id);
        ASSERT_NE(param, nullptr);
        EXPECT_EQ(param->choices.size(), 24);
        auto* box = boxOf(*card, id);
        ASSERT_NE(box, nullptr) << "a division is a NoteLengthBox, not a stepped fader";
        EXPECT_TRUE(box->isCompact());
        EXPECT_EQ(box->getChoices(), param->choices);
        // The longest name must fit the box's own width in the font it draws with, so it never truncates.
        synth::theme::AppLookAndFeel look; // the app's own look: a plain test card has the stock one
        const juce::Font font = look.getComboBoxFont(*box);
        for (const auto& name : param->choices)
            EXPECT_LE(juce::GlyphArrangement::getStringWidthInt(font, name), box->getWidth() - 2) << name.toRawUTF8();
    }
}
