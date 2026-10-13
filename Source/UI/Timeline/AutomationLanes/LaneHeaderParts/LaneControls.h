#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <vector>

namespace synth::theme {
struct Theme;
}

namespace synth::ui {

class LaneControlStrip;

// The lane header's control chips: which actions a lane kind offers, the order they are listed in, the words and
// picture of each, and the set a kind shows on its header (the rest sit behind the "+" button). One set per lane KIND:
// every automation lane shows the same chips, every mod lane the same, and a change on one lane reaches them all.
enum class LaneControlKind { Automation, Mod };

// Every action a chip can run. Each maps onto an item the lane's right-click menu already has (Mute is the exception:
// on an automation lane it sets the record mode to Off and puts the previous mode back).
enum class LaneControl {
    ReadWrite,       // the record mode (Off / Read / Touch / Latch / Write)
    AddMod,          // add a modulator to the lane's parameter
    ChangeParameter, // point the lane at another parameter
    ChangeSource,    // point a mod lane at another modulation source
    Mute,            // automation lane: record mode Off, and back
    Duplicate,
    MoveToTrack, // opens the "Move to track" list
    ShowInCanvas,
    CreateCustomLfo, // automation lane: turn the range selection into an LFO
    Remove           // delete the lane / remove the modulator
};

struct LaneControlInfo {
    LaneControl id;
    const char* key;         // what the settings store
    const char* name;        // the chip's screen-reader name and the label on its "+" row button
    const char* description; // the tooltip, after "<name>: "
};

const LaneControlInfo& laneControlInfo(LaneControl control);
/** The name for a lane of `kind`: Remove reads "Remove lane" on an automation lane and "Remove modulator" on a mod
 * lane. */
juce::String laneControlName(LaneControl control, LaneControlKind kind);
/** "Name: description", worded for a lane of `kind`. */
juce::String laneControlTooltip(LaneControl control, LaneControlKind kind);
/** Every control the kind can offer, in the order the "+" row lists them. */
std::vector<LaneControl> laneControlCatalogue(LaneControlKind kind);
/** The set a kind starts with, in header order. */
std::vector<LaneControl> defaultLaneControls(LaneControlKind kind);
/** The colour of a control's picture. */
juce::Colour laneControlColour(LaneControl control, const synth::theme::Theme& theme);

/** Paints a control's picture on a 14 px grid scaled to the shorter side of `area`. `colour` is the main hue; AddMod
 *  and CreateCustomLfo keep their second hue. */
void paintLaneControlIcon(juce::Graphics& g, LaneControl control, juce::Rectangle<float> area, juce::Colour colour);
/** The "+" and the pencil that replaces it in edit mode. */
void paintLanePlusIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour);
void paintLanePencilIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour);

/** The comma list the settings keep for a set ("readWrite,addMod"), and its reading: ids the kind does not offer and
 *  repeats are dropped. */
juce::String serialiseLaneControls(const std::vector<LaneControl>& controls);
std::vector<LaneControl> parseLaneControls(LaneControlKind kind, const juce::String& text);
/** The settings key of a kind ("timelineLaneControls.automation"). */
const char* laneControlSettingsKey(LaneControlKind kind);

// What every lane header and mod row of one timeline shares: the sets of shown controls (kept in the user settings so
// they outlive the project and the session), the listeners that redraw when a set changes, the one lane that may be in
// edit mode, and the component the "+" row floats on. Message thread only. Owned by the timeline's lanes; headers hold
// a pointer, which may be null (they then show the defaults and nothing is saved).
class LaneControlSet {
public:
    struct Listener {
        virtual ~Listener() = default;
        virtual void laneControlsChanged(LaneControlKind kind) = 0;
    };

    /** Reads the sets from `props` (null: the defaults) and tells the listeners. `props` must outlive this. */
    void setProperties(juce::ApplicationProperties* props);
    /** The controls `kind` shows on its header, in order. */
    std::vector<LaneControl> shown(LaneControlKind kind) const;
    /** Replaces the set, saves it and tells the listeners; a set equal to the current one does nothing. */
    void setShown(LaneControlKind kind, std::vector<LaneControl> controls);

    void addListener(Listener* listener) { listeners_.add(listener); }
    void removeListener(Listener* listener) { listeners_.remove(listener); }

    /** The component the "+" row is added to (the timeline panel); null: the row has nowhere to float. */
    void setOverlayParent(juce::Component* parent) noexcept { overlayParent_ = parent; }
    juce::Component* getOverlayParent() const noexcept { return overlayParent_; }

    /** `strip` starts edit mode: any other strip in it leaves it first. */
    void claimEditing(LaneControlStrip* strip);
    void releaseEditing(LaneControlStrip* strip) noexcept;
    LaneControlStrip* getEditing() const noexcept { return editing_; }

private:
    std::optional<std::vector<LaneControl>>& slot(LaneControlKind kind) { return sets_[kind]; }

    juce::ApplicationProperties* props_ = nullptr;
    std::map<LaneControlKind, std::optional<std::vector<LaneControl>>> sets_; // unset: the defaults
    juce::ListenerList<Listener> listeners_;
    juce::Component* overlayParent_ = nullptr;
    LaneControlStrip* editing_ = nullptr;
};

} // namespace synth::ui
