#include "TimelineTransportBar.h"
#include "../Transport/Metronome.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kButtonSize = 22;
constexpr int kGap = 4;
constexpr int kBpmLabelWidth = 52;
constexpr int kTimeSigLabelWidth = 40;
constexpr int kReadoutWidth = 92;
constexpr int kCountInComboWidth = 64;

// TL5-6 persistence keys — restored/persisted in setApplicationProperties(), the same idiom
// TimelinePanelComponent uses for its own "timelineSnap" key.
constexpr const char* kMetronomeEnabledKey = "timelineMetronomeEnabled";
constexpr const char* kCountInBarsKey = "timelineCountInBars";

// BPM drag tuning: this many pixels of vertical drag per "step" (1.0 BPM normally, 0.1 BPM fine).
constexpr float kBpmDragPixelsPerStep = 4.0f;

// A beat is always a quarter note regardless of the file's notated denominator — same formula
// TransportService::getPosition() and TimelineRulerComponent::beatsPerBarFrom use, kept in sync
// there rather than shared (message-thread-only UI vs. the audio-thread-facing service).
double beatsPerBarFrom(int numerator, int denominator) noexcept {
    const double v = (double)numerator * 4.0 / (double)std::max(1, denominator);
    return v > 0.0 ? v : 4.0;
}
} // namespace

//==============================================================================
void TimelineTransportBar::GlyphButton::paintButton(juce::Graphics& g, bool shouldDrawHighlighted, bool) {
    using namespace synth::theme;

    juce::Colour bg, border, accent, danger, textPrimary;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        bg = c.surface;
        border = c.border;
        accent = c.accent;
        danger = c.error;
        textPrimary = c.textPrimary;
    } else {
        bg = juce::Colours::darkgrey.darker(0.4f);
        border = juce::Colours::grey;
        accent = juce::Colours::cyan;
        danger = juce::Colours::red;
        textPrimary = juce::Colours::white;
    }

    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(shouldDrawHighlighted ? bg.brighter(0.15f) : bg);
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(border);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    const auto glyphArea = bounds.reduced(bounds.getWidth() * 0.28f);

    switch (glyph_) {
    case Glyph::PlayStop: {
        g.setColour(getToggleState() ? accent : textPrimary);
        if (getToggleState()) {
            g.fillRoundedRectangle(glyphArea, 1.0f); // stop = square
        } else {
            juce::Path triangle;
            triangle.addTriangle(glyphArea.getX(), glyphArea.getY(), glyphArea.getX(), glyphArea.getBottom(),
                                 glyphArea.getRight(), glyphArea.getCentreY());
            g.fillPath(triangle);
        }
        break;
    }
    case Glyph::Record: {
        g.setColour(getToggleState() ? danger : textPrimary);
        if (getToggleState())
            g.fillEllipse(glyphArea);
        else
            g.drawEllipse(glyphArea, 1.5f);
        break;
    }
    case Glyph::Loop: {
        g.setColour(getToggleState() ? accent : textPrimary.withAlpha(0.7f));
        const float radius = juce::jmin(glyphArea.getWidth(), glyphArea.getHeight()) * 0.5f;
        const auto centre = glyphArea.getCentre();
        constexpr float kGapStartRadians = juce::MathConstants<float>::pi * 0.15f;
        constexpr float kGapEndRadians = juce::MathConstants<float>::pi * 1.85f;

        juce::Path loopPath;
        loopPath.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, kGapStartRadians, kGapEndRadians, true);
        g.strokePath(loopPath, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // A small arrowhead at the arc's start so the bracket reads as a loop, not a plain "C".
        const auto tip = centre.translated(radius * std::sin(kGapStartRadians), -radius * std::cos(kGapStartRadians));
        juce::Path arrow;
        arrow.addTriangle(tip.x - 3.0f, tip.y - 2.0f, tip.x + 3.0f, tip.y, tip.x - 1.0f, tip.y + 3.5f);
        g.fillPath(arrow);
        break;
    }
    case Glyph::Metronome: {
        // A plain "quarter note" glyph (notehead + stem) — asset-free and distinct at a glance from
        // Record's plain circle. Lit accent when the metronome is on, dim otherwise.
        g.setColour(getToggleState() ? accent : textPrimary.withAlpha(0.7f));
        const float headWidth = glyphArea.getWidth() * 0.62f;
        const float headHeight = glyphArea.getHeight() * 0.42f;
        const juce::Rectangle<float> head(glyphArea.getX(), glyphArea.getBottom() - headHeight, headWidth, headHeight);
        g.fillEllipse(head);
        const float stemWidth = headWidth * 0.16f;
        const float stemX = head.getRight() - stemWidth;
        g.fillRect(stemX, glyphArea.getY(), stemWidth, glyphArea.getHeight() - headHeight * 0.5f);
        break;
    }
    }
}

//==============================================================================
void TimelineTransportBar::BpmDragLabel::mouseDown(const juce::MouseEvent& e) {
    juce::Label::mouseDown(e);
    dragAnchorY_ = e.position.y;
    dragAnchorBpm_ = owner_.currentBpmForDrag();
}

void TimelineTransportBar::BpmDragLabel::mouseDrag(const juce::MouseEvent& e) {
    if (isBeingEdited())
        return; // the text editor owns the mouse now — never fight it
    owner_.applyDraggedBpm(dragAnchorBpm_, dragAnchorY_ - e.position.y, e.mods.isCommandDown());
}

//==============================================================================
TimelineTransportBar::TimelineTransportBar() {
    addAndMakeVisible(playStopButton_);
    playStopButton_.setComponentID("timelineTransportPlayStop");
    playStopButton_.setClickingTogglesState(false); // the transport is the truth
    playStopButton_.setTooltip("Play / Stop");
    playStopButton_.onClick = [this] {
        if (transport_ == nullptr)
            return;
        if (transport_->getPositionSnapshot().playing)
            transport_->stop();
        else
            transport_->play();
    };

    addAndMakeVisible(recordButton_);
    recordButton_.setComponentID("timelineTransportRecord");
    recordButton_.setClickingTogglesState(false); // the owner is authoritative — see setRecordingState
    recordButton_.setTooltip("Record (arms the first armed track; implies Play)");
    recordButton_.onClick = [this] {
        if (onRecordToggled)
            onRecordToggled(!recordButton_.getToggleState());
    };

    addAndMakeVisible(loopButton_);
    loopButton_.setComponentID("timelineTransportLoop");
    loopButton_.setClickingTogglesState(false); // the transport is the truth
    loopButton_.setTooltip("Loop");
    loopButton_.onClick = [this] {
        if (transport_ == nullptr)
            return;
        const auto snap = transport_->getPositionSnapshot();
        transport_->setLoop(snap.loopStartPpq, snap.loopEndPpq, !snap.looping);
    };

    addAndMakeVisible(metronomeButton_);
    metronomeButton_.setComponentID("timelineTransportMetronome");
    metronomeButton_.setClickingTogglesState(false); // this bar owns the visual explicitly below
    metronomeButton_.setTooltip("Metronome click (summed after the graph — never recorded or bounced)");
    metronomeButton_.onClick = [this] {
        const bool newState = !metronomeButton_.getToggleState();
        metronomeButton_.setToggleState(newState, juce::dontSendNotification);
        pendingMetronomeEnabled_ = newState;
        if (metronome_ != nullptr)
            metronome_->setEnabled(newState);
        if (appProperties_ != nullptr && appProperties_->getUserSettings() != nullptr) {
            appProperties_->getUserSettings()->setValue(kMetronomeEnabledKey, newState);
            appProperties_->saveIfNeeded();
        }
    };

    addAndMakeVisible(countInCombo_);
    countInCombo_.setComponentID("timelineTransportCountIn");
    countInCombo_.setTooltip("Count-in before recording");
    countInCombo_.addItem("Off", 1);
    countInCombo_.addItem("1 bar", 2);
    countInCombo_.addItem("2 bars", 3);
    countInCombo_.setSelectedId(1, juce::dontSendNotification);
    countInCombo_.onChange = [this] {
        countInBars_ = countInCombo_.getSelectedId() - 1;
        if (appProperties_ != nullptr && appProperties_->getUserSettings() != nullptr) {
            appProperties_->getUserSettings()->setValue(kCountInBarsKey, countInBars_);
            appProperties_->saveIfNeeded();
        }
    };

    addAndMakeVisible(bpmLabel_);
    bpmLabel_.setComponentID("timelineTransportBpmLabel");
    bpmLabel_.setJustificationType(juce::Justification::centred);
    bpmLabel_.setTooltip("Tempo (double-click to type, drag to scrub — Cmd for fine)");
    bpmLabel_.setEditable(false, true, false); // double-click to edit, same idiom as the track-name label
    bpmLabel_.setText(formatBpm(120.0), juce::dontSendNotification);
    bpmLabel_.onTextChange = [this] {
        if (transport_ != nullptr)
            transport_->setBpm(bpmLabel_.getText().getDoubleValue());
    };

    addAndMakeVisible(timeSigLabel_);
    timeSigLabel_.setComponentID("timelineTransportTimeSigLabel");
    timeSigLabel_.setJustificationType(juce::Justification::centred);
    timeSigLabel_.setTooltip("Time signature (double-click to type as N/D)");
    timeSigLabel_.setEditable(false, true, false);
    timeSigLabel_.setText(formatTimeSig(4, 4), juce::dontSendNotification);
    timeSigLabel_.onTextChange = [this] {
        const juce::String text = timeSigLabel_.getText();
        const int slashPos = text.indexOfChar('/');
        bool applied = false;
        if (slashPos > 0 && transport_ != nullptr) {
            const int numerator = text.substring(0, slashPos).trim().getIntValue();
            const int denominator = text.substring(slashPos + 1).trim().getIntValue();
            applied = transport_->setTimeSignature(numerator, denominator);
        }
        if (!applied) {
            // Revert to whatever the transport is CURRENTLY reporting (not a remembered value —
            // a rejected edit never posted anything, so the transport's own snapshot is still the
            // last known-good time signature).
            int numerator = 4, denominator = 4;
            if (transport_ != nullptr) {
                const auto snap = transport_->getPositionSnapshot();
                numerator = snap.timeSigNumerator;
                denominator = snap.timeSigDenominator;
            }
            timeSigLabel_.setText(formatTimeSig(numerator, denominator), juce::dontSendNotification);
        }
    };
}

//==============================================================================
double TimelineTransportBar::currentBpmForDrag() const noexcept {
    return transport_ != nullptr ? transport_->getPositionSnapshot().bpm : bpmLabel_.getText().getDoubleValue();
}

void TimelineTransportBar::applyDraggedBpm(double anchorBpm, float deltaY, bool fine) {
    if (transport_ == nullptr)
        return;
    const double step = fine ? 0.1 : 1.0;
    const double newBpm = anchorBpm + (double)(deltaY / kBpmDragPixelsPerStep) * step;
    transport_->setBpm(newBpm);
    const double clamped = juce::jlimit(synth::TransportService::kMinBpm, synth::TransportService::kMaxBpm, newBpm);
    bpmLabel_.setText(formatBpm(clamped), juce::dontSendNotification);
}

//==============================================================================
void TimelineTransportBar::setRecordingState(bool recording) noexcept {
    recordButton_.setToggleState(recording, juce::dontSendNotification);
}

//==============================================================================
void TimelineTransportBar::setMetronome(synth::Metronome* metronome) noexcept {
    metronome_ = metronome;
    if (metronome_ != nullptr)
        metronome_->setEnabled(pendingMetronomeEnabled_);
}

void TimelineTransportBar::setApplicationProperties(juce::ApplicationProperties* props) {
    appProperties_ = props;
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;

    auto* settings = appProperties_->getUserSettings();
    pendingMetronomeEnabled_ = settings->getBoolValue(kMetronomeEnabledKey, false);
    countInBars_ = juce::jlimit(0, 2, settings->getIntValue(kCountInBarsKey, 0));

    metronomeButton_.setToggleState(pendingMetronomeEnabled_, juce::dontSendNotification);
    countInCombo_.setSelectedId(countInBars_ + 1, juce::dontSendNotification);

    if (metronome_ != nullptr)
        metronome_->setEnabled(pendingMetronomeEnabled_);
}

//==============================================================================
void TimelineTransportBar::updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot) {
    playStopButton_.setToggleState(snapshot.playing, juce::dontSendNotification);
    loopButton_.setToggleState(snapshot.looping, juce::dontSendNotification);

    // Never stomp on an in-progress edit: juce::Label::setText() unconditionally discards any open
    // editor's contents (see Label::hideEditor's call site), so a poll landing mid-keystroke would
    // otherwise fight the user's own typing.
    if (!bpmLabel_.isBeingEdited())
        bpmLabel_.setText(formatBpm(snapshot.bpm), juce::dontSendNotification);
    if (!timeSigLabel_.isBeingEdited())
        timeSigLabel_.setText(formatTimeSig(snapshot.timeSigNumerator, snapshot.timeSigDenominator),
                              juce::dontSendNotification);

    refreshReadout(snapshot);
}

void TimelineTransportBar::refreshReadout(const synth::TransportService::PositionSnapshot& snapshot) {
    const juce::String text = formatBarBeat(snapshot.ppq, snapshot.timeSigNumerator, snapshot.timeSigDenominator);
    if (text == lastReadoutText_)
        return;
    lastReadoutText_ = text;
    ++readoutRepaintCount_;
    repaint(readoutBounds_);
}

//==============================================================================
juce::String TimelineTransportBar::formatBpm(double bpm) { return juce::String(bpm, 1); }

juce::String TimelineTransportBar::formatTimeSig(int numerator, int denominator) {
    return juce::String(numerator) + "/" + juce::String(denominator);
}

juce::String TimelineTransportBar::formatBarBeat(double ppq, int tsNumerator, int tsDenominator) {
    const double beatsPerBar = beatsPerBarFrom(tsNumerator, tsDenominator);
    const double safePpq = std::isfinite(ppq) ? std::max(0.0, ppq) : 0.0;

    juce::int64 bar = (juce::int64)std::floor(safePpq / beatsPerBar);
    double beatInBar = safePpq - (double)bar * beatsPerBar;
    // Guard float error landing exactly on (or a hair past) the next bar boundary.
    if (beatInBar >= beatsPerBar) {
        beatInBar -= beatsPerBar;
        ++bar;
    } else if (beatInBar < 0.0) {
        beatInBar = 0.0;
    }

    int beatNumber = (int)std::floor(beatInBar) + 1;
    const double fractionalBeat = beatInBar - std::floor(beatInBar);
    int ticks = (int)std::llround(fractionalBeat * 960.0);
    if (ticks >= 960) {
        ticks = 0;
        ++beatNumber;
    }

    const int beatsPerBarRounded = std::max(1, (int)std::llround(beatsPerBar));
    if (beatNumber > beatsPerBarRounded) {
        beatNumber = 1;
        ++bar;
    }

    const juce::String barStr = juce::String(bar + 1).paddedLeft('0', 3);
    const juce::String ticksStr = juce::String(ticks).paddedLeft('0', 3);
    return barStr + "." + juce::String(beatNumber) + "." + ticksStr;
}

//==============================================================================
void TimelineTransportBar::resized() {
    auto bounds = getLocalBounds().reduced(4, 3);

    playStopButton_.setBounds(bounds.removeFromLeft(kButtonSize));
    bounds.removeFromLeft(kGap);
    recordButton_.setBounds(bounds.removeFromLeft(kButtonSize));
    bounds.removeFromLeft(kGap);
    loopButton_.setBounds(bounds.removeFromLeft(kButtonSize));
    bounds.removeFromLeft(kGap * 2);

    metronomeButton_.setBounds(bounds.removeFromLeft(kButtonSize));
    bounds.removeFromLeft(kGap);
    countInCombo_.setBounds(bounds.removeFromLeft(kCountInComboWidth));
    bounds.removeFromLeft(kGap * 2);

    bpmLabel_.setBounds(bounds.removeFromLeft(kBpmLabelWidth));
    bounds.removeFromLeft(kGap);
    timeSigLabel_.setBounds(bounds.removeFromLeft(kTimeSigLabelWidth));
    bounds.removeFromLeft(kGap * 2);

    readoutBounds_ = bounds.removeFromLeft(kReadoutWidth);
}

//==============================================================================
void TimelineTransportBar::paint(juce::Graphics& g) {
    using namespace synth::theme;

    juce::Colour textMuted;
    float monoSize = 11.0f;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        textMuted = lf->getTheme().colors.textMuted;
        monoSize = lf->getTheme().type.value + 1.0f;
    } else {
        textMuted = juce::Colours::lightgrey;
    }

    if (readoutBounds_.getWidth() > 0) {
        g.setColour(textMuted);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), monoSize, juce::Font::plain));
        g.drawText(lastReadoutText_, readoutBounds_, juce::Justification::centredLeft, false);
    }
}

} // namespace synth::ui
