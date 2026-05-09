#pragma once

#include "../AudioEngine.h"
#include "../GravisynthUndoManager.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ModuleComponent;
#include "ModMatrixComponent.h"

class GraphEditor
    : public juce::Component
    , public juce::Timer
    , public juce::DragAndDropTarget {
public:
    GraphEditor(AudioEngine& engine, GravisynthUndoManager* undoMgr = nullptr);
    ~GraphEditor() override;

    AudioEngine& getAudioEngine() { return audioEngine; }
    ModMatrixComponent& getModMatrix() { return modMatrix; }
    juce::OwnedArray<ModuleComponent>& getModuleComponents() { return content.getModules(); }
    void detachAllModuleComponents();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void timerCallback() override;
    void updateComponents();
    void toggleModMatrixVisibility();
    bool isModMatrixVisible() const { return isMatrixVisible; }

    // Interactions
    void beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                             juce::Point<int> screenPos);
    void dragConnection(juce::Point<int> screenPos);
    void endConnectionDrag(juce::Point<int> screenPos);
    void disconnectPort(ModuleComponent* module, int portIndex, bool isInput, bool isMidi);
    void deleteModule(ModuleComponent* module);
    void replaceModule(ModuleComponent* module, const juce::String& newModuleType);
    void updateModulePosition(ModuleComponent* module);

    // Preset Management
    void savePreset(juce::File file);
    void loadPreset(juce::File file);

    // DragAndDropTarget overrides
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;

    // Mouse Overrides
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    juce::AudioProcessorGraph::NodeID getAttenuverterNodeAt(juce::Point<float> localPos);

private:
    class GraphContentComponent : public juce::Component {
    public:
        GraphContentComponent(GraphEditor& editor);
        void paint(juce::Graphics& g) override;
        void resized() override;

        juce::OwnedArray<ModuleComponent>& getModules() { return moduleComponents; }

        float connectionAnimPhase = 0.0f;

    private:
        GraphEditor& editor;
        juce::OwnedArray<ModuleComponent> moduleComponents;
    };

    AudioEngine& audioEngine;
    GraphContentComponent content;
    ModMatrixComponent modMatrix;
    bool isMatrixVisible = false;

    // Navigation State
    float zoomLevel = 1.0f;
    juce::Point<float> panOffset;
    juce::Point<int> lastMousePos;

    // Drag State
    bool isDraggingConnection = false;
    ModuleComponent* dragSourceModule = nullptr;
    int dragSourceChannel = 0;
    bool dragSourceIsInput = false;
    bool dragSourceIsMidi = false;
    juce::Point<int> dragCurrentPos;

    juce::AudioProcessorGraph::NodeID draggingAttenuverterNodeId;
    float attenDragStartValue = 0.0f;

    GravisynthUndoManager* undoManager = nullptr;
    std::vector<AudioEngine::ModulationDisplayInfo> cachedModDisplayInfo;

    void updateTransform();

public:
    const std::vector<AudioEngine::ModulationDisplayInfo>& getCachedModDisplayInfo() const {
        return cachedModDisplayInfo;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphEditor)
};
