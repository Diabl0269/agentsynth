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

        ModMatrixComponent& owner;
        juce::AudioProcessorGraph::NodeID attenuverterId;

        juce::ComboBox sourceCombo;
        juce::ComboBox destCombo;
        juce::Slider amountSlider;
        juce::TextButton bypassToggle{"B"};
        juce::TextButton deleteButton{"X"};

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
