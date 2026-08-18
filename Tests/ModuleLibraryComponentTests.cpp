// ModuleLibraryComponentTests.cpp
// Headless unit tests for ModuleLibraryComponent (UI Phase 5):
//   • descriptionFor — known modules return non-empty, distinct strings; unknown → generic fallback
//   • getEntryIndexAt — maps y-coordinates to entry indices, including header rows
//   • paint smoke test — construct, setSize, paint; simulate a hovered index and paint again

#include "../Source/UI/ModuleLibraryComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// descriptionFor — pure static helper, no GUI needed
// ============================================================================

TEST(ModuleLibraryDescriptionFor, KnownModulesReturnNonEmpty) {
    const char* known[] = {"Oscillator",    "Wavetable",     "Noise",          "Sampler",
                           "LFO",           "Sequencer",     "Poly Sequencer", "MidiKeyboard",
                           "Poly MIDI",     "External MIDI", "ADSR",           "VCA",
                           "Filter",        "Parametric EQ", "Chorus",         "Phaser",
                           "Flanger",       "Distortion",    "Delay",          "Reverb",
                           "Compressor",    "Limiter",       "Voice Mixer",    "Bitcrusher",
                           "Pitch Shifter", "Sample & Hold", "Math",           "Envelope Follower",
                           "Comparator"};
    for (const char* name : known) {
        juce::String desc = ModuleLibraryComponent::descriptionFor(name);
        EXPECT_FALSE(desc.isEmpty()) << "descriptionFor(\"" << name << "\") must not be empty";
    }
}

TEST(ModuleLibraryDescriptionFor, KnownModulesReturnDistinctStrings) {
    const char* known[] = {"Oscillator",    "Wavetable",     "Noise",          "Sampler",
                           "LFO",           "Sequencer",     "Poly Sequencer", "MidiKeyboard",
                           "Poly MIDI",     "External MIDI", "ADSR",           "VCA",
                           "Filter",        "Parametric EQ", "Chorus",         "Phaser",
                           "Flanger",       "Distortion",    "Delay",          "Reverb",
                           "Compressor",    "Limiter",       "Voice Mixer",    "Bitcrusher",
                           "Pitch Shifter", "Sample & Hold", "Math",           "Envelope Follower",
                           "Comparator"};
    std::vector<juce::String> descs;
    for (const char* name : known)
        descs.push_back(ModuleLibraryComponent::descriptionFor(name));

    for (size_t i = 0; i < descs.size(); ++i)
        for (size_t j = i + 1; j < descs.size(); ++j)
            EXPECT_NE(descs[i], descs[j]) << "descriptionFor(\"" << known[i] << "\") == descriptionFor(\"" << known[j]
                                          << "\") — descriptions should be distinct";
}

TEST(ModuleLibraryDescriptionFor, UnknownNameReturnsGenericFallback) {
    juce::String desc = ModuleLibraryComponent::descriptionFor("UnknownXYZModule");
    EXPECT_FALSE(desc.isEmpty()) << "Unknown module name must return a non-empty fallback";
    // Verify it is the documented generic string (not a module-specific one).
    // All known-module strings contain at least one comma, dash, or parenthesis.
    // The generic fallback is intentionally short and plain.
    EXPECT_EQ(desc, juce::String("Audio processing module."));
}

TEST(ModuleLibraryDescriptionFor, LookupIsCaseInsensitive) {
    // "oscillator" (lowercase) should match the same description as "Oscillator".
    EXPECT_EQ(ModuleLibraryComponent::descriptionFor("oscillator"),
              ModuleLibraryComponent::descriptionFor("Oscillator"));
    EXPECT_EQ(ModuleLibraryComponent::descriptionFor("FILTER"), ModuleLibraryComponent::descriptionFor("Filter"));
}

// ============================================================================
// Hover index mapping — tests the y-to-entry-index logic exposed by the
// component's public getEntryIndexAt (via mouseMove). We test it indirectly
// through a real component instance, exercising getHoveredIndex() after
// synthesising MouseEvent y-coordinates.
//
// The layout (from the constructor):
//   y=10: header  "Sources"          h=25  (y 10..34)
//   y=35: item    "Oscillator"       h=32  (y 35..66)
//   y=67: item    "LFO"              h=32  (y 67..98)
//   y=98: header  "Sequencing"       h=25  (extra 5px gap -> y starts at 103)
//   actually layout: after the first header (y=10, h=25) y becomes 35.
//   Second header gets an extra 5px gap so: y=35+32=67, 67+32=99; then gap: 99+5=104, header h=25.
//
// The exact y values are verified by the component's own getEntryIndexAt
// logic. These tests exercise known-good and boundary points.
// ============================================================================

// Helper: simulate a mouse-move at a given y into the component.
// We construct a minimal MouseEvent from scratch.
static void simulateMouseMoveAt(ModuleLibraryComponent& comp, int y) {
    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(5.0f, (float)y), juce::ModifierKeys(),
                         1.0f,                   // pressure
                         0.0f, 0.0f, 0.0f, 0.0f, // tilt, rotation, etc.
                         &comp, &comp, juce::Time::getCurrentTime(), juce::Point<float>(5.0f, (float)y),
                         juce::Time::getCurrentTime(), 1, false);
    comp.mouseMove(evt);
}

static void simulateMouseExit(ModuleLibraryComponent& comp) {
    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(-1.0f, -1.0f), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &comp, &comp, juce::Time::getCurrentTime(), juce::Point<float>(-1.0f, -1.0f),
                         juce::Time::getCurrentTime(), 0, false);
    comp.mouseExit(evt);
}

// Vertical centre of the first draggable row. Resolved from the component's own layout rather
// than hard-coded: the sidebar gained a Snippets section and a collapse-all strip (issue #156),
// and every row y shifted. Asking the component keeps these tests about hover behaviour instead
// of about a magic offset that moves whenever a section is added.
static int firstDraggableRowY(const ModuleLibraryComponent& comp) {
    const int index = comp.getFirstDraggableEntryIndex();
    return index >= 0 ? comp.getRowCentreY(index) : -1;
}

// Row index of a module by name, or -1. Looked up rather than hard-coded for the same reason as
// firstDraggableRowY above.
static int rowIndexForModule(const ModuleLibraryComponent& comp, const juce::String& name) {
    for (int i = 0; i < comp.getEntryCount(); ++i)
        if (comp.getEntryText(i) == name)
            return i;
    return -1;
}

TEST(ModuleLibraryAvailability, EveryModuleIsEnabledWhenNoPredicateIsSet) {
    ModuleLibraryComponent lib;
    const int oscillatorRow = rowIndexForModule(lib, "Oscillator");
    ASSERT_GE(oscillatorRow, 0);

    EXPECT_TRUE(lib.isEntryEnabled(oscillatorRow));
    EXPECT_FALSE(lib.isEntryEnabled(-1));
    EXPECT_FALSE(lib.isEntryEnabled(9999));

    for (int i = 0; i < lib.getEntryCount(); ++i)
        if (lib.getEntry(i).kind == ModuleLibraryComponent::RowKind::Header)
            EXPECT_FALSE(lib.isEntryEnabled(i)) << "Headers are never draggable";
}

TEST(ModuleLibraryAvailability, PredicateDisablesMatchingRows) {
    ModuleLibraryComponent lib;
    lib.isModuleAvailable = [](const juce::String& name) { return name != "Audio Output"; };

    const int audioOutputRow = rowIndexForModule(lib, "Audio Output");
    const int oscillatorRow = rowIndexForModule(lib, "Oscillator");

    ASSERT_GE(audioOutputRow, 0) << "Audio Output should be offered in the library";
    ASSERT_GE(oscillatorRow, 0);

    EXPECT_FALSE(lib.isEntryEnabled(audioOutputRow)) << "An unavailable module must not be draggable";
    EXPECT_TRUE(lib.isEntryEnabled(oscillatorRow)) << "Other modules stay unaffected";
}

TEST(ModuleLibraryHoverIndex, HoverOnHeaderRowIsMinusOne) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    // Find a real header row and hover its centre — headers never get a hover index.
    int headerY = -1;
    for (int i = 0; i < comp.getEntryCount(); ++i) {
        if (comp.getEntry(i).kind == ModuleLibraryComponent::RowKind::Header) {
            headerY = comp.getRowCentreY(i);
            break;
        }
    }
    ASSERT_GT(headerY, 0);

    simulateMouseMoveAt(comp, headerY);
    EXPECT_EQ(comp.getHoveredIndex(), -1) << "Hovering over a header row must not set a positive hover index";
}

TEST(ModuleLibraryHoverIndex, HoverOnFirstItemRow) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    const int y = firstDraggableRowY(comp);
    ASSERT_GT(y, 0);

    simulateMouseMoveAt(comp, y);
    int idx = comp.getHoveredIndex();
    EXPECT_GE(idx, 0) << "Hovering inside the first draggable row must set a non-negative index";
    EXPECT_LT(idx, comp.getEntryCount()) << "Hovered index must be within valid range";
    EXPECT_NE(comp.getEntry(idx).kind, ModuleLibraryComponent::RowKind::Header);
}

TEST(ModuleLibraryHoverIndex, MouseExitClearsHoverIndex) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    // First hover over a valid item row.
    const int y = firstDraggableRowY(comp);
    ASSERT_GT(y, 0);
    simulateMouseMoveAt(comp, y);
    ASSERT_GE(comp.getHoveredIndex(), 0);

    // Then exit.
    simulateMouseExit(comp);
    EXPECT_EQ(comp.getHoveredIndex(), -1) << "mouseExit must reset hovered index to -1";
}

TEST(ModuleLibraryHoverIndex, HoverBelowAllEntriesIsMinusOne) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    // y=9999 is well past all entries.
    simulateMouseMoveAt(comp, 9999);
    EXPECT_EQ(comp.getHoveredIndex(), -1) << "y beyond all entries must give hoveredIndex -1";
}

TEST(ModuleLibraryHoverIndex, HoverAboveFirstEntryIsMinusOne) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    // y=5 is above the first row which starts at y=10.
    simulateMouseMoveAt(comp, 5);
    EXPECT_EQ(comp.getHoveredIndex(), -1) << "y above first entry must give hoveredIndex -1";
}

// ============================================================================
// Paint smoke tests — construct, setSize, paint into a juce::Image; no crash.
// Exercises the repaint-on-hover code path as well.
// ============================================================================

TEST(ModuleLibraryPaintSmoke, PaintWithNoHoverNoCrash) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    juce::Image img(juce::Image::ARGB, 200, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));

    // Must produce at least one non-transparent pixel.
    bool hasPixel = false;
    for (int y = 0; y < img.getHeight() && !hasPixel; ++y)
        for (int x = 0; x < img.getWidth() && !hasPixel; ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                hasPixel = true;
    EXPECT_TRUE(hasPixel) << "paint() should produce at least one visible pixel";
}

TEST(ModuleLibraryPaintSmoke, PaintWithHoveredIndexNoCrash) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    // Simulate hovering over the first draggable row.
    simulateMouseMoveAt(comp, firstDraggableRowY(comp));
    ASSERT_GE(comp.getHoveredIndex(), 0);

    juce::Image img(juce::Image::ARGB, 200, 600, true);
    juce::Graphics g(img);
    // This exercises the highlight-fill branch in paint().
    EXPECT_NO_THROW(comp.paint(g));
}

TEST(ModuleLibraryPaintSmoke, PaintAfterMouseExitNoCrash) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 600);

    simulateMouseMoveAt(comp, firstDraggableRowY(comp));
    simulateMouseExit(comp);
    ASSERT_EQ(comp.getHoveredIndex(), -1);

    juce::Image img(juce::Image::ARGB, 200, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
}
