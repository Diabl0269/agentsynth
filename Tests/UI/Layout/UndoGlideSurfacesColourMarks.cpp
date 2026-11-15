// UndoGlideSurfacesColourMarks.cpp -- the registry fixtures for the colours of marks on the timeline and in the shape
// window, each changed through its real commit path (one undo step) and undone: a section's block colour on the
// Sections strip, a marker's flag on the ruler, the By hand key strip's shape dots, and the routing pane's track
// stripe. drawn() is the red channel (0..255) of the colour as drawn; the two colours differ in red by construction
// (docs/layout/undo-glide.md).
#include "../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "../Timeline/TimelineSectionStrip/TimelineSectionStripFixture.h"
#include "AppUndoManager.h"
#include "Transport/TransportService.h"
#include "UI/Layout/KeyGlide/RestoredColourGlide.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/PerNoteBar.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Timeline/TimelineRoutingPane/TimelineRoutingPane.h"
#include "UI/Timeline/TimelineRulerComponent.h"
#include "UI/Timeline/TrackColour.h"
#include "UI/Timeline/TrackColourPicker.h"
#include "UndoGlideSurfaces.h"

namespace undo_glide_test {

namespace {

using synth::ui::RestoredColourGlide;

/** A colour whose red is far from `was`'s. */
juce::Colour farFrom(juce::Colour was) { return juce::Colour::fromRGB(was.getRed() < 128 ? 255 : 0, 90, 200); }

/** Every fixture lets glides run as if their owners were on screen for as long as it lives. */
struct OffScreenGlides {
    OffScreenGlides() { RestoredColourGlide::setAnimateOffScreenForTest(true); }
    ~OffScreenGlides() { RestoredColourGlide::setAnimateOffScreenForTest(false); }
};

/** A section's colour from the strip's colour picker, as the Sections strip is refreshed on every doc notification. */
class SectionColourSurface
    : public Surface
    , private synth::TimelineDoc::Listener {
public:
    SectionColourSurface() {
        f_.doc.addClip(f_.track, 0.0, 4.0, "Intro clip");
        f_.doc.makeSection(0.0, 8.0, "Intro", 0xff56D4C0, 0.5);
        f_.strip.refresh();
        intro_ = f_.doc.getPlacements().front().id;
        section_ = f_.doc.getPlacements().front().section;
        f_.doc.addListener(this);
    }
    ~SectionColourSurface() override { f_.doc.removeListener(this); }

    void move() override {
        auto picker = f_.strip.createColourPickerForTest(intro_);
        picker->setCurrentColourForTest(farFrom(juce::Colour(f_.doc.getSection(section_)->colourArgb)));
        picker->commitForTest();
        f_.strip.colourGlide().stepForTest(1000.0);
    }
    bool undo() override { return f_.undo.undo(); }
    bool redo() override { return f_.undo.redo(); }
    double drawn() const override { return f_.strip.getDrawnSectionColour(section_).getRed(); }
    bool gliding() const override { return f_.strip.colourGlide().isGliding(); }
    void stepTo(double elapsedMs) override { f_.strip.colourGlide().stepForTest(elapsedMs); }
    juce::Component* paintedComponent() override { return &f_.strip; }

private:
    void timelineChanged(const synth::TimelineDoc&) override { f_.strip.refresh(); }

    OffScreenGlides offScreen_;
    mutable sectionstrip_test::StripFixture f_;
    synth::PlacementId intro_;
    synth::SectionId section_;
};

/** A marker's colour from the ruler's picker; the panel syncs the ruler's colours on every doc notification. */
class MarkerColourSurface
    : public Surface
    , private synth::TimelineDoc::Listener {
public:
    MarkerColourSurface() {
        state_.pixelsPerBeat = 40.0;
        state_.firstVisibleBeat = 0.0;
        transport_.prepare(48000.0, 512);
        ruler_.setTransport(&transport_);
        ruler_.setTimelineDoc(&doc_);
        ruler_.setUndoManager(&undo_);
        ruler_.setSize(800, 48);
        marker_ = doc_.addMarker(2.0, "Intro", 0xff2288AA);
        ruler_.syncMarkerColours();
        doc_.addListener(this);
    }
    ~MarkerColourSurface() override { doc_.removeListener(this); }

    void move() override {
        auto picker = ruler_.createMarkerColourPickerForTest(marker_);
        picker->setCurrentColourForTest(farFrom(juce::Colour(doc_.getMarker(marker_)->colourArgb)));
        picker->commitForTest();
        ruler_.colourGlide().stepForTest(1000.0);
    }
    bool undo() override { return undo_.undo(); }
    bool redo() override { return undo_.redo(); }
    double drawn() const override {
        return ruler_.getDrawnMarkerColour(marker_, juce::Colour(doc_.getMarker(marker_)->colourArgb)).getRed();
    }
    bool gliding() const override { return ruler_.colourGlide().isGliding(); }
    void stepTo(double elapsedMs) override { ruler_.colourGlide().stepForTest(elapsedMs); }
    juce::Component* paintedComponent() override { return &ruler_; }

private:
    void timelineChanged(const synth::TimelineDoc&) override {
        ruler_.syncMarkerColours();
        ruler_.repaint();
    }

    OffScreenGlides offScreen_;
    synth::ui::TimelineViewState state_;
    synth::TransportService transport_;
    synth::TimelineDoc doc_;
    AppUndoManager undo_;
    mutable synth::ui::TimelineRulerComponent ruler_{state_};
    synth::MarkerId marker_;
};

/** Shape 0's colour (picked on the shape window's map) on the By hand key strip's dots. */
class KeyStripColourSurface : public Surface {
public:
    KeyStripColourSurface() {
        editor_ = std::make_unique<synth::ui::ShapeListEditor>(rig_.adapter, 0);
        strip().setSize(400, 60);
        strip().cycle(strip().getBaseNote()); // a key on shape 0, so its dot is painted
        editor_->refresh();
    }

    void move() override {
        const auto was = rest();
        const auto to = farFrom(was);
        editor_->getMap().onCommitDotColour(0, std::nullopt, std::optional<juce::uint32>(to.getARGB()));
        editor_->refresh();
        strip().colourGlide().stepForTest(1000.0);
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override { return strip().getDrawnShapeColour(0).getRed(); }
    bool gliding() const override { return strip().colourGlide().isGliding(); }
    void stepTo(double elapsedMs) override { strip().colourGlide().stepForTest(elapsedMs); }
    juce::Component* paintedComponent() override { return &strip(); }

private:
    synth::ui::NoteKeyStrip& strip() const { return editor_->getPerNoteBar()->getKeyStrip(); }
    juce::Colour rest() const { return strip().getDrawnShapeColour(0); }
    bool restore(bool forward) {
        const bool did = forward ? rig_.undo.redo() : rig_.undo.undo();
        editor_->refresh(); // what the shape window's poll runs after a restore
        return did;
    }

    OffScreenGlides offScreen_;
    synth::ui::test::ShapeRig rig_;
    std::unique_ptr<synth::ui::ShapeListEditor> editor_;
};

/** The routing pane's header stripe, for the selected track's colour picked in the colour picker. */
class RoutingPaneColourSurface
    : public Surface
    , private synth::TimelineDoc::Listener {
public:
    RoutingPaneColourSurface() {
        pane_.setDoc(&doc_);
        pane_.setUndoManager(&undo_);
        pane_.setSize(260, 400);
        track_ = doc_.addTrack(synth::TrackKind::Midi, "Lead");
        pane_.setTrack(track_);
        doc_.addListener(this);
    }
    ~RoutingPaneColourSurface() override { doc_.removeListener(this); }

    void move() override {
        synth::ui::TrackColourPickerContext context;
        context.doc = &doc_;
        context.targets = {track_};
        context.performEdit = [this](const std::function<void()>& edit) { undo_.recordTimelineChange(doc_, edit); };
        auto picker = synth::ui::buildTrackColourPicker(context, track_);
        picker->setCurrentColourForTest(farFrom(pane_.getDrawnColour()));
        picker->commitForTest();
        pane_.colourGlide().stepForTest(1000.0);
    }
    bool undo() override { return undo_.undo(); }
    bool redo() override { return undo_.redo(); }
    double drawn() const override { return pane_.getDrawnColour().getRed(); }
    bool gliding() const override { return pane_.colourGlide().isGliding(); }
    void stepTo(double elapsedMs) override { pane_.colourGlide().stepForTest(elapsedMs); }
    juce::Component* paintedComponent() override { return &pane_; }

private:
    void timelineChanged(const synth::TimelineDoc&) override { pane_.refresh(); }

    OffScreenGlides offScreen_;
    synth::TimelineDoc doc_;
    AppUndoManager undo_;
    mutable synth::ui::TimelineRoutingPane pane_;
    synth::TrackId track_;
};

} // namespace

void appendMarksColourEntries(std::vector<Entry>& list) {
    list.push_back({"Section colour (strip)", false, [] { return std::make_unique<SectionColourSurface>(); }});
    list.push_back({"Ruler marker colour", false, [] { return std::make_unique<MarkerColourSurface>(); }});
    list.push_back({"Per-note key strip colour", false, [] { return std::make_unique<KeyStripColourSurface>(); }});
    list.push_back({"Track colour (routing pane)", false, [] { return std::make_unique<RoutingPaneColourSurface>(); }});
}

} // namespace undo_glide_test
