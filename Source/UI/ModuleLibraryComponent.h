#pragma once

#include "Theme/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ModuleLibraryComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::SettableTooltipClient {
public:
    struct Entry {
        juce::String text;
        bool isHeader;
    };

    ModuleLibraryComponent() {
        entries = {
            {"Sources", true},
            {"Oscillator", false},
            {"Noise", false},
            {"LFO", false},
            {"Sequencing", true},
            {"Sequencer", false},
            {"Poly Sequencer", false},
            {"MidiKeyboard", false},
            {"Poly MIDI", false},
            {"External MIDI", false},
            {"Envelopes & Control", true},
            {"ADSR", false},
            {"VCA", false},
            {"Filters", true},
            {"Filter", false},
            {"Modulation FX", true},
            {"Chorus", false},
            {"Phaser", false},
            {"Flanger", false},
            {"Distortion", false},
            {"Time FX", true},
            {"Delay", false},
            {"Reverb", false},
            {"Dynamics", true},
            {"Compressor", false},
            {"Limiter", false},
            {"Utility", true},
            {"Voice Mixer", false},
            {"I/O", true},
            {"Audio Input", false},
            {"Audio Output", false},
        };
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    /** Optional predicate deciding whether a module can currently be added. Used for the singleton
     *  I/O modules: once a patch has an Audio Output, its row greys out and stops being draggable,
     *  rather than accepting a drag that would silently do nothing. Unset means everything is
     *  available, which keeps headless tests and every non-singleton module unaffected. */
    std::function<bool(const juce::String&)> isModuleAvailable;

    /** True when the row at `index` is a draggable, currently-addable module. */
    bool isEntryEnabled(int index) const {
        if (index < 0 || index >= (int)entries.size() || entries[index].isHeader)
            return false;
        return !isModuleAvailable || isModuleAvailable(entries[index].text);
    }

    // -------------------------------------------------------------------------
    // Pure static helpers — callable headlessly (no GUI / MessageManager needed)
    // -------------------------------------------------------------------------

    /** Returns a one-line description for a known module name, or a generic
     *  fallback string for unknown names. */
    static juce::String descriptionFor(const juce::String& moduleName) {
        if (moduleName.equalsIgnoreCase("Oscillator"))
            return "Generates audio waveforms (sine, saw, square, triangle). Switch Poly on to run "
                   "8 voices driven by a Poly MIDI pitch fan.";
        if (moduleName.equalsIgnoreCase("Noise"))
            return "Generates noise (white, pink, brown).";
        if (moduleName.equalsIgnoreCase("LFO"))
            return "Low-frequency oscillator for slow cyclic modulation.";
        if (moduleName.equalsIgnoreCase("Sequencer"))
            return "Step sequencer that outputs pitch and gate CV signals.";
        if (moduleName.equalsIgnoreCase("Poly Sequencer"))
            return "Polyphonic step sequencer for multi-voice melodies.";
        if (moduleName.equalsIgnoreCase("MidiKeyboard"))
            return "On-screen MIDI keyboard for note input.";
        if (moduleName.equalsIgnoreCase("Poly MIDI"))
            return "Converts MIDI into 8 voices of pitch and gate CV. Patch Poly Out to an "
                   "Oscillator's Pitch and an ADSR's Gate, and switch Poly on for every module in "
                   "the chain (Oscillator, ADSR, Filter, VCA) — with Poly off, only one voice sounds.";
        if (moduleName.equalsIgnoreCase("External MIDI"))
            return "Routes external MIDI device input into the patch graph.";
        if (moduleName.equalsIgnoreCase("ADSR"))
            return "Attack-Decay-Sustain-Release envelope generator. With Poly on it takes one gate "
                   "per voice; with Poly off it is driven by MIDI rather than its Gate jack.";
        if (moduleName.equalsIgnoreCase("VCA"))
            return "Voltage-controlled amplifier — controls signal amplitude via CV. Switch Poly on "
                   "to gain-control 8 voices and sum them to stereo.";
        if (moduleName.equalsIgnoreCase("Filter"))
            return "Multi-mode resonant filter (low-pass, high-pass, band-pass). Switch Poly on to "
                   "filter 8 voices; cutoff and resonance CV stay shared across them.";
        if (moduleName.equalsIgnoreCase("Chorus"))
            return "Adds lush width by layering slightly detuned copies of the signal.";
        if (moduleName.equalsIgnoreCase("Phaser"))
            return "Sweeping all-pass phase modulation effect.";
        if (moduleName.equalsIgnoreCase("Flanger"))
            return "Short delay feedback comb-filter with a sweeping metallic sound.";
        if (moduleName.equalsIgnoreCase("Distortion"))
            return "Waveshaping distortion from soft saturation to hard clipping.";
        if (moduleName.equalsIgnoreCase("Delay"))
            return "Tempo-syncable stereo echo / delay line.";
        if (moduleName.equalsIgnoreCase("Reverb"))
            return "Algorithmic reverb for adding space and depth.";
        if (moduleName.equalsIgnoreCase("Compressor"))
            return "Dynamic range compressor with threshold, ratio, attack and release.";
        if (moduleName.equalsIgnoreCase("Limiter"))
            return "Brickwall limiter that prevents the signal from exceeding 0 dBFS.";
        if (moduleName.equalsIgnoreCase("Voice Mixer"))
            return "Sums multiple polyphonic voices down to a stereo mix.";
        if (moduleName.equalsIgnoreCase("Audio Input"))
            return "Audio from the input device. Only one per patch.";
        if (moduleName.equalsIgnoreCase("Audio Output"))
            return "Sends the patch to the output device. Only one per patch.";
        // Generic fallback for any unrecognised module name.
        return "Audio processing module.";
    }

    // -------------------------------------------------------------------------
    // Paint
    // -------------------------------------------------------------------------

    void paint(juce::Graphics& g) override {
        // Resolve theme tokens from the active LnF; fall back to plain colors when our LnF
        // isn't installed (e.g. headless tests).
        juce::Colour bgColour = juce::Colours::darkgrey.darker();
        juce::Colour headerColour = juce::Colours::grey;
        juce::Colour itemColour = juce::Colours::white;
        juce::Colour accentColour = juce::Colours::lightblue;

        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        if (lf != nullptr) {
            const auto& c = lf->getTheme().colors;
            bgColour = c.bg0;
            headerColour = c.textMuted;
            itemColour = c.textPrimary;
            accentColour = c.accent;
        }

        g.fillAll(bgColour);

        int y = 10;
        for (int i = 0; i < (int)entries.size(); ++i) {
            const auto& entry = entries[i];
            if (entry.isHeader) {
                if (y > 10)
                    y += 5; // extra spacing before headers (except first)
                g.setColour(headerColour);
                g.setFont(juce::Font(juce::FontOptions(12.0f)));

                // Draw a 16x16 category icon at x=10 (null-guarded — no-op when LnF absent).
                synth::theme::Icon catIcon = categoryIconForHeader(entry.text);
                const juce::Drawable* icon = (lf != nullptr) ? lf->peekIcon(catIcon) : nullptr;

                if (icon != nullptr) {
                    icon->drawWithin(g, juce::Rectangle<float>(10.0f, (float)y + 2.0f, 16.0f, 16.0f),
                                     juce::RectanglePlacement::centred, 1.0f);
                    // Icon present: shift header text right to x=30.
                    g.drawText(entry.text.toUpperCase(), 30, y, getWidth() - 40, 20, juce::Justification::centredLeft);
                } else {
                    // No icon (headless or assets absent): text at original x=10.
                    g.drawText(entry.text.toUpperCase(), 10, y, getWidth() - 20, 20, juce::Justification::centredLeft);
                }

                y += 25;
            } else {
                const bool enabled = isEntryEnabled(i);

                // Draw hover highlight behind the text (non-header, enabled rows only).
                if (i == hoveredIndex && enabled) {
                    g.setColour(accentColour.withAlpha(0.12f));
                    g.fillRect(0, y, getWidth(), 32);
                }

                // Greyed out = already in the patch and not addable again.
                g.setColour(enabled ? itemColour : headerColour.withAlpha(0.5f));
                g.setFont(juce::Font(juce::FontOptions(16.0f)));
                g.drawText(entry.text, 20, y, getWidth() - 40, 28, juce::Justification::centredLeft);
                y += 32;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Mouse events
    // -------------------------------------------------------------------------

    void mouseMove(const juce::MouseEvent& e) override {
        int newIndex = getEntryIndexAt(e.y);
        // Only non-header entries can be hovered; clamp headers to -1.
        if (newIndex >= 0 && (int)entries.size() > newIndex && entries[newIndex].isHeader)
            newIndex = -1;

        if (newIndex != hoveredIndex) {
            hoveredIndex = newIndex;

            // Update tooltip: the shared TooltipWindow (owned by MainComponent) reads
            // this component's tooltip string on each hover. Setting it here on hover
            // change means each draggable row surfaces its per-module description.
            if (hoveredIndex >= 0 && hoveredIndex < (int)entries.size()) {
                juce::String tip = descriptionFor(entries[hoveredIndex].text);
                if (!isEntryEnabled(hoveredIndex))
                    tip += " (already in this patch)";
                setTooltip(tip);
            } else {
                setTooltip({});
            }

            repaint();
        }

        // Update cursor: grab hand for draggable items, normal otherwise.
        if (isEntryEnabled(hoveredIndex))
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoveredIndex != -1) {
            hoveredIndex = -1;
            setTooltip({});
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int index = getEntryIndexAt(e.y);
        // Disabled rows must not start a drag at all — accepting one and dropping it on the floor
        // reads as the canvas being broken.
        if (isEntryEnabled(index)) {
            juce::Image dragImage(juce::Image::ARGB, 150, 30, true);
            juce::Graphics dg(dragImage);
            dg.setColour(juce::Colours::white);
            dg.setFont(juce::Font(juce::FontOptions(16.0f)));
            dg.drawText(entries[index].text, dragImage.getBounds(), juce::Justification::centred, false);

            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
                container->startDragging(entries[index].text, this, dragImage);
        }
    }

    // -------------------------------------------------------------------------
    // Test / inspection helpers
    // -------------------------------------------------------------------------

    /** Returns the currently hovered entry index, or -1 when nothing is hovered. */
    int getHoveredIndex() const noexcept { return hoveredIndex; }

    /** Total number of entries (headers + items). */
    int getEntryCount() const noexcept { return (int)entries.size(); }

    /** Display text of the entry at `index`, or an empty string when out of range. */
    juce::String getEntryText(int index) const {
        return (index >= 0 && index < (int)entries.size()) ? entries[index].text : juce::String();
    }

private:
    // Map a category header string to its Icon enum value.
    static synth::theme::Icon categoryIconForHeader(const juce::String& header) {
        if (header.equalsIgnoreCase("Sources"))
            return synth::theme::Icon::CatSources;
        if (header.equalsIgnoreCase("Sequencing"))
            return synth::theme::Icon::CatSequencing;
        if (header.startsWithIgnoreCase("Envelopes"))
            return synth::theme::Icon::CatEnvelopes;
        if (header.equalsIgnoreCase("Filters"))
            return synth::theme::Icon::CatFilters;
        if (header.startsWithIgnoreCase("Modulation"))
            return synth::theme::Icon::CatModulationFX;
        if (header.equalsIgnoreCase("Time FX"))
            return synth::theme::Icon::CatTimeFX;
        if (header.equalsIgnoreCase("Dynamics"))
            return synth::theme::Icon::CatDynamics;
        // "Utility" and any unrecognised headers fall back to CatUtility.
        return synth::theme::Icon::CatUtility;
    }

    /** Returns the entry index whose row contains mouseY, or -1 if none. */
    int getEntryIndexAt(int mouseY) const {
        int y = 10;
        for (int i = 0; i < (int)entries.size(); ++i) {
            if (entries[i].isHeader) {
                if (y > 10)
                    y += 5;
                int h = 25;
                if (mouseY >= y && mouseY < y + h)
                    return i;
                y += h;
            } else {
                int h = 32;
                if (mouseY >= y && mouseY < y + h)
                    return i;
                y += h;
            }
        }
        return -1;
    }

    std::vector<Entry> entries;
    int hoveredIndex = -1; // -1 = no hover; updated on mouseMove/mouseExit only
};
