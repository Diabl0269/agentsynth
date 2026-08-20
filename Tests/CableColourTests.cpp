// Tests for cable colour coding (issue #157).
//
// Split in three layers:
//   1. The pure resolver + category mapping (no GUI, no graph) — the bulk of the logic.
//   2. Theme plumbing: the new midiWire / cableCategory tokens survive a JSON round-trip.
//   3. The canvas integration: cable enumeration, hit-testing and disconnect on a real graph.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/UI/CableColour.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include "../Source/UI/Theme/BuiltInThemes.h"
#include "../Source/UI/Theme/ThemeLoader.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

using namespace synth::ui;

namespace {

// The ModuleCategory whose display label matches a library section header, or nullopt.
// The library's headers and moduleCategoryLabel() are deliberately the SAME strings — that
// correspondence is the invariant the library-driven test below enforces.
std::optional<ModuleCategory> categoryFromLabel(const juce::String& label) {
    for (int i = 0; i < kModuleCategoryCount; ++i)
        if (label == moduleCategoryLabel((ModuleCategory)i))
            return (ModuleCategory)i;
    return std::nullopt;
}

juce::File tempSettingsFile(const juce::String& name) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name + ".settings");
}

std::unique_ptr<juce::PropertiesFile> makeProps(const juce::String& name) {
    auto file = tempSettingsFile(name);
    file.deleteFile();
    juce::PropertiesFile::Options opts;
    opts.applicationName = name;
    opts.filenameSuffix = "settings";
    return std::make_unique<juce::PropertiesFile>(file, opts);
}

class DummyDragSource : public juce::Component {};

} // namespace

//==============================================================================
// 1. Category mapping
//==============================================================================

TEST(CableColourCategoryTest, EveryLibraryModuleMapsToItsLibrarySection) {
    // Driven off the module library itself rather than a hand-kept list of ModuleType values:
    // a module added to the library is covered automatically, and a module added to the enum
    // without a categoryFor() case shows up here as a Utility mismatch instead of silently
    // taking the fallback colour. (A hand-kept list went stale exactly once; hence this.)
    ModuleLibraryComponent library;
    const auto names = library.getDraggableModuleNames();
    ASSERT_GT(names.size(), 0);

    for (const auto& name : names) {
        auto processor = synth::AIStateMapper::createModule(name);
        ASSERT_NE(processor, nullptr) << "library offers \"" << name << "\" but createModule cannot build it";

        // The library's "I/O" section has no cable-colour category to match against: Audio Output
        // is a juce::AudioGraphIOProcessor with no ModuleType at all, and Audio Input (a module
        // now) is deliberately filed under Utility rather than a bucket of its own. Both
        // carry plain audio and are drawn with the Direct cable colour.
        if (GraphEditor::isSingletonIOModule(name))
            continue;

        auto* moduleBase = dynamic_cast<ModuleBase*>(processor.get());
        ASSERT_NE(moduleBase, nullptr) << name;

        const auto section = library.getSectionForModule(name);
        const auto expected = categoryFromLabel(section);
        ASSERT_TRUE(expected.has_value())
            << "library section \"" << section << "\" has no matching ModuleCategory label (module: " << name << ")";

        EXPECT_EQ(categoryFor(moduleBase->getModuleType()), *expected)
            << name << " sits under \"" << section << "\" in the library but its cable colour category disagrees";
    }
}

TEST(CableColourCategoryTest, HiddenAttenuverterIsAUtilityModule) {
    // Attenuverter is never in the library (it is an implementation detail of a mod routing), so
    // the library-driven test above cannot reach it.
    EXPECT_EQ(categoryFor(ModuleType::Attenuverter), ModuleCategory::Utility);
}

TEST(CableColourCategoryTest, EveryCategoryLabelIsAUsableLibrarySectionName) {
    // The reverse direction: every category must be reachable from some library section, or a
    // swatch exists in Settings that no cable can ever take.
    ModuleLibraryComponent library;
    std::set<int> reached;
    for (const auto& name : library.getDraggableModuleNames())
        if (const auto c = categoryFromLabel(library.getSectionForModule(name)))
            reached.insert((int)*c);

    for (int i = 0; i < kModuleCategoryCount; ++i)
        EXPECT_TRUE(reached.count(i) > 0)
            << "no library module maps to category \"" << moduleCategoryLabel((ModuleCategory)i) << "\"";
}

TEST(CableColourCategoryTest, PersistedIdsAreUniqueAndStable) {
    // These strings land in user settings files and theme JSON; a collision would silently make
    // two swatches share one stored value.
    std::set<juce::String> ids;
    for (int i = 0; i < kModuleCategoryCount; ++i)
        ids.insert(moduleCategoryId((ModuleCategory)i));
    EXPECT_EQ((int)ids.size(), kModuleCategoryCount);

    std::set<juce::String> sigIds;
    for (int i = 0; i < kCableSignalCount; ++i)
        sigIds.insert(cableSignalId((CableSignal)i));
    EXPECT_EQ((int)sigIds.size(), kCableSignalCount);

    // Spot-check a couple of exact ids: renaming these breaks existing users' saved colours.
    EXPECT_STREQ(moduleCategoryId(ModuleCategory::EnvelopesControl), "envelopes");
    EXPECT_STREQ(cableSignalId(CableSignal::ModCV), "modcv");
}

//==============================================================================
// 2. Resolver
//==============================================================================

TEST(CableColourResolveTest, BySignalTypeUsesTheMatchingThemeToken) {
    synth::theme::Colors colors; // Obsidian defaults
    CableColourOverrides none;

    auto resolve = [&](CableSignal s) {
        return resolveCableColour(CableColourMode::BySignalType, s, ModuleCategory::Utility, colors, none, false);
    };

    EXPECT_EQ(resolve(CableSignal::Audio), colors.audioWire);
    EXPECT_EQ(resolve(CableSignal::Midi), colors.midiWire);
    EXPECT_EQ(resolve(CableSignal::ModCV), colors.modWire);
    EXPECT_EQ(resolve(CableSignal::PolyBus), colors.polyBusWire);
    EXPECT_EQ(resolve(CableSignal::Pitch), colors.pitchWire);
    EXPECT_EQ(resolve(CableSignal::Gate), colors.gateWire);
}

TEST(CableColourResolveTest, MidiIsVisuallyDistinctFromAudio) {
    // The whole point of adding a midiWire token: before this, MIDI cables drew with audioWire,
    // so "colour by signal type" could not actually tell them apart.
    synth::theme::Colors colors;
    EXPECT_NE(colors.midiWire, colors.audioWire);
    for (const auto& t : synth::theme::builtInThemes())
        EXPECT_NE(t.colors.midiWire, t.colors.audioWire) << "theme: " << t.name;
}

TEST(CableColourResolveTest, BySourceCategoryUsesTheCategoryPalette) {
    synth::theme::Colors colors;
    CableColourOverrides none;

    for (int i = 0; i < kModuleCategoryCount; ++i) {
        // Signal is deliberately varied to prove it is ignored in this mode.
        const auto c = resolveCableColour(CableColourMode::BySourceCategory, (CableSignal)(i % kCableSignalCount),
                                          (ModuleCategory)i, colors, none, false);
        EXPECT_EQ(c, colors.cableCategory[(size_t)i]);
    }
}

TEST(CableColourResolveTest, CategoryPaletteEntriesAreDistinctPerBuiltInTheme) {
    // Eight buckets are useless if two of them look the same.
    for (const auto& t : synth::theme::builtInThemes()) {
        std::set<juce::uint32> seen;
        for (const auto& c : t.colors.cableCategory)
            seen.insert(c.getARGB());
        EXPECT_EQ((int)seen.size(), synth::theme::kCableCategoryCount) << "theme: " << t.name;
    }
}

TEST(CableColourResolveTest, OverridesApplyOnlyWithinTheirOwnMode) {
    synth::theme::Colors colors;
    CableColourOverrides o;
    o.setSignal(CableSignal::Audio, juce::Colours::red);
    o.setCategory(ModuleCategory::Filters, juce::Colours::lime);

    // Signal override wins in BySignalType...
    EXPECT_EQ(resolveCableColour(CableColourMode::BySignalType, CableSignal::Audio, ModuleCategory::Filters, colors, o,
                                 false),
              juce::Colours::red);
    // ...and is ignored in BySourceCategory, where the category override applies instead.
    EXPECT_EQ(resolveCableColour(CableColourMode::BySourceCategory, CableSignal::Audio, ModuleCategory::Filters, colors,
                                 o, false),
              juce::Colours::lime);

    // An un-overridden entry still follows the theme in both modes.
    EXPECT_EQ(
        resolveCableColour(CableColourMode::BySignalType, CableSignal::Gate, ModuleCategory::Sources, colors, o, false),
        colors.gateWire);
    EXPECT_EQ(resolveCableColour(CableColourMode::BySourceCategory, CableSignal::Gate, ModuleCategory::Sources, colors,
                                 o, false),
              colors.cableCategory[(size_t)ModuleCategory::Sources]);
}

TEST(CableColourResolveTest, BypassAppliesAlphaToWhateverColourWon) {
    synth::theme::Colors colors;
    CableColourOverrides o;
    o.setSignal(CableSignal::ModCV, juce::Colours::magenta);

    const auto bypassed =
        resolveCableColour(CableColourMode::BySignalType, CableSignal::ModCV, ModuleCategory::Utility, colors, o, true);
    // juce::Colour stores alpha as a byte, so 0.3f comes back as 76/255 — compare with a
    // tolerance rather than exactly.
    EXPECT_NEAR(bypassed.getFloatAlpha(), kBypassedCableAlpha, 1.0f / 255.0f);
    EXPECT_EQ(bypassed.withAlpha(1.0f), juce::Colours::magenta);

    // The base helper (used by the settings swatches) must NOT apply the bypass alpha.
    const auto base =
        resolveCableBaseColour(CableColourMode::BySignalType, CableSignal::ModCV, ModuleCategory::Utility, colors, o);
    EXPECT_FLOAT_EQ(base.getFloatAlpha(), 1.0f);
}

TEST(CableColourResolveTest, ClearingAnOverrideRestoresTheThemeColour) {
    synth::theme::Colors colors;
    CableColourOverrides o;
    o.setSignal(CableSignal::Audio, juce::Colours::red);
    EXPECT_TRUE(o.hasAny());

    o.clearSignal(CableSignal::Audio);
    EXPECT_FALSE(o.hasAny());
    EXPECT_EQ(resolveCableColour(CableColourMode::BySignalType, CableSignal::Audio, ModuleCategory::Utility, colors, o,
                                 false),
              colors.audioWire);
}

//==============================================================================
// 3. Persistence
//==============================================================================

TEST(CableColourPersistenceTest, ModeRoundTrips) {
    auto props = makeProps("AgentSynthCableMode");
    EXPECT_EQ(loadCableColourMode(*props), CableColourMode::BySignalType); // default

    saveCableColourMode(*props, CableColourMode::BySourceCategory);
    EXPECT_EQ(loadCableColourMode(*props), CableColourMode::BySourceCategory);

    saveCableColourMode(*props, CableColourMode::BySignalType);
    EXPECT_EQ(loadCableColourMode(*props), CableColourMode::BySignalType);

    tempSettingsFile("AgentSynthCableMode").deleteFile();
}

TEST(CableColourPersistenceTest, OverridesRoundTripAndResetRemovesTheKey) {
    auto props = makeProps("AgentSynthCableOverrides");

    CableColourOverrides o;
    o.setSignal(CableSignal::Pitch, juce::Colour(0xff123456));
    o.setCategory(ModuleCategory::TimeFX, juce::Colour(0xffABCDEF));
    saveCableColourOverrides(*props, o);

    const auto loaded = loadCableColourOverrides(*props);
    ASSERT_TRUE(loaded.signal[(size_t)CableSignal::Pitch].has_value());
    EXPECT_EQ(*loaded.signal[(size_t)CableSignal::Pitch], juce::Colour(0xff123456));
    ASSERT_TRUE(loaded.category[(size_t)ModuleCategory::TimeFX].has_value());
    EXPECT_EQ(*loaded.category[(size_t)ModuleCategory::TimeFX], juce::Colour(0xffABCDEF));

    // Untouched entries stay unset rather than being written out as the theme's current value —
    // that is what lets them keep following the theme.
    EXPECT_FALSE(loaded.signal[(size_t)CableSignal::Audio].has_value());
    EXPECT_FALSE(props->containsKey(signalOverrideKey(CableSignal::Audio)));

    // Reset clears the stored key entirely.
    CableColourOverrides cleared = loaded;
    cleared.clearSignal(CableSignal::Pitch);
    saveCableColourOverrides(*props, cleared);
    EXPECT_FALSE(props->containsKey(signalOverrideKey(CableSignal::Pitch)));
    EXPECT_FALSE(loadCableColourOverrides(*props).signal[(size_t)CableSignal::Pitch].has_value());

    tempSettingsFile("AgentSynthCableOverrides").deleteFile();
}

//==============================================================================
// 4. Theme plumbing
//==============================================================================

TEST(CableColourThemeTest, NewTokensSurviveAJsonRoundTrip) {
    auto original = synth::theme::makeNeon();
    original.colors.midiWire = juce::Colour(0xff102030);
    original.colors.cableCategory[3] = juce::Colour(0xff405060);

    const auto json = synth::theme::ThemeLoader::themeToJson(original);
    const auto reparsed = synth::theme::ThemeLoader::parseTheme(json, "neon");
    ASSERT_TRUE(reparsed.has_value());

    EXPECT_EQ(reparsed->colors.midiWire, juce::Colour(0xff102030));
    EXPECT_EQ(reparsed->colors.cableCategory[3], juce::Colour(0xff405060));
    for (int i = 0; i < synth::theme::kCableCategoryCount; ++i)
        EXPECT_EQ(reparsed->colors.cableCategory[(size_t)i], original.colors.cableCategory[(size_t)i]) << "index " << i;
}

TEST(CableColourThemeTest, MissingCableKeysFallBackToDefaults) {
    // A pre-#157 user theme has neither midiWire nor cableCategory; it must still load.
    const juce::String legacy = R"({
        "name": "Legacy",
        "colors": {
            "bg0": "#FF000000", "surface": "#FF111111", "accent": "#FF00D1FF",
            "textPrimary": "#FFFFFFFF", "audioWire": "#FFEEEEEE", "modWire": "#FF00D1FF"
        }
    })";

    const auto parsed = synth::theme::ThemeLoader::parseTheme(juce::JSON::parse(legacy), "legacy");
    ASSERT_TRUE(parsed.has_value()) << synth::theme::ThemeLoader::getLastError();

    const synth::theme::Colors defaults;
    EXPECT_EQ(parsed->colors.midiWire, defaults.midiWire);
    for (int i = 0; i < synth::theme::kCableCategoryCount; ++i)
        EXPECT_EQ(parsed->colors.cableCategory[(size_t)i], defaults.cableCategory[(size_t)i]);
}

TEST(CableColourThemeTest, PartialCableCategoryObjectOnlyOverridesNamedKeys) {
    const juce::String partial = R"({
        "name": "Partial",
        "colors": {
            "bg0": "#FF000000", "surface": "#FF111111", "accent": "#FF00D1FF",
            "textPrimary": "#FFFFFFFF", "audioWire": "#FFEEEEEE", "modWire": "#FF00D1FF",
            "cableCategory": { "filters": "#FF010203" }
        }
    })";

    const auto parsed = synth::theme::ThemeLoader::parseTheme(juce::JSON::parse(partial), "partial");
    ASSERT_TRUE(parsed.has_value()) << synth::theme::ThemeLoader::getLastError();

    const synth::theme::Colors defaults;
    EXPECT_EQ(parsed->colors.cableCategory[(size_t)ModuleCategory::Filters], juce::Colour(0xff010203));
    EXPECT_EQ(parsed->colors.cableCategory[(size_t)ModuleCategory::Sources],
              defaults.cableCategory[(size_t)ModuleCategory::Sources]);
}

//==============================================================================
// 5. Canvas geometry, enumeration and hit-testing
//==============================================================================

TEST(CableGeometryTest, PathStartsAndEndsAtThePorts) {
    const juce::Point<float> a(10.0f, 20.0f), b(300.0f, 400.0f);
    const auto path = GraphEditor::buildCablePath(a, b);

    EXPECT_NEAR(path.getPointAlongPath(0.0f).getDistanceFrom(a), 0.0f, 0.5f);
    EXPECT_NEAR(path.getPointAlongPath(path.getLength()).getDistanceFrom(b), 0.0f, 0.5f);
}

TEST(CableGeometryTest, DistanceIsZeroOnTheWireAndLargeAwayFromIt) {
    GraphEditor::VisibleCable cable;
    cable.p1 = {0.0f, 0.0f};
    cable.p2 = {200.0f, 0.0f};

    // A straight horizontal run: the bezier collapses onto the line between the ports.
    EXPECT_NEAR(GraphEditor::distanceToCable(cable, {100.0f, 0.0f}), 0.0f, 0.5f);
    EXPECT_NEAR(GraphEditor::distanceToCable(cable, {0.0f, 0.0f}), 0.0f, 0.5f);

    EXPECT_GT(GraphEditor::distanceToCable(cable, {100.0f, 60.0f}), 50.0f);
    EXPECT_GT(GraphEditor::distanceToCable(cable, {100.0f, -60.0f}), 50.0f);
}

// Builds a two-module patch with one audio cable and exposes the pieces the tests need.
class CableCanvasTest : public ::testing::Test {
protected:
    void buildPatch() {
        editor = std::make_unique<GraphEditor>(engine);
        editor->setSize(1400, 800);

        DummyDragSource src;
        editor->itemDropped({juce::var("Oscillator"), &src, juce::Point<int>(100, 100)});
        editor->itemDropped({juce::var("Filter"), &src, juce::Point<int>(700, 100)});
        editor->updateComponents();

        for (auto* node : engine.getGraph().getNodes()) {
            const auto name = node->getProcessor()->getName();
            if (name == "Oscillator")
                oscId = node->nodeID;
            else if (name == "Filter")
                filterId = node->nodeID;
        }
        ASSERT_NE(oscId.uid, 0u);
        ASSERT_NE(filterId.uid, 0u);

        engine.getGraph().addConnection({{oscId, 0}, {filterId, 0}});
        editor->updateComponents();
    }

    // The audio cable from the oscillator to the filter, if buildVisibleCables found it.
    std::optional<GraphEditor::VisibleCable> oscToFilter() {
        for (const auto& c : editor->buildVisibleCables())
            if (c.id.srcUid == oscId.uid && c.id.dstUid == filterId.uid)
                return c;
        return std::nullopt;
    }

    AudioEngine engine;
    std::unique_ptr<GraphEditor> editor;
    juce::AudioProcessorGraph::NodeID oscId, filterId;
};

TEST_F(CableCanvasTest, EnumeratesTheAudioCableWithSignalAndSourceCategory) {
    buildPatch();

    auto cable = oscToFilter();
    ASSERT_TRUE(cable.has_value()) << "buildVisibleCables did not report the osc -> filter connection";

    EXPECT_EQ(cable->kind, GraphEditor::VisibleCable::Kind::Direct);
    EXPECT_EQ(cable->signal, CableSignal::Audio);
    EXPECT_EQ(cable->sourceCategory, ModuleCategory::Sources); // Oscillator
    EXPECT_FALSE(cable->isBypassed);
    EXPECT_FALSE(cable->isPolyBus);
}

TEST_F(CableCanvasTest, HitTestFindsTheCableOnItsCurveAndNotFarFromIt) {
    buildPatch();

    auto cable = oscToFilter();
    ASSERT_TRUE(cable.has_value());

    // Sample the drawn curve itself — the same path paint() strokes.
    const auto path = GraphEditor::buildCablePath(cable->p1, cable->p2);
    const auto mid = path.getPointAlongPath(path.getLength() * 0.5f);

    auto hit = editor->getCableAt(mid);
    ASSERT_TRUE(hit.has_value()) << "a point ON the wire should hit it";
    EXPECT_EQ(hit->id.srcUid, oscId.uid);
    EXPECT_EQ(hit->id.dstUid, filterId.uid);

    // Well clear of every wire.
    EXPECT_FALSE(editor->getCableAt({mid.x, mid.y + 400.0f}).has_value());
}

TEST_F(CableCanvasTest, HitToleranceIsRespected) {
    buildPatch();
    auto cable = oscToFilter();
    ASSERT_TRUE(cable.has_value());

    const auto path = GraphEditor::buildCablePath(cable->p1, cable->p2);
    const auto mid = path.getPointAlongPath(path.getLength() * 0.5f);
    const juce::Point<float> nearMiss{mid.x, mid.y + 30.0f};

    EXPECT_FALSE(editor->getCableAt(nearMiss, 5.0f).has_value());
    EXPECT_TRUE(editor->getCableAt(nearMiss, 40.0f).has_value());
}

TEST_F(CableCanvasTest, DisconnectCableRemovesTheGraphEdge) {
    buildPatch();

    auto cable = oscToFilter();
    ASSERT_TRUE(cable.has_value());
    EXPECT_TRUE(engine.getGraph().isConnected({{oscId, 0}, {filterId, 0}}));

    editor->disconnectCable(*cable);

    EXPECT_FALSE(engine.getGraph().isConnected({{oscId, 0}, {filterId, 0}}));
    EXPECT_FALSE(oscToFilter().has_value());
}

TEST_F(CableCanvasTest, ColourFollowsTheActiveModeAndOverrides) {
    buildPatch();

    auto cable = oscToFilter();
    ASSERT_TRUE(cable.has_value());

    // Headless tests run on the stock JUCE LnF, so colourForCable falls back to the default
    // (Obsidian) tokens — enough to prove mode + override precedence reach the canvas.
    const synth::theme::Colors defaults;

    editor->setCableColourMode(CableColourMode::BySignalType);
    EXPECT_EQ(editor->colourForCable(*cable), defaults.audioWire);

    editor->setCableColourMode(CableColourMode::BySourceCategory);
    EXPECT_EQ(editor->colourForCable(*cable), defaults.cableCategory[(size_t)ModuleCategory::Sources]);

    CableColourOverrides o;
    o.setCategory(ModuleCategory::Sources, juce::Colours::hotpink);
    editor->setCableColourOverrides(o);
    EXPECT_EQ(editor->colourForCable(*cable), juce::Colours::hotpink);

    // Switching back to signal mode ignores the category override.
    editor->setCableColourMode(CableColourMode::BySignalType);
    EXPECT_EQ(editor->colourForCable(*cable), defaults.audioWire);
}
