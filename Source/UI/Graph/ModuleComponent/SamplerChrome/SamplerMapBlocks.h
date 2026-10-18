#pragma once

// The instrument map's picture: a keyboard on top and one row per velocity layer under it, with a block per
// recording spanning its keys. It is one Tab stop; the arrow keys move between blocks, Return asks for the fields,
// Delete removes the block and Escape closes the window. Recordings dropped on a block replace it, dropped on a key
// they are added there.

#include "Modules/Sampler/SamplerRegionEdit.h"
#include "UI/Layout/FadeVisibility.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

class SamplerMapBlock;

/** What a drop on the map asks for: replace region `replaceRegion`, or (-1) add one recording per path from `key`. */
struct MapDrop {
    int replaceRegion = -1;
    int key = 0;
    int loVel = 1, hiVel = 127;
    juce::StringArray paths;
};

class SamplerMapBlocks
    : public juce::Component
    , public juce::SettableTooltipClient
    , public juce::FileDragAndDropTarget {
public:
    SamplerMapBlocks();
    ~SamplerMapBlocks() override;

    /** Shows `specs` and selects region `selected` (-1 = none). */
    void setRegions(const std::vector<synth::sampler::RegionSpec>& specs, int selected);
    /** False: the map can be walked and read but a drop does nothing. */
    void setEditable(bool editable) { editable_ = editable; }

    int getSelected() const noexcept { return selected_; }
    /** Selects `index` (-1 = none) and tells onSelect when it changed. */
    void select(int index);
    int getBlockCount() const noexcept { return (int)blocks_.size(); }
    /** The block drawn for region `index`, or null. */
    juce::Button* getBlock(int index) const;
    /** The keys drawn, first to last. */
    juce::Range<int> getKeySpan() const noexcept { return span_; }
    /** The key under local x, or -1 left or right of the keyboard. */
    int keyAt(int x) const;
    /** The row (index into the layers) under local y, or -1. */
    int rowAt(int y) const;
    int getRowCount() const noexcept { return (int)layers_.size(); }
    /** What dropping `paths` at (x, y) asks for; no paths when the point is outside the keyboard and the rows. */
    MapDrop dropAt(int x, int y, const juce::StringArray& paths) const;

    std::function<void(int)> onSelect;
    std::function<void()> onOpenFields;
    std::function<void()> onRemove;
    std::function<void(const MapDrop&)> onDrop;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override { fileDragMove(files, x, y); }
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    friend class SamplerMapBlock;
    void rebuildBlocks();
    void layoutBlocks();
    juce::Rectangle<int> keysArea() const;
    juce::Rectangle<int> rowsArea() const;
    juce::Rectangle<int> blockBounds(int regionIndex, int row) const;
    float keyWidth() const;
    void paintKeys(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintRowLabels(juce::Graphics& g) const;
    void updateTitle();

    std::vector<synth::sampler::RegionSpec> specs_;
    std::vector<synth::sampler::MapLayer> layers_;
    std::vector<int> rowOfRegion_;
    std::vector<std::unique_ptr<SamplerMapBlock>> blocks_; // by region index
    juce::Range<int> span_{36, 71};
    int selected_ = -1;
    int dropKey_ = -1;
    int dropRegion_ = -1;
    bool editable_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerMapBlocks)
};

} // namespace synth::ui
