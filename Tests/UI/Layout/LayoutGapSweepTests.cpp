// LayoutGapSweepTests.cpp -- the guard for the layout-overlap bug kind on whole surfaces: every open focus region of a
// real MainComponent (each main view in turn, a piano roll and a drum machine open on a clip) and every dialog of the
// theme sweep registry is checked for parts that touch, captions that sit at the wrong distance from their control,
// text that does not fit and buttons whose label is off centre. Surfaces are discovered, never listed here.
//
// Swept: the default window size in every built-in theme, and the smallest size (the main window at its enforced
// minimum, a dialog at its constrainer's minimum) in the first built-in theme only, to keep the run short.
//
// What counts as a visible leaf: a Slider, ComboBox, Button, Label or TextEditor (never looked into: a slider's value
// box is part of the slider), or any other component with no visible child. It is showing, has a size, no ancestor is
// hidden or fully transparent, and its rectangle is clipped to every ancestor's bounds, so content scrolled out of a
// viewport is not judged.
//
// 1. Touching. Two leaves with the same parent must have at least 1 px between them (0 px is touching). Skipped: a pair
//    where either part takes no mouse clicks at all (a deliberate overlay laid over a sibling, such as a painted layer
//    or a caption drawn over a control), a pair of buttons joined on the facing edges (getConnectedEdgeFlags: they
//    touch by design, but must not overlap), and a caption/control pair, which rule 2 judges. A child inside its parent
//    is never a pair: only siblings are compared, and a leaf has no children.
// 2. Caption gap. The pair for a juce::Label is its attached component when it has one; otherwise the nearest Slider,
//    ComboBox, Button or TextEditor directly below it (horizontal overlap) or directly to its right (vertical overlap)
//    within kPairReach px. The distance between the two must be kCaptionGap, the design system's `space-unit`
//    (theme.metrics.spacingUnit, 6 px, tokens.json "spacing"), so a caption touching its control or floating away from
//    it both fail. A pair that overlaps fails as "overlaps".
// 3. Text fits. A Label, TextButton or ComboBox whose text, in the font its look and feel draws it with, is wider than
// its
//    text area (a Label may squeeze to its minimum horizontal scale or wrap onto a second line) must have a non-empty
//    tooltip. The text area is the one AppLookAndFeel gives: a TextButton's width minus 4 px each side and the reserved
//    right inset, a ComboBox's own text label.
// 4. Button content centred. A TextButton is painted twice, with and without its text; the pixels that differ are the
//    text ink, and the gap from that ink to the left and to the right edge of the button must differ by at most 1 px.
//    Only TextButton text is measured: an icon or a toggle's tick has no content rectangle to ask for.
//
// A failure prints "<region id or dialog name> / <component description>: <what>"; the main window's smallest size adds
// " [min]" to the surface name. Digits in a description are written as '#', so a value that changes does not change the
// key. A gap that is known and ticketed lives in kKnownGaps; the list may only shrink (docs/layout/module-card.md).
#include "../../TestWait.h"
#include "../Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "KeyboardReachFixture.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ThemeManager.h"
#if defined(__GNUG__)
#include <cxxabi.h>
#endif
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <typeinfo>

using synth::test::theme_sweep::dialogSurfaces;
using synth::test::theme_sweep::SweepEnvironment;
using Tab = synth::ui::BottomDockComponent::Tab;

namespace {

// A gap that is known and has a ticket. Key: "<region id or dialog name> / <component>: <what>" as the failure prints
// it.
struct KnownGap {
    const char* key;
    const char* ticket;
};

const std::vector<KnownGap> kKnownGaps = {
    // Export audio
    {"Export audio / Label '<folder path>': its text does not fit and it has no tooltip", "FRO1712"},
    {"Export audio / Label 'Bit depth': is not 6 px from its control ComboBox 'Bit depth'", "FRO1712"},
    {"Export audio / Label 'File name': is not 6 px from its control TextEditor 'File name'", "FRO1712"},
    {"Export audio / Label 'Format': is not 6 px from its control ComboBox 'Format'", "FRO1712"},
    {"Export audio / Label 'Range': is not 6 px from its control ToggleButton 'Whole arrangement'", "FRO1712"},
    {"Export audio / Label 'Sample rate': is not 6 px from its control ComboBox 'Sample rate'", "FRO1712"},
    {"Export audio / Label 'Tail': is not 6 px from its control ExpandingRangeSlider 'Tail length'", "FRO1712"},
    {"Export audio / ToggleButton 'Whole arrangement': touches ToggleButton 'Current loop range'", "FRO1712"},
    // Export stems
    {"Export stems / Label '<folder path>': its text does not fit and it has no tooltip", "FRO1712"},
    {"Export stems / Label 'Bit depth': is not 6 px from its control ComboBox 'Bit depth'", "FRO1712"},
    {"Export stems / Label 'Folder name': is not 6 px from its control TextEditor 'Folder name'", "FRO1712"},
    {"Export stems / Label 'Format': is not 6 px from its control ComboBox 'Format'", "FRO1712"},
    {"Export stems / Label 'Range': is not 6 px from its control ToggleButton 'Whole arrangement'", "FRO1712"},
    {"Export stems / Label 'Sample rate': is not 6 px from its control ComboBox 'Sample rate'", "FRO1712"},
    {"Export stems / Label 'Tail': is not 6 px from its control ExpandingRangeSlider 'Tail length'", "FRO1712"},
    {"Export stems / ToggleButton 'Whole arrangement': touches ToggleButton 'Current loop range'", "FRO1712"},
    // Macro configure I/O
    {"Macro configure I/O / Label 'Add a port': is not 6 px from its control ComboBox 'New port direction'", "FRO1713"},
    {"Macro configure I/O / Label 'Add a port': touches ComboBox 'New port kind'", "FRO1713"},
    {"Macro configure I/O / Label 'Add a port': touches ComboBox 'New port shape'", "FRO1713"},
    {"Macro configure I/O / Label 'Inputs': is not 6 px from its control TextEditor 'Port name'", "FRO1713"},
    {"Macro configure I/O / Label 'Outputs': is not 6 px from its control TextEditor 'Port name'", "FRO1713"},
    {"Macro configure I/O [min] / Label 'Add a port': is not 6 px from its control ComboBox 'New port direction'",
     "FRO1713"},
    // Macro ports prompt
    {"Macro ports prompt / Label 'This macro will need # ports': is not 6 px from its control ToggleButton 'Remember "
     "my choice'",
     "FRO1713"},
    {"Macro ports prompt [min] / Label 'Macro has boundary cables': its text does not fit and it has no tooltip",
     "FRO1713"},
    // Modulation curve
    {"Modulation curve / ModCurvePresetButton 'Exp curve': touches ModCurvePresetButton 'Log curve'", "FRO1713"},
    {"Modulation curve / ModCurvePresetButton 'Linear curve': touches ModCurvePresetButton 'Exp curve'", "FRO1713"},
    {"Modulation curve / ModCurvePresetButton 'Log curve': touches ModCurvePresetButton 'S-curve curve'", "FRO1713"},
    {"Modulation curve [min] / ModCurvePresetButton 'Linear curve': touches ModCurvePresetButton 'Exp curve'",
     "FRO1713"},
    // Render progress
    {"Render progress / Label 'Render status': touches ProgressBar 'Render progress'", "FRO1712"},
    {"Render progress [min] / Label 'Render status': its text does not fit and it has no tooltip", "FRO1712"},
    {"Render progress [min] / Label 'Render status': touches ProgressBar 'Render progress'", "FRO1712"},
    // Sampler map
    {"Sampler map / Label 'Select a recording above to ': is not 6 px from its control TextButton 'Add recording...'",
     "FRO1713"},
    {"Sampler map [min] / Label 'Select a recording above to ': is not 6 px from its control TextButton 'Add "
     "recording...'",
     "FRO1713"},
    // Settings
    {"Settings / Label 'Input': is not 6 px from its control ComboBox 'Input'", "FRO1713"},
    {"Settings / Label 'Jacks come in neighbouring p': is not 6 px from its control FoldSectionHeader 'Remembered "
     "interfaces, expan'",
     "FRO1713"},
    {"Settings / Label 'No audio interface has been ': is not 6 px from its control FoldSectionHeader 'Outputs, "
     "collapsed'",
     "FRO1713"},
    {"Settings / Label 'Output': is not 6 px from its control ComboBox 'Output'", "FRO1713"},
    {"Settings [min] / Label 'Input': is not 6 px from its control ComboBox 'Input'", "FRO1713"},
    {"Settings [min] / Label 'Output': is not 6 px from its control ComboBox 'Output'", "FRO1713"},
    // Shape editor
    {"Shape editor / Label 'Grid': is not 6 px from its control TextButton 'Off'", "FRO1713"},
    {"Shape editor / Label 'Level': is not 6 px from its control TextButton 'Off'", "FRO1713"},
    // Sign in
    {"Sign in / Label 'Starting...': is not 6 px from its control TextButton 'Open in Browser'", "FRO1713"},
    {"Sign in / Label: is not 6 px from its control TextButton 'Cancel'", "FRO1713"},
    {"Sign in [min] / Label 'Starting...': is not 6 px from its control TextButton 'Open in Browser'", "FRO1713"},
    {"Sign in [min] / Label 'Starting...': its text does not fit and it has no tooltip", "FRO1713"},
    {"Sign in [min] / TextButton 'Open in Browser': overlaps TextButton 'Cancel'", "FRO1713"},
    // canvas
    {"canvas / Label 'Gain': is not 6 px from its control CardKnobSlider 'Gain'", "FRO1713"},
    // mixer
    {"mixer / CursorDelegatingLabel 'Audio # name': is not 6 px from its control TrackShowModuleButton 'Edit Audio # "
     "on canvas'",
     "FRO1711"},
    {"mixer / CursorDelegatingLabel 'Audio # name': touches MixerColourDot 'Audio # colour'", "FRO1711"},
    {"mixer / CursorDelegatingLabel 'Master name': is not 6 px from its control TrackShowModuleButton 'Edit Master on "
     "canvas'",
     "FRO1711"},
    {"mixer / Label 'Audio # fader level': is not 6 px from its control TextButton> 'Audio # mute, off'", "FRO1711"},
    {"mixer / Label 'Master fader level': is not 6 px from its control TextButton 'Master mute, off'", "FRO1711"},
    {"mixer / MixerEqThumbnail 'Audio # EQ curve': touches MixerSectionDivider 'Resize EQ'", "FRO1711"},
    {"mixer / MixerEqThumbnail 'Audio # EQ curve': touches MixerSectionDivider 'Resize Sends'", "FRO1711"},
    {"mixer / MixerFaderSlider 'Audio # fader': touches Label 'Audio # fader level'", "FRO1711"},
    {"mixer / MixerFaderSlider 'Master fader': touches Label 'Master fader level'", "FRO1711"},
    {"mixer / MixerMeter 'Audio # meter': touches MixerMeterReadout 'Audio # peak'", "FRO1711"},
    {"mixer / MixerMeter 'Master meter': touches MixerMeterReadout 'Master peak'", "FRO1711"},
    {"mixer / MixerSectionDivider 'Resize EQ': touches Slider 'Audio # pan'", "FRO1711"},
    {"mixer / Slider 'Audio # pan': touches MixerMeterReadout 'Audio # peak'", "FRO1711"},
    {"mixer / TextButton 'Master mute, off': touches TextButton 'Mixer pan law (this project)'", "FRO1711"},
    // timeline
    {"timeline / Bodies 'automationLaneBodies': overlaps EdgeResizeHandle 'Track column width'", "FRO1710"},
    {"timeline / Bodies 'automationLaneBodies': touches TimelineRulerComponent 'timelineRuler'", "FRO1710"},
    {"timeline / BpmDragLabel 'timelineTransportBpmLabel': is not 6 px from its control SongKeyField 'Song key'",
     "FRO1710"},
    {"timeline / ChannelChipComponent 'trackChannelChip': overlaps EdgeResizeHandle 'Resize Audio #'", "FRO1710"},
    {"timeline / ContextMenuForwardingLabel 'trackNameLabel': is not 6 px from its control ContextMenuForwardingButton "
     "'trackMuteButton'",
     "FRO1710"},
    {"timeline / EdgeResizeHandle 'Track column width': overlaps TimelineRulerComponent 'timelineRuler'", "FRO1710"},
    {"timeline / NonModalLabel 'timelineTransportTimeSigLabe': is not 6 px from its control SongKeyField 'Song key'",
     "FRO1710"},
    {"timeline / NonModalLabel 'timelineTransportTimeSigLabe': overlaps SongKeyField 'Song key'", "FRO1710"},
    {"timeline / SwatchButton 'trackColourSwatch': overlaps EdgeResizeHandle 'Resize Audio #'", "FRO1710"},
    {"timeline / SwatchButton 'trackColourSwatch': overlaps EdgeResizeHandle 'Resize Track #'", "FRO1710"},
    {"timeline / TextButton 'trackBindingChip': overlaps EdgeResizeHandle 'Resize Audio #'", "FRO1710"},
    {"timeline / TextButton 'trackBindingChip': overlaps EdgeResizeHandle 'Resize Track #'", "FRO1710"},
    {"timeline / TimelineRulerComponent 'timelineRuler': overlaps EdgeResizeHandle 'Track column width'", "FRO1710"},
    {"timeline / TimelineRulerComponent 'timelineRuler': touches Bodies 'automationLaneBodies'", "FRO1710"},
    {"timeline / ToolbarButton 'Add': overlaps EdgeResizeHandle 'Track column width'", "FRO1710"},
    {"timeline [drum machine open] / BpmDragLabel 'timelineTransportBpmLabel': is not 6 px from its control "
     "ToolbarButton 'Add'",
     "FRO1710"},
    {"timeline [drum machine open] / CardStepper 'Swing': overlaps CardKnobSlider 'Humanise'", "FRO1710"},
    {"timeline [drum machine open] / ChannelChipComponent 'trackChannelChip': overlaps EdgeResizeHandle 'Resize Audio "
     "#'",
     "FRO1710"},
    {"timeline [drum machine open] / ComboBox 'Steps per bar': overlaps CardKnobSlider 'Humanise'", "FRO1710"},
    {"timeline [drum machine open] / ComboBox 'Steps per bar': overlaps CardStepper 'Swing'", "FRO1710"},
    {"timeline [drum machine open] / ContextMenuForwardingLabel 'trackNameLabel': is not 6 px from its control "
     "ContextMenuForwardingButton 'trackMuteButton'",
     "FRO1710"},
    {"timeline [drum machine open] / EdgeResizeHandle 'Track column width': overlaps TimelineRulerComponent "
     "'timelineRuler'",
     "FRO1710"},
    {"timeline [drum machine open] / NameButton 'Clap row': touches NameButton 'Closed hat row'", "FRO1710"},
    {"timeline [drum machine open] / NameButton 'Closed hat row': touches NameButton 'Open hat row'", "FRO1710"},
    {"timeline [drum machine open] / NameButton 'Cymbal row': touches NameButton 'Cowbell row'", "FRO1710"},
    {"timeline [drum machine open] / NameButton 'Kick row': touches NameButton 'Snare row'", "FRO1710"},
    {"timeline [drum machine open] / NameButton 'Open hat row': touches NameButton 'Tom row'", "FRO1710"},
    {"timeline [drum machine open] / NameButton 'Snare row': touches NameButton 'Clap row'", "FRO1710"},
    {"timeline [drum machine open] / NameButton 'Tom row': touches NameButton 'Cymbal row'", "FRO1710"},
    {"timeline [drum machine open] / NonModalLabel 'timelineTransportTimeSigLabe': is not 6 px from its control "
     "ToolbarButton 'Add'",
     "FRO1710"},
    {"timeline [drum machine open] / NonModalLabel 'timelineTransportTimeSigLabe': overlaps SongKeyField 'Song key'",
     "FRO1710"},
    {"timeline [drum machine open] / SwatchButton 'trackColourSwatch': overlaps EdgeResizeHandle 'Resize Audio #'",
     "FRO1710"},
    {"timeline [drum machine open] / SwatchButton 'trackColourSwatch': overlaps EdgeResizeHandle 'Resize Track #'",
     "FRO1710"},
    {"timeline [drum machine open] / TextButton 'trackBindingChip': overlaps EdgeResizeHandle 'Resize Audio #'",
     "FRO1710"},
    {"timeline [drum machine open] / TextButton 'trackBindingChip': overlaps EdgeResizeHandle 'Resize Track #'",
     "FRO1710"},
    {"timeline [drum machine open] / ToolbarButton 'Add': overlaps EdgeResizeHandle 'Track column width'", "FRO1710"},
    {"timeline [min] / Bodies 'automationLaneBodies': overlaps EdgeResizeHandle 'Track column width'", "FRO1710"},
    {"timeline [min] / Bodies 'automationLaneBodies': touches TimelineRulerComponent 'timelineRuler'", "FRO1710"},
    {"timeline [min] / ChannelChipComponent 'trackChannelChip': overlaps EdgeResizeHandle 'Resize Audio #'", "FRO1710"},
    {"timeline [min] / ContextMenuForwardingLabel 'trackNameLabel': is not 6 px from its control "
     "ContextMenuForwardingButton 'trackMuteButton'",
     "FRO1710"},
    {"timeline [min] / EdgeResizeHandle 'Track column width': overlaps TimelineRulerComponent 'timelineRuler'",
     "FRO1710"},
    {"timeline [min] / SwatchButton 'trackColourSwatch': overlaps EdgeResizeHandle 'Resize Audio #'", "FRO1710"},
    {"timeline [min] / SwatchButton 'trackColourSwatch': overlaps EdgeResizeHandle 'Resize Track #'", "FRO1710"},
    {"timeline [min] / TextButton 'trackBindingChip': overlaps EdgeResizeHandle 'Resize Audio #'", "FRO1710"},
    {"timeline [min] / TextButton 'trackBindingChip': overlaps EdgeResizeHandle 'Resize Track #'", "FRO1710"},
    {"timeline [min] / ToolbarButton 'Add': overlaps EdgeResizeHandle 'Track column width'", "FRO1710"},
    {"timeline [piano roll open] / BpmDragLabel 'timelineTransportBpmLabel': is not 6 px from its control "
     "ToolbarButton 'Add'",
     "FRO1710"},
    {"timeline [piano roll open] / ChannelChipComponent 'trackChannelChip': overlaps EdgeResizeHandle 'Resize Audio #'",
     "FRO1710"},
    {"timeline [piano roll open] / ContextMenuForwardingLabel 'trackNameLabel': is not 6 px from its control "
     "ContextMenuForwardingButton 'trackMuteButton'",
     "FRO1710"},
    {"timeline [piano roll open] / EdgeResizeHandle 'Track column width': overlaps TimelineRulerComponent "
     "'timelineRuler'",
     "FRO1710"},
    {"timeline [piano roll open] / NonModalLabel 'timelineTransportTimeSigLabe': is not 6 px from its control "
     "ToolbarButton 'Add'",
     "FRO1710"},
    {"timeline [piano roll open] / NonModalLabel 'timelineTransportTimeSigLabe': overlaps SongKeyField 'Song key'",
     "FRO1710"},
    {"timeline [piano roll open] / SwatchButton 'trackColourSwatch': overlaps EdgeResizeHandle 'Resize Audio #'",
     "FRO1710"},
    {"timeline [piano roll open] / SwatchButton 'trackColourSwatch': overlaps EdgeResizeHandle 'Resize Track #'",
     "FRO1710"},
    {"timeline [piano roll open] / TextButton 'trackBindingChip': overlaps EdgeResizeHandle 'Resize Audio #'",
     "FRO1710"},
    {"timeline [piano roll open] / TextButton 'trackBindingChip': overlaps EdgeResizeHandle 'Resize Track #'",
     "FRO1710"},
    {"timeline [piano roll open] / ToolbarButton 'Add': overlaps EdgeResizeHandle 'Track column width'", "FRO1710"},
};

constexpr int kPairReach = 24;       // how far below or to the right of a caption its control may be
constexpr int kMinWindowWidth = 480; // the main window's enforced minimum (Main.cpp setResizeLimits)
constexpr int kMinWindowHeight = 400;

struct Leaf {
    juce::Component* component;
    juce::Rectangle<int> area; // in the surface root's coordinates, clipped to every ancestor
};

std::string className(const juce::Component& c) {
#if defined(__GNUG__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(typeid(c).name(), nullptr, nullptr, &status);
    std::string name = status == 0 && demangled != nullptr ? demangled : typeid(c).name();
    std::free(demangled);
#else
    std::string name = typeid(c).name();
    for (const char* prefix : {"class ", "struct "})
        if (name.rfind(prefix, 0) == 0)
            name = name.substr(std::char_traits<char>::length(prefix));
#endif
    const auto colons = name.rfind("::");
    return colons == std::string::npos ? name : name.substr(colons + 2);
}

// Class and name, with digits as '#': the same text on every run and every theme.
std::string describe(juce::Component& c) {
    juce::String name = c.getTitle();
    if (name.isEmpty())
        name = c.getComponentID();
    if (name.isEmpty())
        if (auto* button = dynamic_cast<juce::Button*>(&c))
            name = button->getButtonText();
    if (name.isEmpty())
        if (auto* label = dynamic_cast<juce::Label*>(&c))
            name = label->getText();
    // A folder path differs on every machine and run (the export dialogs show one), so it is one fixed word.
    if (name.startsWithChar('/') || name.matchesWildcard("?:\\*", false))
        name = "<folder path>";
    juce::String clean;
    for (auto ch : name.substring(0, 28))
        clean += juce::CharacterFunctions::isDigit(ch) ? juce::juce_wchar('#') : ch;
    return className(c) + (clean.isEmpty() ? "" : " '" + clean.toStdString() + "'");
}

bool isControl(const juce::Component& c) {
    return dynamic_cast<const juce::Slider*>(&c) != nullptr || dynamic_cast<const juce::ComboBox*>(&c) != nullptr ||
           dynamic_cast<const juce::Button*>(&c) != nullptr || dynamic_cast<const juce::Label*>(&c) != nullptr ||
           dynamic_cast<const juce::TextEditor*>(&c) != nullptr;
}

bool showing(const juce::Component& c) {
    return c.isVisible() && c.getAlpha() > 0.0f && c.getWidth() > 0 && c.getHeight() > 0;
}

bool hasShowingChild(const juce::Component& c) {
    for (auto* child : c.getChildren())
        if (showing(*child))
            return true;
    return false;
}

// `c`'s rectangle in `root`'s coordinates, clipped to every ancestor up to and including `root`; empty when not shown.
juce::Rectangle<int> clippedArea(juce::Component& root, juce::Component& c) {
    auto area = root.getLocalArea(&c, c.getLocalBounds());
    for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent()) {
        if (!showing(*p) && p != &root)
            return {};
        area = area.getIntersection(root.getLocalArea(p, p->getLocalBounds()));
        if (p == &root)
            return area;
    }
    return {};
}

void collectLeaves(juce::Component& root, juce::Component& node, const std::function<bool(juce::Component&)>& owns,
                   std::vector<Leaf>& out) {
    for (auto* child : node.getChildren()) {
        if (!showing(*child))
            continue;
        const auto area = clippedArea(root, *child);
        if (area.isEmpty())
            continue;
        if (isControl(*child) || !hasShowingChild(*child)) {
            if (owns(*child))
                out.push_back({child, area});
        } else {
            collectLeaves(root, *child, owns, out);
        }
    }
}

bool takesClicks(juce::Component& c) {
    bool self = true, children = true;
    c.getInterceptsMouseClicks(self, children);
    return self || children;
}

// The space between two rectangles along one axis when they share the other; negative when they overlap; nothing when
// they are diagonal.
std::optional<int> gapBetween(juce::Rectangle<int> a, juce::Rectangle<int> b) {
    if (a.intersects(b))
        return -1;
    const bool shareX = a.getX() < b.getRight() && b.getX() < a.getRight();
    const bool shareY = a.getY() < b.getBottom() && b.getY() < a.getBottom();
    if (shareX)
        return juce::jmax(a.getY() - b.getBottom(), b.getY() - a.getBottom());
    if (shareY)
        return juce::jmax(a.getX() - b.getRight(), b.getX() - a.getRight());
    return std::nullopt;
}

struct CaptionPair {
    const Leaf* label;
    const Leaf* control;
};

bool isPairable(const juce::Component& c) {
    return dynamic_cast<const juce::Slider*>(&c) != nullptr || dynamic_cast<const juce::ComboBox*>(&c) != nullptr ||
           dynamic_cast<const juce::Button*>(&c) != nullptr || dynamic_cast<const juce::TextEditor*>(&c) != nullptr;
}

std::vector<CaptionPair> captionPairs(const std::vector<Leaf>& leaves) {
    std::vector<CaptionPair> pairs;
    for (const auto& leaf : leaves) {
        auto* label = dynamic_cast<juce::Label*>(leaf.component);
        if (label == nullptr)
            continue;
        const Leaf* best = nullptr;
        int bestDistance = kPairReach + 1;
        if (auto* attached = label->getAttachedComponent()) {
            for (const auto& other : leaves)
                if (other.component == attached)
                    best = &other;
        } else {
            for (const auto& other : leaves) {
                if (!isPairable(*other.component))
                    continue;
                const auto a = leaf.area;
                const auto b = other.area;
                const bool shareX = a.getX() < b.getRight() && b.getX() < a.getRight();
                const bool shareY = a.getY() < b.getBottom() && b.getY() < a.getBottom();
                int distance = kPairReach + 1;
                if (shareX && b.getY() >= a.getBottom())
                    distance = b.getY() - a.getBottom();
                else if (shareY && b.getX() >= a.getRight())
                    distance = b.getX() - a.getRight();
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = &other;
                }
            }
        }
        if (best != nullptr)
            pairs.push_back({&leaf, best});
    }
    return pairs;
}

// Findings of one run: "<surface> / <component>: <what>", with the rule that raised each for the run's summary.
class Findings {
public:
    void add(const std::string& surface, const std::string& component, const std::string& what, const char* rule) {
        const auto key = surface + " / " + component + ": " + what;
        found_.insert(key);
        ++perRule_[rule];
    }
    void walked(const std::string& surface, size_t leaves) {
        walked_.insert(surface);
        leaves_ += leaves;
    }
    void gap(int px) { ++gapHistogram_[px]; }
    void measuredButton() { ++measuredButtons_; }
    size_t measuredButtons() const { return measuredButtons_; }

    void report() const {
        std::cout << "[ layout gaps ] " << walked_.size() << " surfaces, " << leaves_ << " leaves checked";
        for (const auto& [rule, count] : perRule_)
            std::cout << ", " << rule << ": " << count;
        std::cout << "\n[ layout gaps ] caption gaps (px: pairs):";
        for (const auto& [px, count] : gapHistogram_)
            std::cout << " " << px << ":" << count;
        std::cout << "\n";
    }

    // An unlisted finding fails; a listed one that is gone fails too, so the list only shrinks. A known gap on a
    // surface this run did not walk is not judged.
    void expectOnlyKnownGaps() const {
        report();
        std::set<std::string> listed;
        for (const auto& gap : kKnownGaps) {
            const std::string key = gap.key;
            listed.insert(key);
            if (walked_.count(key.substr(0, key.find(" / "))) != 0 && found_.count(key) == 0)
                ADD_FAILURE() << "fixed - remove it from kKnownGaps: " << key << " (" << gap.ticket << ")";
        }
        for (const auto& key : found_)
            if (listed.count(key) == 0)
                ADD_FAILURE() << key;
    }

private:
    std::set<std::string> found_;
    std::set<std::string> walked_;
    std::map<std::string, int> perRule_;
    std::map<int, int> gapHistogram_;
    size_t leaves_ = 0;
    size_t measuredButtons_ = 0;
};

bool connectedAlong(const juce::Component& a, const juce::Component& b, juce::Rectangle<int> ra,
                    juce::Rectangle<int> rb) {
    auto* ba = dynamic_cast<const juce::Button*>(&a);
    auto* bb = dynamic_cast<const juce::Button*>(&b);
    if (ba == nullptr || bb == nullptr)
        return false;
    if (ra.getRight() == rb.getX() && ba->isConnectedOnRight() && bb->isConnectedOnLeft())
        return true;
    if (rb.getRight() == ra.getX() && bb->isConnectedOnRight() && ba->isConnectedOnLeft())
        return true;
    if (ra.getBottom() == rb.getY() && ba->isConnectedOnBottom() && bb->isConnectedOnTop())
        return true;
    return rb.getBottom() == ra.getY() && bb->isConnectedOnBottom() && ba->isConnectedOnTop();
}

void checkTouching(Findings& f, const std::string& surface, const std::vector<Leaf>& leaves,
                   const std::vector<CaptionPair>& pairs) {
    const auto isCaptionPair = [&](const Leaf& a, const Leaf& b) {
        for (const auto& p : pairs)
            if ((p.label == &a && p.control == &b) || (p.label == &b && p.control == &a))
                return true;
        return false;
    };
    for (size_t i = 0; i < leaves.size(); ++i)
        for (size_t j = i + 1; j < leaves.size(); ++j) {
            const auto &a = leaves[i], &b = leaves[j];
            if (a.component->getParentComponent() != b.component->getParentComponent())
                continue;
            if (!takesClicks(*a.component) || !takesClicks(*b.component) || isCaptionPair(a, b))
                continue;
            if (a.area.intersects(b.area)) {
                f.add(surface, describe(*a.component), "overlaps " + describe(*b.component), "touching");
            } else if (a.area.expanded(1).intersects(b.area) &&
                       !connectedAlong(*a.component, *b.component, a.area, b.area)) {
                f.add(surface, describe(*a.component), "touches " + describe(*b.component), "touching");
            }
        }
}

void checkCaptionGaps(Findings& f, const std::string& surface, const std::vector<CaptionPair>& pairs, int token) {
    for (const auto& pair : pairs) {
        const auto gap = gapBetween(pair.label->area, pair.control->area);
        if (!gap.has_value())
            continue; // diagonal: a label that names its control from a corner
        f.gap(*gap);
        if (*gap < 0)
            f.add(surface, describe(*pair.label->component),
                  "overlaps its control " + describe(*pair.control->component), "caption gap");
        else if (*gap != token)
            f.add(surface, describe(*pair.label->component),
                  "is not " + std::to_string(token) + " px from its control " + describe(*pair.control->component),
                  "caption gap");
    }
}

float textWidth(const juce::Font& font, const juce::String& text) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}

bool hasTooltip(juce::Component& c) {
    auto* client = dynamic_cast<juce::SettableTooltipClient*>(&c);
    return client != nullptr && client->getTooltip().isNotEmpty();
}

// The width of the text, and the width it has, for a control whose text can be cut off; nothing for other components.
struct TextFit {
    float needed;
    float available;
};

std::optional<TextFit> textFitOf(juce::Component& c) {
    if (auto* label = dynamic_cast<juce::Label*>(&c)) {
        if (label->getText().isEmpty())
            return std::nullopt;
        const auto area = label->getBorderSize().subtractedFrom(label->getLocalBounds());
        const auto font = label->getFont();
        if ((int)((float)area.getHeight() / font.getHeight()) >= 2)
            return std::nullopt; // wraps onto a second line
        const float scale = label->getMinimumHorizontalScale() > 0.0f ? label->getMinimumHorizontalScale() : 1.0f;
        return TextFit{textWidth(font, label->getText()), (float)area.getWidth() / scale};
    }
    if (auto* button = dynamic_cast<juce::TextButton*>(&c)) {
        if (button->getButtonText().isEmpty())
            return std::nullopt;
        const int inset = (int)button->getProperties()[synth::theme::AppLookAndFeel::kTextInsetRightProperty];
        const auto font = button->getLookAndFeel().getTextButtonFont(*button, button->getHeight());
        return TextFit{textWidth(font, button->getButtonText()), (float)(button->getWidth() - 8 - inset)};
    }
    if (auto* combo = dynamic_cast<juce::ComboBox*>(&c)) {
        if (combo->getText().isEmpty())
            return std::nullopt;
        for (auto* child : combo->getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child))
                return TextFit{textWidth(label->getFont(), combo->getText()),
                               (float)label->getBorderSize().subtractedFrom(label->getLocalBounds()).getWidth()};
    }
    return std::nullopt;
}

void checkTextFits(Findings& f, const std::string& surface, const std::vector<Leaf>& leaves) {
    for (const auto& leaf : leaves) {
        const auto fit = textFitOf(*leaf.component);
        if (fit.has_value() && fit->needed > fit->available && !hasTooltip(*leaf.component))
            f.add(surface, describe(*leaf.component), "its text does not fit and it has no tooltip", "text fit");
    }
}

// The horizontal extent of the pixels of `button`'s text: the pixels that differ when the text is left out.
std::optional<std::pair<int, int>> textInk(juce::TextButton& button) {
    const auto text = button.getButtonText();
    const auto paint = [&button] {
        juce::Image image(juce::Image::ARGB, button.getWidth(), button.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        button.paintEntireComponent(g, false);
        return image;
    };
    const auto with = paint();
    button.setButtonText({});
    const auto without = paint();
    button.setButtonText(text);
    int left = button.getWidth(), right = -1;
    for (int y = 0; y < with.getHeight(); ++y)
        for (int x = 0; x < with.getWidth(); ++x)
            if (with.getPixelAt(x, y) != without.getPixelAt(x, y)) {
                left = juce::jmin(left, x);
                right = juce::jmax(right, x);
            }
    if (right < 0)
        return std::nullopt;
    return std::make_pair(left, right);
}

void checkButtonsCentred(Findings& f, const std::string& surface, const std::vector<Leaf>& leaves) {
    for (const auto& leaf : leaves) {
        auto* button = dynamic_cast<juce::TextButton*>(leaf.component);
        if (button == nullptr || button->getButtonText().isEmpty() || button->getWidth() < 4)
            continue;
        const auto fit = textFitOf(*button);
        if (fit.has_value() && fit->needed > fit->available)
            continue; // cut off: rule 3 judges it
        const auto ink = textInk(*button);
        if (!ink.has_value())
            continue;
        f.measuredButton();
        const int leftInset = ink->first, rightInset = button->getWidth() - 1 - ink->second;
        if (std::abs(leftInset - rightInset) > 1)
            f.add(surface, describe(*button), "its text is not centred", "centred");
    }
}

void checkSurface(Findings& f, const std::string& surface, juce::Component& root,
                  const std::function<bool(juce::Component&)>& owns, int token) {
    std::vector<Leaf> leaves;
    collectLeaves(root, root, owns, leaves);
    f.walked(surface, leaves.size());
    const auto pairs = captionPairs(leaves);
    checkTouching(f, surface, leaves, pairs);
    checkCaptionGaps(f, surface, pairs, token);
    checkTextFits(f, surface, leaves);
    checkButtonsCentred(f, surface, leaves);
}

class LayoutGapShell : public KeyboardReachFixture {
protected:
    void SetUp() override {
        MainComponentTest::SetUp(); // the shell is built below with an look and feel this test owns
        resetGuard_.emplace();
        previousDefault_ = &juce::LookAndFeel::getDefaultLookAndFeel();
        lookAndFeel_.applyTheme(synth::theme::builtInThemes().front());
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);
        mc_ = std::make_unique<MainComponent>(themeManager_, lookAndFeel_, std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        mc_->simulateAddAudioTrackClick();
        mc_->getTimelineDoc().addTrack(synth::TrackKind::Midi, "Track 1");
        rebuildMixer();
        mc_->resized();
    }
    void TearDown() override {
        mc_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(previousDefault_);
        KeyboardReachFixture::TearDown();
    }

    void applyTheme(const synth::theme::Theme& theme, int width, int height) {
        lookAndFeel_.applyTheme(theme);
        mc_->setSize(width, height);
        mc_->sendLookAndFeelChange();
        mc_->resized();
    }

    void checkRegions(Findings& findings, const std::string& suffix, int token) {
        const auto& registry = mc_->getFocusRegionsForTest();
        for (const auto& region : openRegions()) {
            const auto id = region.id;
            checkSurface(
                findings, (region.id + suffix).toStdString(), *region.root,
                [&registry, id](juce::Component& c) {
                    const auto* owner = registry.regionContaining(&c);
                    return owner != nullptr && owner->id == id;
                },
                token);
        }
    }

    // Each main view in turn, then the timeline with a piano roll open on a clip, then with a drum machine.
    void checkScenes(Findings& findings, const std::string& suffix, int token) {
        const auto firstTrack = mc_->getTimelineDoc().getTracks().front().id;
        mc_->getTimelinePanel().selectTracks({firstTrack}, firstTrack);
        mc_->getTimelinePanel().toggleSidePane(true);
        for (const Tab view : {Tab::Canvas, Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
            mc_->setMainViewForTest(view);
            checkRegions(findings, suffix, token);
        }
        auto& panel = mc_->getTimelinePanel();
        mc_->setMainViewForTest(Tab::Timeline);
        const auto clip = mc_->getTimelineDoc().addClip(firstTrack, 0.0, 8.0, "Clip");
        panel.openPianoRoll(clip);
        panel.getPianoRoll().toggleScalePanel();
        mc_->resized();
        checkRegions(findings, " [piano roll open]" + suffix, token);
        panel.closePianoRoll();
        panel.openDrumMachine(clip);
        mc_->resized();
        checkRegions(findings, " [drum machine open]" + suffix, token);
        panel.closeDrumMachine();
        mc_->resized();
    }

    synth::theme::ThemeManager themeManager_;
    synth::theme::AppLookAndFeel lookAndFeel_;
    juce::LookAndFeel* previousDefault_ = nullptr;
};

} // namespace

TEST_F(LayoutGapShell, NoRegionHasTouchingPartsWrongCaptionGapsCutOffTextOrOffCentreButtons) {
#if !JUCE_MAC
    GTEST_SKIP() << "the known-gap list is measured on macOS; fonts, audio devices and menus differ on other platforms";
#endif
    Findings findings;
    const auto themes = synth::theme::builtInThemes();
    for (const auto& theme : themes) {
        applyTheme(theme, 1400, 900);
        checkScenes(findings, "", theme.metrics.spacingUnit);
    }
    applyTheme(themes.front(), kMinWindowWidth, kMinWindowHeight);
    checkScenes(findings, " [min]", themes.front().metrics.spacingUnit);
    EXPECT_GT(findings.measuredButtons(), 0u)
        << "no button text was measured: the centring check is not seeing buttons";
    findings.expectOnlyKnownGaps();
}

class LayoutGapDialogs : public KeyboardReachDialogFixture {};

TEST_F(LayoutGapDialogs, NoDialogHasTouchingPartsWrongCaptionGapsCutOffTextOrOffCentreButtons) {
#if !JUCE_MAC
    GTEST_SKIP() << "the known-gap list is measured on macOS; fonts, audio devices and menus differ on other platforms";
#endif
    Findings findings;
    const auto themes = synth::theme::builtInThemes();
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        for (const auto& theme : themes) {
            for (const bool smallest : {false, true}) {
                if (smallest && &theme != &themes.front())
                    continue;
                SweepEnvironment env(theme);
                auto* window = surface.open(env);
                ASSERT_NE(window, nullptr) << surface.name;
                if (smallest) {
                    auto* constrainer = window->getConstrainer();
                    if (constrainer == nullptr || constrainer->getMinimumWidth() >= window->getWidth() ||
                        constrainer->getMinimumHeight() >= window->getHeight())
                        continue; // nothing smaller to try
                    window->setSize(constrainer->getMinimumWidth(), constrainer->getMinimumHeight());
                }
                synth::test::flushMessageQueue();
                checkSurface(
                    findings, surface.name + (smallest ? " [min]" : ""), *window, [](juce::Component&) { return true; },
                    theme.metrics.spacingUnit);
            }
        }
    }
    findings.expectOnlyKnownGaps();
}
