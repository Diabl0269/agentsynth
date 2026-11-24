// CaptionHeightFitTests.cpp
//
// The text-fits-its-box guard for height: the caption above every knob, fader, switch and combo on every card
// (each type the library offers, and each type with a code default) is at least as tall as the font it is drawn in
// (ascent plus descent), so a descender (the g in "Ceiling") is never cut by the label's bottom edge. A caption's box
// comes from one measure shared by every layout run, so a short box on one knob is a short box on all of them.
// Regression test for FRO2324: the Limiter's Ceiling title lost the tail of its g.

#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include <set>

using namespace cardbody_test;

TEST(CaptionHeightFit, EveryCaptionOnEveryCardIsTallEnoughForItsFont) {
    std::set<juce::String> types;
    for (const auto& type : libraryTypes())
        types.insert(type);
    for (const auto& type : synth::DefaultCardLayouts::builtIn().types())
        types.insert(type);
    ASSERT_GT(types.size(), 20u);

    int captions = 0;
    for (const auto& type : types) {
        for (const bool moreOpen : {false, true}) {
            SCOPED_TRACE(type + (moreOpen ? " more=open" : " more=folded"));
            CardCanvas canvas;
            auto processor = synth::AIStateMapper::createModule(type);
            if (processor == nullptr)
                continue;
            const auto id = canvas.add(std::move(processor), 0, 0);
            canvas.editor.updateComponents();
            auto* card = canvas.card(id);
            ASSERT_NE(card, nullptr);
            auto* body = card->getCardBody();
            if (body == nullptr)
                continue;
            if (moreOpen)
                body->setMoreUnfolded(true);
            for (const auto& item : body->getPlan().items) {
                auto* label = dynamic_cast<juce::Label*>(item.label);
                if (label == nullptr || !label->isVisible() || label->getBounds().isEmpty() ||
                    label->getText().isEmpty())
                    continue;
                const auto font = label->getFont();
                const int textHeight = (int)std::ceil(font.getAscent() + font.getDescent());
                EXPECT_GE(label->getHeight(), textHeight)
                    << "caption \"" << label->getText().toStdString() << "\" is " << label->getHeight()
                    << " px tall for a " << textHeight << " px font";
                ++captions;
            }
        }
    }
    EXPECT_GT(captions, 100);
}
