#pragma once

#include "../AudioEngine.h"
#include "../UndoManager.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

class ModMatrixComponent
    : public juce::Component
    , public juce::Timer {
public:
    ModMatrixComponent(AudioEngine& engine, UndoManager* undoMgr = nullptr);
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

private:
    AudioEngine& audioEngine;
    UndoManager* undoManager = nullptr;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModMatrixComponent)
};
