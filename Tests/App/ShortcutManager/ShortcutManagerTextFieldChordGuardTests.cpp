// Concern: guard for the bug kind "a printable-key shortcut is typed into a focused text field instead of firing".
//
// On macOS a key that prints a character (Shift+Space, Shift+letter, Option+letter) reaches a focused text field
// through the input system as inserted text and never as a key press, so a shortcut made of one is dead there unless
// the field itself claims it (NavigationSearchField::onShiftSpaceTyped is the one hook today). This walks the whole
// default table, on both platforms' conventions, and demands that every such binding is classified:
//   * claimed: a text field takes it, and the claim is exercised through the field's real input path; or
//   * yields: typing wins by design, with the reason written down.
// A new printable binding that is in neither list fails here, so it cannot reach a user as a swallowed keystroke.
#include "../../SoundBrowser/SoundBrowserTestKit.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/PointerState.h"
#include "UI/Layout/SearchPillField.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include <functional>
#include <gtest/gtest.h>
#include <map>

namespace {

// A key the macOS input system turns into text when no Command or Control is held: a printable key code with only
// Shift and/or Option (Alt) on it. Bare printable keys are not shortcuts a text field could ever yield to.
bool typesACharacter(const juce::KeyPress& key) {
    if (!key.isValid())
        return false;
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown())
        return false;
    if (!mods.isShiftDown() && !mods.isAltDown())
        return false;
    const int code = key.getKeyCode();
    return code >= 32 && code <= 126;
}

struct TypingPolicy {
    // Set: a text field claims this chord; the function exercises the field's input path and returns true when the
    // action ran and no character was typed.
    std::function<bool()> claimedByField;
    // Else: why typing wins here.
    const char* yieldsBecause = nullptr;
};

bool soundSearchBoxClaimsShiftSpace() {
    using namespace sound_browser_test;
    TempTree tree;
    tree.fill();
    auto index = makeIndex(tree);
    index->setUserFolders({tree.user});
    scanAndWait(*index);
    synth::soundbrowser::SoundLists lists;
    synth::ui::SoundBrowserComponent view(*index, lists);
    view.setSize(300, 900);
    auto& search = view.getSearchField();
    search.setText("snare", juce::dontSendNotification);
    const auto toggles = [&view] {
        juce::DynamicObject state;
        view.addTestModeState(state);
        return (int)state.getProperty("previewToggles");
    };
    const int before = toggles();
    {
        const synth::ui::ScopedSyntheticModifiers held{juce::ModifierKeys(juce::ModifierKeys::shiftModifier)};
        search.insertTextAtCaret(" ");
    }
    return toggles() == before + 1 && search.getText() == "snare";
}

const char* const kSurfaceLocal =
    "acts on its own surface (a grid, lane, canvas or card) once that surface holds the keyboard; typing into a text "
    "field there is meant to type";

/** Every printable-key default binding and how it treats a focused text field. */
const std::map<juce::String, TypingPolicy>& typingPolicy() {
    static const std::map<juce::String, TypingPolicy> policy{
        {"soundPreviewToggle", {soundSearchBoxClaimsShiftSpace, nullptr}},
        {"drumMachinePlayClip", {nullptr, kSurfaceLocal}},
        {"pianoRollQuantiseLength", {nullptr, kSurfaceLocal}},
        {"pianoRollQuantisePitches", {nullptr, kSurfaceLocal}},
        {"pianoRollToggleScaleFilter", {nullptr, kSurfaceLocal}},
        {"recolourFocusedItem", {nullptr, kSurfaceLocal}},
        {"renameSelectedModuleAlt", {nullptr, kSurfaceLocal}},
        {"timelineDecreaseTrackHeight", {nullptr, kSurfaceLocal}},
        {"timelineIncreaseTrackHeight", {nullptr, kSurfaceLocal}},
        {"timelineJumpToLocator1", {nullptr, kSurfaceLocal}},
        {"timelineJumpToLocator2", {nullptr, kSurfaceLocal}},
        {"timelineResetTrackHeight", {nullptr, kSurfaceLocal}},
        {"timelineShapeFree", {nullptr, kSurfaceLocal}},
        {"timelineShapeLine", {nullptr, kSurfaceLocal}},
        {"timelineShapeMenu", {nullptr, kSurfaceLocal}},
        {"timelineShapeSaw", {nullptr, kSurfaceLocal}},
        {"timelineShapeSine", {nullptr, kSurfaceLocal}},
        {"timelineShapeSquare", {nullptr, kSurfaceLocal}},
        {"timelineShapeTriangle", {nullptr, kSurfaceLocal}},
    };
    return policy;
}

/** Ids of the printable-key bindings in `bindings` that `policy` does not classify. */
std::vector<juce::String> unclassified(const std::map<juce::String, juce::KeyPress>& bindings,
                                       const std::map<juce::String, TypingPolicy>& policy) {
    std::vector<juce::String> out;
    for (const auto& [id, key] : bindings) {
        if (!typesACharacter(key))
            continue;
        const auto it = policy.find(id);
        if (it == policy.end() || (!it->second.claimedByField && it->second.yieldsBecause == nullptr))
            out.push_back(id);
    }
    return out;
}

std::map<juce::String, juce::KeyPress> defaultBindings(ShortcutManager::DefaultsPlatform platform) {
    ShortcutManager manager;
    manager.setDefaultsPlatform(platform);
    std::map<juce::String, juce::KeyPress> out;
    for (const auto& id : manager.getActionIds())
        out[id] = manager.getBinding(id);
    return out;
}

} // namespace

TEST(ShortcutManagerTextFieldChordGuardTest, EveryPrintableDefaultBindingIsClaimedByATextFieldOrYieldsToTyping) {
    for (auto platform : {ShortcutManager::DefaultsPlatform::Mac, ShortcutManager::DefaultsPlatform::Other}) {
        const auto bindings = defaultBindings(platform);
        EXPECT_GT(bindings.size(), 50u);
        const auto missing = unclassified(bindings, typingPolicy());
        for (const auto& id : missing)
            ADD_FAILURE() << "default binding '" << id << "' ("
                          << ShortcutManager::keyPressToDisplayString(bindings.at(id))
                          << ") prints a character, so a focused text field types it instead of running it. Make a "
                             "text field claim it (see NavigationSearchField::onShiftSpaceTyped) and list it as "
                             "claimed, or list it as yielding with the reason.";
    }
}

TEST(ShortcutManagerTextFieldChordGuardTest, EveryClaimedChordRunsItsActionThroughTheFieldsRealInputPath) {
    int claimed = 0;
    for (const auto& [id, policy] : typingPolicy()) {
        if (!policy.claimedByField)
            continue;
        ++claimed;
        EXPECT_TRUE(policy.claimedByField()) << "'" << id << "': the focused text field must claim the chord";
    }
    EXPECT_GE(claimed, 1);
}

TEST(ShortcutManagerTextFieldChordGuardTest, TheClassificationHasNoEntryForAnActionThatIsGone) {
    const auto bindings = defaultBindings(ShortcutManager::hostDefaultsPlatform());
    auto other = defaultBindings(ShortcutManager::DefaultsPlatform::Other);
    for (const auto& [id, policy] : typingPolicy())
        EXPECT_TRUE(bindings.count(id) > 0 || other.count(id) > 0) << "'" << id << "' is not a default action";
}

TEST(ShortcutManagerTextFieldChordGuardTest, APlantedPrintableBindingWithNoClaimFailsTheGuard) {
    std::map<juce::String, juce::KeyPress> bindings{
        {"soundPreviewToggle", juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys::shiftModifier, ' ')},
        {"plantedShiftLetter", juce::KeyPress('x', juce::ModifierKeys::shiftModifier, 0)},
        {"plantedOptionLetter", juce::KeyPress('y', juce::ModifierKeys::altModifier, 0)},
        {"plantedCommandChord", juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)},
        {"plantedArrow", juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)},
    };
    const auto missing = unclassified(bindings, typingPolicy());
    ASSERT_EQ(missing.size(), 2u);
    EXPECT_EQ(missing[0], "plantedOptionLetter");
    EXPECT_EQ(missing[1], "plantedShiftLetter");
}

TEST(ShortcutManagerTextFieldChordGuardTest, AFieldThatDoesNotClaimShiftSpaceFailsItsClaimCheck) {
    // The claim check reads the field's real behaviour: a search box with no hook types the space.
    juce::TextEditor plain;
    plain.setText("snare", juce::dontSendNotification);
    {
        const synth::ui::ScopedSyntheticModifiers held{juce::ModifierKeys(juce::ModifierKeys::shiftModifier)};
        plain.insertTextAtCaret(" ");
    }
    EXPECT_NE(plain.getText(), "snare") << "an unclaimed field types the space";
}
