#pragma once

#include "AppUndoManager.h"
#include "AudioEngine/ModulationRoutingTypes.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

class AudioEngine;
class GraphEditor;

class ModMatrixComponent
    : public juce::Component
    , public juce::Timer {
public:
    /** Row height used by both layout and tests. */
    static constexpr int kRowHeight = 48;

    // Shared column geometry — header labels (paint()) and each ModRow's combo columns
    // (ModRow::resized()) must stay pixel-aligned, so both read from these instead of
    // duplicating the same fractions.
    static constexpr int kRowNumColW = 30;
    static constexpr float kSourceColFrac = 0.30f;
    static constexpr float kDestColFrac = 0.35f;

    // 8px-grid gutter used for spacing between columns/controls.
    static constexpr int kGutter = 8;

    /** Returns true for odd rows (zebra striping). */
    static bool isZebraRow(int rowIndex) noexcept { return rowIndex % 2 == 1; }

    /** `editor` is the owning canvas. With it, a row re-point enters and leaves macros through ports
     *  the way a dragged cable does, as one undo step; without it (headless unit tests of the
     *  panel alone) the re-point is a plain graph edit. */
    ModMatrixComponent(AudioEngine& engine, AppUndoManager* undoMgr = nullptr, GraphEditor* editor = nullptr);
    ~ModMatrixComponent() override;

    void paint(juce::Graphics& g) override;
    // Focus-region outline (Source/UI/Layout/FocusRegion.h), drawn OVER children -- the row viewport
    // tiles wall-to-wall against this component's own edge, so an outline painted at the end of
    // paint() would sit UNDER it and never show.
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    void setFlatSourceMenu(bool shouldBeFlat);

    void clearRows();

    // Safely detach all rows from their processors before graph rebuild
    void detachAllRows();

    /** Hover row index (-1 = none). Set by ModRow mouse callbacks, or from tests. */
    void setHoveredRow(int rowIndex);
    int getHoveredRow() const noexcept { return hoveredRow_; }

    // Test-only: the closed-combobox label text for a given row's source/destination combo.
    // Exercises the exact JUCE label-resolution path (ComboBox::setSelectedId ->
    // getItemForId over the root menu) that the grouped-menu label bug hit.
    juce::String getRowSourceComboTextForTest(int rowIndex) const;
    juce::String getRowDestComboTextForTest(int rowIndex) const;

    /** Test-only: the live combos of a row, so a test drives the real comboBoxChanged path by
     *  selecting an id (see encodeComboId). Null when the row does not exist. */
    juce::ComboBox* getRowSourceComboForTest(int rowIndex);
    juce::ComboBox* getRowDestComboForTest(int rowIndex);
    int getNumRowsForTest() const noexcept { return (int)rows.size(); }

    /** Where a row's searchable picker opens. The default is a juce::CallOutBox anchored on the combo; a
     *  headless test has no window to host one, so it substitutes a launcher that takes the picker. */
    using PickerLauncher = std::function<void(std::unique_ptr<juce::Component> picker, juce::Rectangle<int> anchor)>;
    void setPickerLauncherForTest(PickerLauncher launcher) { pickerLauncher = std::move(launcher); }

    /** The id a source/destination combo item carries for (node, channel). */
    static int encodeComboId(juce::AudioProcessorGraph::NodeID node, int channel) noexcept {
        return (int)((node.uid << 8) | (juce::uint32)channel);
    }

private:
    AudioEngine& audioEngine;
    AppUndoManager* undoManager = nullptr;
    GraphEditor* graphEditor = nullptr;
    bool isSourceMenuFlat = false;

    juce::TextButton addButton{"Add Modulation"};
    juce::ToggleButton flatToggle{"Flat Sources"};

    // Defined in ModMatrixComponent.cpp: keeping it out of this header means the rows' combo,
    // picker and routing internals can change without recompiling everything that includes
    // GraphEditor.h.
    struct ModRow;

    std::vector<std::unique_ptr<ModRow>> rows;
    juce::Viewport viewport;
    juce::Component contentContainer;

    void addModulation();
    bool anyPopupOpen() const;

public:
    void updateRowsFromGraph();

private:
    int lastNodeCount = 0;
    size_t lastNamesSignature = 0; // hash of every module title the combos list; a rename changes it
    PickerLauncher pickerLauncher;
    int hoveredRow_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModMatrixComponent)
};
