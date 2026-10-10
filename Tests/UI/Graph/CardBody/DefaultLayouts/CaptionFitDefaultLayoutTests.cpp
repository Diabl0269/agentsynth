// CaptionFitDefaultLayoutTests.cpp
//
// No caption on a default card is ellipsised: for every type with a code default, each caption label's text
// (in the font the label draws with, inside the label's border) fits the label the way
// AppLookAndFeel::drawLabel -> Graphics::drawFittedText fits it: the text is squashed horizontally down to the
// minimum scale (0.7 unless the label sets its own) before it is ellipsised, so a caption truncates only when
// its width times that scale still exceeds the label. A long parameter name gets a short label override in the default;
// the full name stays in the tooltip.

#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"

using namespace cardbody_test;

TEST(CaptionFitDefaultLayout, NoCaptionOnADefaultCardTruncates) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const char* type : {"Filter",        "Compressor",     "Limiter",       "Gate",      "Delay",
                             "Reverb",        "Chorus",         "Flanger",       "Phaser",    "Distortion",
                             "Bitcrusher",    "Ring Modulator", "Pitch Shifter", "VCA",       "Envelope Follower",
                             "Sample & Hold", "Math",           "Voice Mixer",   "Poly MIDI", "Oscillator",
                             "Noise",         "Sampler",        "Drum Kit",      "LFO"}) {
        SCOPED_TRACE(type);
        ASSERT_NE(defaults.find(type), nullptr);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        auto* body = card->getCardBody();
        ASSERT_NE(body, nullptr);
        for (const auto& item : body->getPlan().items) {
            auto* label = dynamic_cast<juce::Label*>(item.label);
            if (label == nullptr || !label->isVisible())
                continue;
            const auto area = label->getBorderSize().subtractedFrom(label->getLocalBounds());
            const float textWidth = juce::GlyphArrangement::getStringWidth(label->getFont(), label->getText());
            const float scale = label->getMinimumHorizontalScale() > 0.0f
                                    ? label->getMinimumHorizontalScale()
                                    : juce::Font::getDefaultMinimumHorizontalScaleFactor();
            EXPECT_LT(textWidth * scale, (float)area.getWidth())
                << "caption \"" << label->getText().toStdString() << "\" truncates in a " << area.getWidth()
                << " px cell";
        }
    }
}
