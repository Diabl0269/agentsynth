#pragma once

// The instrument map's picture: a keyboard on top and one row per velocity layer under it, with a block per
// recording spanning its keys. It is one Tab stop; the arrow keys move between blocks, Return asks for the fields,
// Delete removes the block and Escape closes the window. Recordings dropped on a block replace it, dropped on a key
// they are added there. Pressing a key of the keyboard plays it (onPlayKey, onReleaseKey); so does the play key on
// the selected block's root key. The owner decides what the notes reach.

#include "Modules/Sampler/SamplerRegionEdit.h"
#include "UI/Layout/FadeVisibility.h"
#include <bitset>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

class SamplerMapBlock;

} // namespace synth::ui

class ShortcutManager;

namespace synth::ui {

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
    /** True for a drum kit: each block is named by its pad ("Kick") and its tooltip and spoken name start with the
     *  pad and key ("Kick, C1"), for the keys that have a General MIDI drum. */
    void setDrumKit(bool drumKit);
    bool isDrumKit() const noexcept { return drumKit_; }

    /** Where the play key is read from; with none it is the plain P. Not owned. */
    void setShortcutManager(const ShortcutManager* shortcuts) noexcept { shortcuts_ = shortcuts; }
    /** The notes drawn as sounding; repaints only when they changed. */
    void setLitKeys(const std::bitset<128>& notes);
    /** The key the mouse or the play key holds down, or -1. */
    int getHeldKey() const noexcept { return mouseKey_ >= 0 ? mouseKey_ : playKey_; }

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
    /** A key goes down (velocity 64 at the key's top to 127 at its bottom, or the block's top velocity for the play
     *  key); each is followed by onReleaseKey for the same note. */
    std::function<void(int note, int velocity)> onPlayKey;
    std::function<void(int note)> onReleaseKey;
    /** Test seam: whether a key code is held down; null asks the keyboard. */
    std::function<bool(int keyCode)> isKeyDownProbe;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override;
    bool keyStateChanged(bool isKeyDown) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    /** What test mode reads (heldKey, litKeys) through the map's hidden child "samplerMapKeys". */
    void addTestModeState(juce::DynamicObject& into) const;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override { fileDragMove(files, x, y); }
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    friend class SamplerMapBlock;
    void rebuildBlocks();
    juce::String padNameFor(const synth::sampler::RegionSpec& spec) const;
    void layoutBlocks();
    juce::Rectangle<int> keysArea() const;
    juce::Rectangle<int> rowsArea() const;
    juce::Rectangle<int> blockBounds(int regionIndex, int row) const;
    float keyWidth() const;
    void paintKeys(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintRowLabels(juce::Graphics& g) const;
    void updateTitle();
    int velocityAt(int y) const;
    void pressKey(int note, int velocity);
    void releaseMouseKey();
    void releasePlayKey();
    bool isPlayKey(const juce::KeyPress& key) const;
    juce::KeyPress playBinding() const;

    std::vector<synth::sampler::RegionSpec> specs_;
    std::vector<synth::sampler::MapLayer> layers_;
    std::vector<int> rowOfRegion_;
    std::vector<std::unique_ptr<SamplerMapBlock>> blocks_; // by region index
    juce::Range<int> span_{36, 71};
    int selected_ = -1;
    int dropKey_ = -1;
    int dropRegion_ = -1;
    bool editable_ = true;
    bool drumKit_ = false;
    const ShortcutManager* shortcuts_ = nullptr;
    std::bitset<128> lit_;
    int mouseKey_ = -1;
    int playKey_ = -1;
    std::unique_ptr<juce::Component> probe_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerMapBlocks)
};

} // namespace synth::ui
