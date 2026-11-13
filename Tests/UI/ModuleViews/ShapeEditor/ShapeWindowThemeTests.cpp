// ShapeWindowThemeTests.cpp -- how the shape editor window looks and reads:
// - ShapeWindowTheme: the window (and every dialog that goes through launchDialog) is backed with an opaque, themed
//   colour: a glass theme's translucent surface must never leave JUCE's light grey showing.
// - ShapeWindowButtonLayout: at the size it opens at, every button's words fit and no two buttons overlap, for the
//   Filter and the LFO; the window's Pick shape is the card's (one label, one tooltip, the same Tab slot).
// - ShapeWindowTooltips: the button tooltip rules (docs/development/accessibility.md#the-rule) on the Filter and LFO
//   windows and the Stack harmonics panel, with the walk TooltipRulesTests.cpp runs on the cards.
#include "../../Accessibility/TooltipRulesWalk.h"
#include "../../Theme/ThemeSweep/ThemeSweepSupport.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/Layout/ActionTexts.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/NoteKeyStrip.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawPenButton.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeStackPanel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr int kWidth = 720;
constexpr int kHeight = 520;

void collectShownButtons(juce::Component& component, std::vector<juce::Button*>& out) {
    for (auto* child : component.getChildren()) {
        if (!child->isVisible())
            continue;
        if (auto* button = dynamic_cast<juce::Button*>(child))
            out.push_back(button);
        collectShownButtons(*child, out);
    }
}

// The width a text button's words have: AppLookAndFeel::drawButtonText draws them in the button less 4 px a side and
// any right inset, squeezing them down to the font's default minimum horizontal scale before it cuts any off.
float textRoom(juce::Button& button) {
    auto room = (float)button.getWidth() - 8.0f;
    room -= (float)(int)button.getProperties()[synth::theme::AppLookAndFeel::kTextInsetRightProperty];
    return room;
}

// The width a button's words need: the text, plus the Draw button's pen icon; a pill's text and padding.
float neededWidth(juce::Button& button) {
    auto& lf = button.getLookAndFeel();
    if (auto* text = dynamic_cast<juce::TextButton*>(&button)) {
        const auto font = lf.getTextButtonFont(*text, button.getHeight());
        float width = juce::GlyphArrangement::getStringWidth(font, button.getButtonText());
        if (dynamic_cast<ShapeDrawPenButton*>(&button) != nullptr)
            width += (float)ShapeDrawPenButton::kIconSize + ShapeDrawPenButton::kIconGap;
        return width;
    }
    if (dynamic_cast<juce::ToggleButton*>(&button) != nullptr) // a pill: its words and its side padding
        return (float)synth::theme::AppLookAndFeel::togglePillWidth(button.getButtonText());
    return 0.0f; // a painted segment or icon: no words of its own to fit
}

void expectButtonsFitAndDoNotOverlap(ShapeListEditor& editor, const char* which) {
    std::vector<juce::Button*> buttons;
    collectShownButtons(editor, buttons);
    ASSERT_GT(buttons.size(), 8u) << which;
    for (auto* button : buttons) {
        if (button->getButtonText().isEmpty() || button->getWidth() == 0)
            continue;
        if (dynamic_cast<juce::TextButton*>(button) != nullptr) {
            const float squeezed = neededWidth(*button) * juce::Font::getDefaultMinimumHorizontalScaleFactor();
            EXPECT_GE(textRoom(*button), squeezed) << which << ": \"" << button->getButtonText() << "\" is cut off";
        } else {
            EXPECT_GE((float)button->getWidth(), neededWidth(*button))
                << which << ": \"" << button->getButtonText() << "\" is cut off";
        }
    }
    for (size_t i = 0; i < buttons.size(); ++i)
        for (size_t j = i + 1; j < buttons.size(); ++j) {
            auto* parent = buttons[i]->getParentComponent();
            if (parent != buttons[j]->getParentComponent())
                continue;
            if (dynamic_cast<NoteKeyStrip*>(parent) != nullptr)
                continue; // a keyboard: the black keys sit over the white ones by design
            EXPECT_FALSE(buttons[i]->getBounds().intersects(buttons[j]->getBounds()))
                << which << ": \"" << buttons[i]->getTitle() << "\" overlaps \"" << buttons[j]->getTitle() << "\"";
        }
}

struct LfoWindowRig {
    AudioEngine engine;
    LFOModule* lfo = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    AppUndoManager undo;
    std::shared_ptr<LfoShapesAdapter> adapter;

    LfoWindowRig() {
        lfo = new LFOModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
        adapter = std::make_shared<LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
    }
};

} // namespace

TEST(ShapeWindowTheme, WindowBackgroundIsOpaqueAndThemedForEveryBuiltInTheme) {
    std::vector<juce::Colour> seen;
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto bg = synth::theme::windowBackgroundFor(theme);
        EXPECT_EQ(bg.getAlpha(), 255) << theme.name;
        EXPECT_NE(bg, juce::Colours::lightgrey) << theme.name;
        for (const auto& other : seen)
            EXPECT_NE(bg, other) << theme.name;
        seen.push_back(bg);
    }
    EXPECT_EQ(seen.size(), 4u);
}

TEST(ShapeWindowTheme, ShapeEditorPaintsOpaqueThemedPixelsInEveryTheme) {
    juce::MessageManager::getInstance();
    for (const auto& theme : synth::theme::builtInThemes()) {
        ShapeRig rig;
        synth::theme::AppLookAndFeel lookAndFeel;
        lookAndFeel.applyTheme(theme);
        ShapeListEditor editor(rig.adapter, 0);
        editor.setLookAndFeel(&lookAndFeel);
        editor.setSize(kWidth, kHeight);

        juce::Image image(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType());
        {
            juce::Graphics g(image);
            editor.paintEntireComponent(g, false);
        }
        EXPECT_TRUE(editor.isOpaque()) << theme.name;
        const juce::Point<int> probes[] = {
            {0, 0}, {kWidth - 1, 0}, {0, kHeight - 1}, {kWidth - 1, kHeight - 1}, {2, kHeight / 2}};
        for (const auto& pt : probes) {
            const auto pixel = image.getPixelAt(pt.x, pt.y);
            EXPECT_EQ(pixel.getAlpha(), 255) << theme.name << " at " << pt.x << "," << pt.y;
            EXPECT_NE(pixel, juce::Colours::lightgrey) << theme.name;
            EXPECT_EQ(pixel, synth::theme::windowBackgroundFor(theme)) << theme.name << " at " << pt.x << "," << pt.y;
        }

        synth::test::theme_sweep::writeSweepPng("shape-window", theme.id, image);
        editor.setLookAndFeel(nullptr);
    }
}

TEST(ShapeWindowTheme, LaunchDialogGivesDialogsTheThemedBackground) {
    const auto themes = synth::theme::builtInThemes();
    synth::theme::AppLookAndFeel lookAndFeel;
    lookAndFeel.applyTheme(themes[1]); // Neon: translucent surface
    juce::Component anchor;
    anchor.setLookAndFeel(&lookAndFeel);

    juce::DialogWindow::LaunchOptions options;
    options.componentToCentreAround = &anchor;
    popup_motion::applyThemedBackground(options);
    EXPECT_EQ(options.dialogBackgroundColour, synth::theme::windowBackgroundFor(themes[1]));
    EXPECT_NE(options.dialogBackgroundColour, juce::Colours::lightgrey);

    juce::DialogWindow::LaunchOptions explicitOptions;
    explicitOptions.componentToCentreAround = &anchor;
    explicitOptions.dialogBackgroundColour = juce::Colours::hotpink;
    popup_motion::applyThemedBackground(explicitOptions);
    EXPECT_EQ(explicitOptions.dialogBackgroundColour, juce::Colours::hotpink);

    anchor.setLookAndFeel(nullptr);
}

TEST(ShapeWindowButtonLayout, FilterWindowsButtonsFitTheirWordsAndDoNotOverlap) {
    synth::theme::AppLookAndFeel lookAndFeel;
    ShapeRig rig;
    synth::CustomFilterPerNote byHand;
    byHand.mode = synth::CustomFilterPerNote::Mode::ByHand; // the per-note bar with its octave buttons
    rig.filter->setCustomPerNote(byHand);
    ShapeListEditor editor(rig.adapter, 0);
    editor.setLookAndFeel(&lookAndFeel);
    editor.setSize(ShapeListEditor::kWidth, editor.getHeight());
    editor.resized();
    expectButtonsFitAndDoNotOverlap(editor, "Filter");
    EXPECT_GE(textRoom(editor.getPickShapeButton()), neededWidth(editor.getPickShapeButton()))
        << "Pick shape is drawn at full size, not squeezed";
    editor.setLookAndFeel(nullptr);
}

TEST(ShapeWindowButtonLayout, LfoWindowsButtonsFitTheirWordsAndDoNotOverlap) {
    synth::theme::AppLookAndFeel lookAndFeel;
    LfoWindowRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setLookAndFeel(&lookAndFeel);
    editor.setSize(ShapeListEditor::kWidth, editor.getHeight());
    editor.resized();
    expectButtonsFitAndDoNotOverlap(editor, "LFO");
    editor.setLookAndFeel(nullptr);
}

// The window's Pick shape and the card's are one action: the same words, name and tooltip, from one pair of constants.
TEST(ShapeWindowButtonLayout, PickShapeIsWordedLikeTheCardsPickShape) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& pick = editor.getPickShapeButton();
    EXPECT_EQ(pick.getButtonText(), juce::String(actiontext::kPickShape));
    EXPECT_EQ(pick.getTitle(), juce::String(actiontext::kPickShape));
    EXPECT_EQ(pick.getTooltip(), juce::String(actiontext::kPickShapeTip));
    EXPECT_EQ(pick.getExplicitFocusOrder(), editor.getRenameButton().getExplicitFocusOrder() + 1)
        << "the same Tab slot: right after Rename";
    EXPECT_EQ(editor.getDrawButton().getExplicitFocusOrder(), pick.getExplicitFocusOrder() + 1);
}

// No labelled button in either shape window or the Stack harmonics panel repeats its words in its tooltip.
TEST(ShapeWindowTooltips, NoLabelledButtonRepeatsItsLabelInItsTooltip) {
    namespace rules = synth::ui::test::tooltip_rules;
    std::vector<rules::ButtonTip> all;
    ShapeRig filterRig;
    ShapeListEditor filterWindow(filterRig.adapter, 0);
    rules::collectButtons(filterWindow, "[Filter shape window]", all);
    LfoWindowRig lfoRig;
    ShapeListEditor lfoWindow(lfoRig.adapter, 0);
    rules::collectButtons(lfoWindow, "[LFO shape window]", all);
    ShapeStackPanel stack({});
    rules::collectButtons(stack, "[Stack harmonics]", all);
    ASSERT_GT(all.size(), 10u);
    const auto found = rules::echoes(all);
    EXPECT_TRUE(found.empty()) << "a button that shows its words has a tooltip starting with them; give it only the "
                                  "explanation (synth::ui::setButtonTooltip):"
                               << rules::listing(found);
    const auto twins = rules::twinsThatDiffer(all);
    EXPECT_TRUE(twins.empty()) << "one name, different tooltips:" << rules::listing(twins);
}

// The Filter window's Show Spectrum, Pick shape and Done are the card's and the Sampler map's: one constant each
// (TooltipRulesTests.cpp checks the card and the map against the same constants).
TEST(ShapeWindowTooltips, SharedActionsUseTheirConstants) {
    ShapeRig rig;
    ShapeListEditor window(rig.adapter, 0);
    auto* spectrum = window.getSpectrumToggle();
    ASSERT_NE(spectrum, nullptr);
    EXPECT_EQ(spectrum->getButtonText(), juce::String(actiontext::kShowSpectrum));
    EXPECT_EQ(spectrum->getTooltip(), juce::String(actiontext::kShowSpectrumFilterTip));
    auto& pick = window.getPickShapeButton();
    EXPECT_EQ(pick.getTooltip(), juce::String(actiontext::kPickShapeTip));
    namespace rules = synth::ui::test::tooltip_rules;
    std::vector<rules::ButtonTip> buttons;
    rules::collectButtons(window, "[Filter shape window]", buttons);
    int dones = 0;
    for (const auto& b : buttons)
        if (b.name == actiontext::kDone) {
            ++dones;
            EXPECT_EQ(b.tip, actiontext::kDoneCloseWindowTip) << b.path;
        }
    EXPECT_EQ(dones, 1);
}
