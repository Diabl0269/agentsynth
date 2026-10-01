// DialogKeyboardTests.cpp -- every dialog and popup is reachable with Tab, in visual order, with no
// invisible stops, and closes on Escape (docs/development/accessibility.md).
//
// Each test walks the surface's Tab order through juce::KeyboardFocusTraverser (TabOrderHelpers.h)
// and asserts the exact sequence of control names, then sends a real juce::KeyPress through the
// surface's keyPressed().
#include "AI/AccountService.h"
#include "AccessibilitySettingsFixture.h"
#include "Auth/InMemoryTokenStore.h"
#include "Modules/FX/ParametricEQModule.h"
#include "TabOrderHelpers.h"
#include "UI/Assistant/SignInDialog.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Chrome/ExportAudioDialog.h"
#include "UI/Chrome/WelcomeScreenComponent.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Library/ModuleLibraryHelpPopup.h"
#include "UI/Macros/MacroPortConfigDialog/MacroPortConfigDialog.h"
#include "UI/ModuleViews/EQWindow.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UI/Settings/ShortcutsSettingsTab.h"
#include <gtest/gtest.h>

namespace {

using synth::test::sendEscape;
using synth::test::sendReturn;
using synth::test::walkTabOrder;

// A dialog window with no native peer that records its close button, so a test can see which
// surfaces close their host on Escape.
struct RecordingDialogWindow : juce::DialogWindow {
    RecordingDialogWindow()
        : juce::DialogWindow("test", juce::Colours::black, false, false) {}
    void closeButtonPressed() override { closed = true; }
    bool closed = false;
};

juce::StringArray names(std::initializer_list<const char*> list) {
    juce::StringArray out;
    for (const auto* s : list)
        out.add(s);
    return out;
}

// The Tab order of `root` must be a full cycle with every stop named.
void expectTabOrder(juce::Component& root, const juce::StringArray& expected) {
    const auto walk = walkTabOrder(root);
    EXPECT_EQ(walk.names().joinIntoString(" | "), expected.joinIntoString(" | "));
    EXPECT_TRUE(walk.isCompleteCycle()) << "Tab and Shift+Tab must visit every stop once, in opposite orders";
}

juce::TextEditor* firstTextEditor(juce::Component& root) {
    for (auto* child : root.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            return editor;
        if (auto* nested = firstTextEditor(*child))
            return nested;
    }
    return nullptr;
}

juce::Component* findByName(juce::Component& root, const juce::String& name) {
    for (auto* child : root.getChildren()) {
        if (child->isVisible() && synth::test::tabStopName(*child) == name)
            return child;
        if (auto* nested = findByName(*child, name))
            return nested;
    }
    return nullptr;
}

} // namespace

// ============================================================================
// The helpers
// ============================================================================

TEST(DialogKeyboardHelpers, ATextEditorIsOneTabStopAndTabThenReachesTheNextRealControl) {
    juce::Component root;
    root.setSize(300, 120);
    juce::TextEditor editor;
    editor.setTitle("Name");
    juce::TextButton next("Next");
    editor.setBounds(0, 0, 200, 24);
    next.setBounds(0, 50, 80, 24);
    root.addAndMakeVisible(editor);
    root.addAndMakeVisible(next);

    juce::KeyboardFocusTraverser traverser;
    EXPECT_EQ(traverser.getNextComponent(&editor), &next);
    expectTabOrder(root, names({"Name", "Next"}));
}

TEST(DialogKeyboardHelpers, RemovingHiddenTabStopsDropsAFocusWantingPartInsideTheEditor) {
    juce::Component root;
    root.setSize(300, 120);
    juce::TextEditor editor;
    editor.setTitle("Name");
    juce::TextButton next("Next");
    editor.setBounds(0, 0, 200, 24);
    next.setBounds(0, 50, 80, 24);
    root.addAndMakeVisible(editor);
    root.addAndMakeVisible(next);

    // The kind of invisible stop a text field can hide: an inner component that wants focus.
    juce::Component hidden;
    hidden.setWantsKeyboardFocus(true);
    hidden.setBounds(0, 0, 10, 10);
    editor.addAndMakeVisible(hidden);

    juce::KeyboardFocusTraverser traverser;
    ASSERT_EQ(traverser.getNextComponent(&editor), &hidden) << "the fixture must start with a hidden stop";

    synth::ui::removeHiddenTabStops(editor);

    EXPECT_EQ(traverser.getNextComponent(&editor), &next);
    EXPECT_EQ(walkTabOrder(root).forward.size(), 2u);
}

TEST(DialogKeyboardHelpers, ScrollIntoViewRevealsAControlBelowTheVisibleArea) {
    juce::Viewport viewport;
    viewport.setSize(200, 100);
    juce::Component content;
    content.setSize(200, 800);
    juce::TextButton far("Far");
    far.setBounds(10, 600, 80, 24);
    content.addAndMakeVisible(far);
    viewport.setViewedComponent(&content, false);

    synth::ui::ScrollIntoViewOnFocus follow(viewport);
    follow.reveal(&far);

    EXPECT_GE(viewport.getViewPositionY(), far.getBottom() - viewport.getMaximumVisibleHeight());
    EXPECT_LE(viewport.getViewPositionY(), far.getY());

    follow.reveal(&far); // already visible: no further scrolling
    const int settled = viewport.getViewPositionY();
    follow.reveal(&far);
    EXPECT_EQ(viewport.getViewPositionY(), settled);
}

TEST(DialogKeyboardHelpers, EscapeFromATextEditorTravelsUpToTheDialogAround) {
    struct Dialog : juce::Component {
        bool keyPressed(const juce::KeyPress& key) override {
            gotEscape = key == juce::KeyPress::escapeKey;
            return gotEscape;
        }
        bool gotEscape = false;
    } dialog;
    juce::TextEditor editor;
    dialog.addAndMakeVisible(editor);
    synth::ui::bubbleEscapeToParents(editor);

    editor.onEscapeKey();

    EXPECT_TRUE(dialog.gotEscape);
}

TEST(DialogKeyboardHelpers, ClosingTheHostingWindowPressesTheDialogWindowsCloseButton) {
    RecordingDialogWindow window;
    juce::Component content;
    window.setContentNonOwned(&content, false);

    EXPECT_TRUE(synth::ui::closeHostingWindow(content));
    EXPECT_TRUE(window.closed);

    juce::Component loose;
    EXPECT_FALSE(synth::ui::closeHostingWindow(loose)) << "nothing hosts a loose component";
}

// ============================================================================
// Dialogs
// ============================================================================

TEST(DialogKeyboard, ExportAudioDialogTabOrderAndKeys) {
    synth::ui::ExportAudioDialog dialog(16.0, true, 0.0, 8.0, 120.0, true,
                                        juce::File::getSpecialLocation(juce::File::tempDirectory), "Take");
    expectTabOrder(dialog, names({"Format", "Sample rate", "Bit depth", "Tail length", "Tail length value", "Tail unit",
                                  "Whole arrangement", "Current loop range", "File name", "Choose folder...", "Cancel",
                                  "Export"}));

    bool closed = false;
    dialog.onRequestClose = [&] { closed = true; };
    EXPECT_TRUE(sendEscape(dialog));
    EXPECT_TRUE(closed);

    bool exported = false;
    dialog.onExport = [&](synth::BounceOptions, juce::File) { exported = true; };
    dialog.collisionPromptForTest = [](std::function<void(int)> choose) { choose(1); };
    EXPECT_TRUE(sendReturn(dialog));
    EXPECT_TRUE(exported) << "Return presses the default Export button";
}

TEST(DialogKeyboard, SignInDialogTabOrderAndEscape) {
    synth::AccountService service(
        "http://mock-host:8787",
        [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&, int,
           const std::atomic<bool>&) { return synth::AuthClient::HttpResult{}; },
        std::make_unique<synth::InMemoryTokenStore>());
    synth::SignInDialog dialog(service);
    dialog.setSize(360, 220);
    expectTabOrder(dialog, names({"Open in Browser", "Cancel"}));

    RecordingDialogWindow window;
    window.setContentNonOwned(&dialog, false);
    EXPECT_TRUE(sendEscape(dialog));
    EXPECT_TRUE(window.closed) << "Escape cancels the sign-in and closes, like the Cancel button";
}

TEST(DialogKeyboard, MacroPortConfigDialogTabOrderAndEscape) {
    using Row = synth::ui::MacroPortConfigDialog::PortRow;
    Row mono;
    mono.nodeUuid = "in-mono";
    mono.isInput = true;
    mono.name = "Pitch In";
    Row poly;
    poly.nodeUuid = "in-poly";
    poly.isInput = true;
    poly.name = "Voices In";
    poly.shape = MacroPortShape::Poly;
    poly.voiceCount = 4;
    Row midi;
    midi.nodeUuid = "out-midi";
    midi.isInput = false;
    midi.name = "Gate Out";
    midi.kind = synth::MacroPortKind::Midi;

    synth::ui::MacroPortConfigDialog dialog("My Macro", {mono, poly, midi});
    expectTabOrder(
        dialog, names({"New port direction", "New port kind", "New port shape", "New port name", "Add", "Port colour",
                       "Port name", "Port shape", "Delete port", "Port colour", "Port name", "Port shape",
                       "Voice count", "Delete port", "Port colour", "Port name", "Delete port", "Close"}));

    bool closed = false;
    dialog.onRequestClose = [&] { closed = true; };
    EXPECT_TRUE(sendEscape(dialog));
    EXPECT_TRUE(closed);
}

TEST(DialogKeyboard, MacroAutoPortPromptTabOrderAndEscape) {
    synth::ui::MacroAutoPortPromptDialog dialog(2);
    expectTabOrder(dialog, names({"Remember my choice", "Create Ports", "Leave Cables As Is"}));

    bool createPorts = true;
    bool remember = true;
    dialog.onChoice = [&](bool create, bool rem) {
        createPorts = create;
        remember = rem;
    };
    EXPECT_TRUE(sendEscape(dialog));
    EXPECT_FALSE(createPorts);
    EXPECT_FALSE(remember);
}

TEST(DialogKeyboard, EQWindowTabOrderAndEscape) {
    ParametricEQModule eq;
    EQWindow window(eq);
    expectTabOrder(window, names({"EQ curve", "Show Spectrum"}));

    bool closed = false;
    window.onRequestClose = [&] { closed = true; };
    EXPECT_TRUE(sendEscape(window));
    EXPECT_TRUE(closed);

    RecordingDialogWindow host;
    EQWindow hosted(eq);
    host.setContentNonOwned(&hosted, false);
    EXPECT_TRUE(sendEscape(hosted));
    EXPECT_TRUE(host.closed) << "with no callback, Escape closes the hosting dialog window";
}

TEST(DialogKeyboard, WelcomeScreenTabOrderCyclesInsideTheOverlay) {
    synth::ui::WelcomeScreenComponent welcome;
    welcome.setSize(900, 700);
    welcome.setRecentProjects({juce::File("/tmp/Alpha.synthproj"), juce::File("/tmp/Beta.synthproj")});
    expectTabOrder(welcome, names({"New empty project", "Open our default project", "Open an existing project...",
                                   "Alpha", "Beta", "Contribute", "What's New..."}));
    EXPECT_TRUE(welcome.isKeyboardFocusContainer());
    EXPECT_FALSE(sendEscape(welcome)) << "the start screen has no cancel";
}

TEST(DialogKeyboard, ColourPickerTabOrderAndEscapeRestoresTheOriginalColour) {
    juce::Colour lastPreview = juce::Colours::transparentBlack;
    synth::ui::ColourPickerPopup popup(juce::Colours::red, nullptr, [&](juce::Colour c) { lastPreview = c; }, {});
    const auto walk = walkTabOrder(popup);
    const auto stops = walk.names();
    ASSERT_GE(stops.size(), 5);
    EXPECT_TRUE(walk.isCompleteCycle());
    EXPECT_EQ(stops[0], "Red value");
    EXPECT_EQ(stops[1], "Green value");
    EXPECT_EQ(stops[2], "Blue value");
    EXPECT_EQ(stops[3], "Add to favourites");
    EXPECT_TRUE(stops[4].startsWith("Favourite colour ")) << stops[4];

    RecordingDialogWindow host;
    host.setContentNonOwned(&popup, false);
    popup.setCurrentColourForTest(juce::Colours::blue);
    EXPECT_TRUE(sendEscape(popup));
    EXPECT_EQ(popup.getCurrentColourForTest(), juce::Colours::red);
    EXPECT_EQ(lastPreview, juce::Colours::red) << "the live preview goes back too";
    EXPECT_TRUE(host.closed);
}

TEST(DialogKeyboard, HelpPopupTabOrderAndEscape) {
    synth::ui::ModuleLibraryHelpPopup popup;
    expectTabOrder(popup, names({"Pin help", "Close help", "Using modules", "Your first patch", "Key shortcuts"}));

    popup.setPinnedForPaint(true);
    EXPECT_EQ(walkTabOrder(popup).names()[0], "Unpin help");

    bool closed = false;
    popup.onCloseRequested = [&] { closed = true; };
    EXPECT_TRUE(sendEscape(popup));
    EXPECT_TRUE(closed);
}

TEST(DialogKeyboard, HelpPopupSectionHeadersFoldWithTheKeyboard) {
    synth::ui::ModuleLibraryHelpPopup popup;
    auto* header = dynamic_cast<juce::Button*>(findByName(popup, "Your first patch"));
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(popup.isSectionExpandedForTest(1));
    header->onClick();
    EXPECT_FALSE(popup.isSectionExpandedForTest(1));
}

// ============================================================================
// Settings
// ============================================================================

TEST_F(AccessibilitySettingsTest, DualIOPopupTabOrderIsTheModuleOrderAndEscapeClosesIt) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 460);
    auto popup = tab.createDualIOPerModuleDefaultsPopupForTest();
    ASSERT_NE(popup, nullptr);
    popup->setSize(popup->getWidth(), popup->getHeight());
    popup->resized();

    juce::StringArray expected;
    for (const auto& type : PreferencesSettingsTab::getDualIOModuleTypes())
        expected.add(type);
    expectTabOrder(*popup, expected);

    RecordingDialogWindow host;
    host.setContentNonOwned(popup.get(), false);
    EXPECT_TRUE(sendEscape(*popup));
    EXPECT_TRUE(host.closed);
    host.clearContentComponent();
}

namespace {
// The content of settings tab `index`, made the current tab.
juce::Component& tabContent(SettingsWindow& window, int index) {
    window.getTabs().setCurrentTabIndex(index);
    return *window.getTabs().getTabContentComponent(index);
}

int tabIndex(SettingsWindow& window, const juce::String& name) {
    for (int i = 0; i < window.getNumTabs(); ++i)
        if (window.getTabName(i) == name)
            return i;
    return -1;
}
} // namespace

TEST_F(AccessibilitySettingsTest, SettingsTabStripIsOneTabStopAheadOfTheTabsControls) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    const auto stops = walkTabOrder(window).names();
    ASSERT_FALSE(stops.isEmpty());
    EXPECT_EQ(stops[0], "Settings tabs") << "the strip comes first";
    EXPECT_EQ(stops.indexOf("Settings tabs", false, 1), -1) << "and is a single stop";
    for (int i = 0; i < window.getNumTabs(); ++i)
        EXPECT_FALSE(window.getTabs().getTabbedButtonBar().getTabButton(i)->getWantsKeyboardFocus())
            << window.getTabName(i) << " button is a stop of its own";
}

TEST_F(AccessibilitySettingsTest, AiTabTabOrder) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    expectTabOrder(tabContent(window, tabIndex(window, "AI")),
                   names({"AI provider", "AI provider host", "Local history retention", "Request timeout"}));
}

TEST_F(AccessibilitySettingsTest, FeedbackTabTabOrderIncludesSendOnceThereIsText) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    auto& tab = tabContent(window, tabIndex(window, "Feedback"));
    expectTabOrder(tab, names({"Feedback category", "Feedback message"}));

    auto* editor = firstTextEditor(tab);
    editor->setText("Great app", false);
    editor->onTextChange(); // the real one is posted to the message loop
    expectTabOrder(tab, names({"Feedback category", "Feedback message", "Send Feedback"}));
}

TEST_F(AccessibilitySettingsTest, AppearanceTabTabOrder) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    expectTabOrder(
        tabContent(window, tabIndex(window, "Appearance")),
        names({"Theme mode", "Default dark theme", "Default light theme", "Theme gallery", "Open Themes Folder",
               "Reload Themes", "Colour cables by", "Cable colours", "Reset Cable Colours", "Piano roll note colours",
               "Reset Note Colours", "Meter colour stops", "Remove", "Reset to Theme"}));
}

TEST_F(AccessibilitySettingsTest, KeyboardShortcutsTabTabOrderFollowsTheSectionsAndRows) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    auto& tab = tabContent(window, tabIndex(window, "Keyboard Shortcuts"));

    juce::StringArray expected{"Search shortcuts", "Collapse all"};
    for (auto category : ShortcutManager::getCategoryOrder()) {
        expected.add(ShortcutManager::getCategoryName(category));
        for (const auto& id : shortcutManager.getActionIds())
            if (ShortcutManager::getCategory(id) == category)
                expected.add(ShortcutManager::getActionDescription(id) + " shortcut");
    }
    expected.addArray({"Export...", "Import...", "Reset to Defaults"});
    expectTabOrder(tab, expected);
}

TEST_F(AccessibilitySettingsTest, ShortcutSectionHeadersAndCollapseAllFoldWithTheKeyboard) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    auto& content = tabContent(window, tabIndex(window, "Keyboard Shortcuts"));
    auto& tab = static_cast<ShortcutsSettingsTab&>(content);

    const auto firstCategory = ShortcutManager::getCategoryOrder().front();
    auto* header = dynamic_cast<juce::Button*>(findByName(tab, ShortcutManager::getCategoryName(firstCategory)));
    ASSERT_NE(header, nullptr);
    ASSERT_FALSE(tab.isSectionCollapsed(firstCategory));
    header->onClick();
    EXPECT_TRUE(tab.isSectionCollapsed(firstCategory));

    auto* strip = dynamic_cast<juce::Button*>(findByName(tab, "Collapse all"));
    ASSERT_NE(strip, nullptr);
    strip->onClick();
    EXPECT_TRUE(tab.areAllSectionsCollapsed());
    EXPECT_NE(findByName(tab, "Expand all"), nullptr) << "the strip's label flips once everything is folded";
}

TEST_F(AccessibilitySettingsTest, ARebindButtonTakesSpaceAndReturnAsTheNewKeyWhileListening) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    auto& tab = static_cast<ShortcutsSettingsTab&>(tabContent(window, tabIndex(window, "Keyboard Shortcuts")));

    const auto id = shortcutManager.getActionIds()[0];
    auto* button =
        dynamic_cast<juce::Button*>(findByName(tab, ShortcutManager::getActionDescription(id) + " shortcut"));
    ASSERT_NE(button, nullptr);
    const auto original = shortcutManager.getBinding(id);
    ASSERT_NE(original, juce::KeyPress(juce::KeyPress::returnKey));

    tab.startListeningForTest(0);
    EXPECT_TRUE(sendReturn(*button));
    EXPECT_EQ(shortcutManager.getBinding(id), juce::KeyPress(juce::KeyPress::returnKey))
        << "while listening, Return is a key to bind and does not press the button";

    shortcutManager.setBinding(id, original);
}

TEST_F(AccessibilitySettingsTest, EscapeClosesTheSettingsWindowFromAnyTabAndFromTextFields) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    int closes = 0;
    window.onRequestClose = [&] { ++closes; };

    EXPECT_TRUE(sendEscape(window));
    EXPECT_EQ(closes, 1);

    for (const char* tabName : {"AI", "Feedback"}) {
        auto* editor = firstTextEditor(tabContent(window, tabIndex(window, tabName)));
        ASSERT_NE(editor, nullptr) << tabName;
        editor->onEscapeKey();
    }
    EXPECT_EQ(closes, 3) << "a text field passes Escape up instead of swallowing it";

    auto& shortcuts = static_cast<ShortcutsSettingsTab&>(tabContent(window, tabIndex(window, "Keyboard Shortcuts")));
    shortcuts.setSearchText("undo");
    firstTextEditor(shortcuts)->onEscapeKey();
    EXPECT_EQ(closes, 3) << "Escape clears a search query first";
    EXPECT_EQ(shortcuts.getSearchText(), juce::String());
    firstTextEditor(shortcuts)->onEscapeKey();
    EXPECT_EQ(closes, 4) << "and closes once there is nothing left to clear";
}

TEST_F(AccessibilitySettingsTest, SettingsWindowEscapeClosesItsDialogWindowWhenNoCallbackIsSet) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    RecordingDialogWindow host;
    host.setContentNonOwned(&window, false);
    EXPECT_TRUE(sendEscape(window));
    EXPECT_TRUE(host.closed);
    host.clearContentComponent();
}

TEST_F(AccessibilitySettingsTest, TheScrollingTabsKeepTheirViewportsOutOfTheTabOrder) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    for (const char* tabName : {"Keyboard Shortcuts", "Preferences", "Appearance"})
        for (auto* stop : walkTabOrder(tabContent(window, tabIndex(window, tabName))).forward)
            EXPECT_EQ(dynamic_cast<juce::Viewport*>(stop), nullptr) << tabName << " has an invisible Viewport stop";
}

TEST_F(AccessibilitySettingsTest, AudioTabControlsAreAllNamedTabStops) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    const auto walk = walkTabOrder(tabContent(window, tabIndex(window, "Audio")));
    EXPECT_TRUE(walk.isCompleteCycle());
    for (const auto& name : walk.names())
        EXPECT_FALSE(name.startsWith("<unnamed")) << name;
}

// ============================================================================
// Settings window: opening focus, Space on a tab, no hidden stop after a switch
// ============================================================================

TEST_F(AccessibilitySettingsTest, OpeningTheSettingsWindowTargetsTheTabStripForFocus) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr,
                          nullptr, true, "Appearance");
    window.setSize(800, 600);
    EXPECT_EQ(window.getCurrentTabIndex(), tabIndex(window, "Appearance"));
    EXPECT_EQ(window.getCurrentTabButton(),
              window.getTabs().getTabbedButtonBar().getTabButton(tabIndex(window, "Appearance")));
    EXPECT_TRUE(window.getTabStripFocus().getWantsKeyboardFocus());
    EXPECT_NO_THROW(window.focusCurrentTab());
}

TEST_F(AccessibilitySettingsTest, FocusLandingOnTheHostingWindowMovesToTheTabStrip) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    RecordingDialogWindow host;
    host.setContentNonOwned(&window, false);
    host.setVisible(true);
    EXPECT_TRUE(window.redirectWindowFocusToTabStrip(&host));
    EXPECT_FALSE(window.redirectWindowFocusToTabStrip(&window.getTabStripFocus()));
    EXPECT_FALSE(window.redirectWindowFocusToTabStrip(nullptr));
    host.clearContentComponent();
}

TEST_F(AccessibilitySettingsTest, AfterAKeyboardTabSwitchTabLandsOnTheNewTabsFirstControlNotAContentWrapper) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    auto& strip = window.getTabStripFocus();
    for (int i = 0; i < window.getNumTabs(); ++i) {
        auto* content = window.getTabs().getTabContentComponent(i);
        ASSERT_NE(content, nullptr);
        EXPECT_FALSE(content->getWantsKeyboardFocus()) << window.getTabName(i) << " content is a Tab stop";

        ASSERT_TRUE(strip.keyPressed(juce::KeyPress(i == 0 ? juce::KeyPress::homeKey : juce::KeyPress::rightKey)));
        ASSERT_EQ(window.getCurrentTabIndex(), i);
        juce::KeyboardFocusTraverser traverser;
        auto* next = traverser.getNextComponent(&strip);
        ASSERT_NE(next, nullptr) << window.getTabName(i);
        EXPECT_NE(next, content) << window.getTabName(i) << ": Tab from the tab strip stops on the content wrapper";
        EXPECT_TRUE(content->isParentOf(next)) << window.getTabName(i);
        EXPECT_FALSE(synth::test::tabStopName(*next).startsWith("<unnamed"))
            << window.getTabName(i) << ": " << synth::test::tabStopName(*next);
    }
}

TEST_F(AccessibilitySettingsTest, EveryPreferencesControlIsNamedAndHasATooltip) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);
    using Category = PreferencesSettingsTab::Category;
    for (auto category : {Category::Graph, Category::Timeline, Category::Files, Category::Mixer, Category::Panels,
                          Category::MidiRemote, Category::All}) {
        tab.setSelectedCategory(category);
        juce::String listing;
        for (const auto& gap : synth::test::auditAccessibility(tab))
            listing << "\n  " << (gap.kind == synth::test::Gap::Kind::MissingName ? "name " : "tip  ") << gap.path;
        EXPECT_TRUE(listing.isEmpty()) << PreferencesSettingsTab::categoryName(category) << listing;
    }
    EXPECT_EQ(tab.getCategoryComboForTest().getTitle(), "Preferences category");
    EXPECT_EQ(tab.getSearchFieldForTest().getTitle(), "Filter preferences");
}
