#pragma once

#include "../AppUndoManager.h"
#include "../AudioEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

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

    ModMatrixComponent(AudioEngine& engine, AppUndoManager* undoMgr = nullptr);
    ~ModMatrixComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    void setFlatSourceMenu(bool shouldBeFlat);

    void clearRows() {
        rows.clear();
        repaint();
    }

    // Safely detach all rows from their processors before graph rebuild
    void detachAllRows();

    /** Hover row index (-1 = none). Set by ModRow mouse callbacks, or from tests. */
    void setHoveredRow(int rowIndex);
    int getHoveredRow() const noexcept { return hoveredRow_; }

    // Test-only: the closed-combobox label text for a given row's source/destination combo.
    // Exercises the exact JUCE label-resolution path (ComboBox::setSelectedId ->
    // getItemForId over the root menu) that the grouped-menu label bug hit.
    juce::String getRowSourceComboTextForTest(int rowIndex) const {
        if (rowIndex < 0 || rowIndex >= (int)rows.size())
            return {};
        return rows[(size_t)rowIndex]->sourceCombo.getText();
    }
    juce::String getRowDestComboTextForTest(int rowIndex) const {
        if (rowIndex < 0 || rowIndex >= (int)rows.size())
            return {};
        return rows[(size_t)rowIndex]->destCombo.getText();
    }

private:
    AudioEngine& audioEngine;
    AppUndoManager* undoManager = nullptr;
    bool isSourceMenuFlat = false;

    juce::TextButton addButton{"Add Modulation"};
    juce::ToggleButton flatToggle{"Flat Sources"};

    struct ModRow
        : public juce::Component
        , public juce::ComboBox::Listener
        , public juce::AudioProcessorParameter::Listener {
        ModRow(ModMatrixComponent& owner, juce::AudioProcessorGraph::NodeID id);

        void parameterValueChanged(int parameterIndex, float newValue) override;
        void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
        ~ModRow() override;

        void setRowIndex(int index) {
            rowIndex = index;
            repaint();
        }
        int rowIndex = 0;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void comboBoxChanged(juce::ComboBox* comboBox) override;
        void lookAndFeelChanged() override;

        // Re-applies the themed bypass/delete icons; called from the constructor and again from
        // lookAndFeelChanged() on every theme switch (mirrors ModuleComponent::applyHeaderButtonIcons).
        void applyButtonIcons();

        ModMatrixComponent& owner;
        juce::AudioProcessorGraph::NodeID attenuverterId;

        // Keeps the attenuverter's processor alive for at least as long as this row holds parameter
        // attachments into it. juce::ParameterAttachment's destructor unconditionally calls
        // parameter.removeListener() on the reference it captured at construction, so the processor
        // MUST outlive amountAttachment/bypassAttachment — including when the node has already been
        // removed from the graph (removeModRouting) before updateRowsFromGraph() erases this row.
        // Graph nodes are reference counted; removeNode() drops the node from the processing list
        // immediately, and holding this Ptr only defers destruction of the object itself.
        juce::AudioProcessorGraph::Node::Ptr attenuverterNode;

        juce::ComboBox sourceCombo;
        juce::ComboBox destCombo;
        juce::Slider amountSlider;
        juce::Label amountValueLabel;
        std::unique_ptr<juce::DrawableButton> bypassToggle;
        std::unique_ptr<juce::DrawableButton> deleteButton;

        std::unique_ptr<juce::SliderParameterAttachment> amountAttachment;
        std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;

        std::map<int, float> gestureStartValues;

        void detach();
        void refresh(const AudioEngine::ModRoutingInfo& info);
        void populateCombos();
    };

    std::vector<std::unique_ptr<ModRow>> rows;
    juce::Viewport viewport;
    juce::Component contentContainer;

    void addModulation();

public:
    void updateRowsFromGraph();

private:
    int lastNodeCount = 0;
    int hoveredRow_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModMatrixComponent)
};
