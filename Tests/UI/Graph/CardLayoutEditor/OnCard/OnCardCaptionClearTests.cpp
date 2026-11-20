// OnCardCaptionClearTests.cpp -- the layout-overlap guard for the Edit Layout state of every card: each caption's
// words keep kCaptionClear of clear room inside their own outline on all four sides, measured on drawn pixels, and no
// two outlines touch.
// Regression test for FRO2144: the Filter Type outline touched its title after an earlier fix
// (the old guard demanded 0 px, measured a computed line box and skipped cells in abutting rows).

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutGroupHandle.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <functional>

using namespace oncard_test;
using synth::ui::CardLayoutGroupHandle;

namespace {

constexpr float kClear = (float)CardLayoutOutline::kCaptionClear;
constexpr juce::uint8 kStrokeAlpha = 64; ///< a stroke is at least this opaque; the hover wash (7%) never is

void setChoice(ModuleBase& module, const juce::String& paramId, const juce::String& choice) {
    for (auto* p : module.getParameters())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p); c != nullptr && c->paramID == paramId)
            *c = c->choices.indexOf(choice);
}

void setParam(ModuleBase& module, const juce::String& paramId, float real) {
    for (auto* p : module.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == paramId)
            r->setValueNotifyingHost(r->convertTo0to1(real));
}

/** One chrome or parameter state of a module type: how to prepare the module and the card before the editor opens. */
struct Variant {
    juce::String name;
    std::function<void(ModuleBase&)> module;
    std::function<void(ModuleComponent&)> card;
};

std::vector<Variant> variantsOf(const juce::String& type) {
    std::vector<Variant> variants;
    variants.push_back({"default", {}, {}});
    variants.push_back({"panels",
                        [](ModuleBase& m) {
                            synth::CardViewState view;
                            view.showScope = true;
                            view.showResponse = true;
                            view.showSpectrum = true;
                            view.showMorphMap = true;
                            m.setCardViewState(view);
                        },
                        [](ModuleComponent& card) {
                            for (auto* child : card.getChildren())
                                if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child);
                                    toggle != nullptr && toggle->getButtonText() == synth::cardbody::kShowEnvelopeText)
                                    toggle->setToggleState(true, juce::sendNotificationSync);
                        }});
    if (type == "Filter")
        variants.push_back({"custom", [](ModuleBase& m) { setChoice(m, "filterType", "Custom"); }, {}});
    if (type == "LFO") {
        variants.push_back({"custom", [](ModuleBase& m) { setChoice(m, "shape", "Custom"); }, {}});
        variants.push_back({"free", [](ModuleBase& m) { setParam(m, "mode", 0.0f); }, {}});
    }
    if (type == "Oscillator")
        for (const bool poly : {false, true})
            for (const int unison : {1, 7})
                for (const char* rate : {"Follow notes", "Fixed"})
                    variants.push_back({juce::String(poly ? "poly" : "mono") + " unison " + juce::String(unison) +
                                            " rate " + rate + " more open",
                                        [=](ModuleBase& m) {
                                            setParam(m, "poly", poly ? 1.0f : 0.0f);
                                            setParam(m, "unison", (float)unison);
                                            setChoice(m, "rateMode", rate);
                                        },
                                        [](ModuleComponent& card) { card.getCardBody()->setMoreUnfolded(true); }});
    return variants;
}

/** Where `label`'s ink lies (alpha of at least a fifth), in the label's own pixels at `scale`, drawn by the label
 * itself. */
juce::Rectangle<float> inkOf(juce::Label& label, float scale) {
    const int w = std::max(1, (int)std::ceil((float)label.getWidth() * scale));
    const int h = std::max(1, (int)std::ceil((float)label.getHeight() * scale));
    juce::Image image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    {
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        label.paintEntireComponent(g, true);
    }
    juce::Rectangle<int> ink;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (image.getPixelAt(x, y).getAlpha() >= 48)
                ink = ink.isEmpty() ? juce::Rectangle<int>(x, y, 1, 1) : ink.getUnion({x, y, 1, 1});
    return ink.toFloat() / scale;
}

/** Every pixel of `parts` drawn at `scale` into one transparent image the size of `bounds`. */
juce::Image drawParts(const std::vector<juce::Component*>& parts, juce::Rectangle<int> bounds, float scale) {
    juce::Image image(juce::Image::ARGB, (int)std::ceil((float)bounds.getWidth() * scale),
                      (int)std::ceil((float)bounds.getHeight() * scale), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(scale));
    for (auto* part : parts) {
        juce::Graphics::ScopedSaveState state(g);
        g.setOrigin(part->getPosition());
        part->paintEntireComponent(g, true);
    }
    return image;
}

/** The first stroke pixel (alpha of kStrokeAlpha or more) whose square meets `box`, or nothing, in the units of `box`.
 */
std::optional<juce::Point<int>> strokeInside(const juce::Image& image, float scale, juce::Rectangle<float> box) {
    const auto px = (box * scale).getSmallestIntegerContainer().getIntersection(image.getBounds());
    for (int y = px.getY(); y < px.getBottom(); ++y)
        for (int x = px.getX(); x < px.getRight(); ++x)
            if (image.getPixelAt(x, y).getAlpha() >= kStrokeAlpha)
                return juce::Point<int>(x, y);
    return std::nullopt;
}

/** A caption's ink in the editor's coordinates, from the label drawn at `scale`; empty when it draws nothing. */
juce::Rectangle<float> inkInEditor(juce::Label& label, juce::Point<int> cardInEditor, float scale) {
    const auto ink = inkOf(label, scale);
    return ink.isEmpty()
               ? ink
               : ink.translated((float)(label.getX() + cardInEditor.x), (float)(label.getY() + cardInEditor.y));
}

/** The outlines whose drawn rectangle has an edge within 1.5 px of pixel `at`, by name, for a failure message. */
juce::String ownersOf(const std::vector<std::pair<juce::String, juce::Rectangle<float>>>& areas, juce::Point<int> at,
                      float scale) {
    const auto p = juce::Point<float>(((float)at.x + 0.5f) / scale, ((float)at.y + 0.5f) / scale);
    juce::String names;
    for (const auto& [name, area] : areas)
        if (area.expanded(2.0f).contains(p) && !area.reduced(2.0f).contains(p))
            names << name << " " << area.toString() << "; ";
    return names;
}

struct Counts {
    int cards = 0;
    int captions = 0;
    int groups = 0;
    int outlines = 0;
    bool filterTypeChecked = false;
};

void checkEditor(const juce::String& context, ModuleComponent& card, CardLayoutOnCardEditor& editor, Counts& counts) {
    const auto cardInEditor = card.getPosition() - editor.getPosition();
    const auto& plan = card.getCardBody()->getPlan();
    ++counts.cards;

    std::vector<juce::Component*> parts;
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> areas;
    struct Held {
        juce::String what;
        juce::Component* owner;
        juce::Rectangle<float> area;
        juce::Rectangle<float> text;
        juce::Label* label;
    };
    std::vector<Held> held;
    const auto editorBounds = editor.getLocalBounds().toFloat();

    for (const auto& item : plan.items) {
        if (item.param == nullptr || !item.shown || item.widget == nullptr || !item.widget->isVisible())
            continue;
        auto* outline = editor.getOutlineForTest(item.param->paramID);
        if (outline == nullptr)
            continue;
        ++counts.outlines;
        EXPECT_TRUE(outline->getLocalBounds().toFloat().contains(outline->getOutlineArea()))
            << context << ": the outline of " << item.param->paramID << " is clipped by its own component";
        parts.push_back(outline);
        const auto area = outline->getOutlineArea().translated((float)outline->getX(), (float)outline->getY());
        if (!outline->isPassThrough()) // the ADSR's Sync outline lies over the switch it passes the mouse to
            areas.emplace_back(context + " " + item.param->paramID, area);
        EXPECT_TRUE(editorBounds.contains(area))
            << context << ": the outline of " << item.param->paramID << " " << area.toString()
            << " is cut by the overlay " << editorBounds.toString();
        auto* label = dynamic_cast<juce::Label*>(item.label);
        if (label == nullptr || !label->isVisible() || label->getText().isEmpty())
            continue;
        // The computed words (the font's line box), held on all four sides with no cell skipped.
        const auto text = synth::ui::captionTextRect(*label).translated((float)cardInEditor.x, (float)cardInEditor.y);
        EXPECT_TRUE(area.contains(text.expanded(CardLayoutOutline::kCaptionRoom - 0.01f)))
            << context << ": the outline of \"" << label->getText() << "\" " << area.toString() << " is closer than "
            << kClear << " px to its caption text " << text.toString();
        held.push_back({"\"" + label->getText() + "\"", outline, area, text, label});
        ++counts.captions;
        if (item.param->paramID == "filterType")
            counts.filterTypeChecked = true;
    }

    // No two outlines touch: a gap of at least 1 px between every pair, the Sequencer's step grid included.
    for (size_t i = 0; i < areas.size(); ++i)
        for (size_t j = i + 1; j < areas.size(); ++j)
            EXPECT_FALSE(areas[i].second.expanded(0.99f).intersects(areas[j].second))
                << areas[i].first << " " << areas[i].second.toString() << " and " << areas[j].first << " "
                << areas[j].second.toString() << " touch or overlap";

    // A group's title, kept clear by the group's own outline, drawn when the group is hovered.
    std::vector<juce::Component*> groupHandles;
    for (const auto& section : plan.sections) {
        auto* handle = section.id.isNotEmpty() ? editor.getGroupHandleForTest(section.id) : nullptr;
        auto* title = dynamic_cast<juce::Label*>(section.header);
        if (handle == nullptr || title == nullptr || !title->isVisible() || title->getText().isEmpty())
            continue;
        groupHandles.push_back(handle);
        ++counts.groups;
    }

    // The drawn pixels: at rest, hovered and with the focus ring, at the sizes a screen draws.
    for (const float scale : {1.0f, 2.0f})
        for (const int state : {0, 1, 2}) {
            synth::ui::setFocusRingsShownForTest(state == 2);
            for (auto* part : parts)
                if (state == 1)
                    part->mouseEnter(cardbody_test::mouseAt(*part, {5.0f, 5.0f}, juce::ModifierKeys()));
            const auto image = drawParts(parts, editor.getLocalBounds(), scale);
            for (const auto& h : held) {
                const auto ink = inkInEditor(*h.label, cardInEditor, scale);
                if (ink.isEmpty())
                    continue;
                const auto point = strokeInside(image, scale, ink.expanded(kClear));
                EXPECT_FALSE(point.has_value())
                    << context << " scale " << scale
                    << (state == 1   ? " hovered"
                        : state == 2 ? " focused"
                                     : " at rest")
                    << ": an outline pixel at " << (point ? point->toString() : juce::String()) << " lies within "
                    << kClear << " px of the caption " << h.what << " ink " << ink.toString() << " own outline "
                    << h.area.toString() << (point ? " drawn by " + ownersOf(areas, *point, scale) : juce::String());
            }
            for (auto* part : parts)
                if (state == 1)
                    part->mouseExit(cardbody_test::mouseAt(*part, {-5.0f, -5.0f}, juce::ModifierKeys()));
            synth::ui::setFocusRingsShownForTest(false);
        }
    for (auto* handle : groupHandles)
        handle->mouseEnter(cardbody_test::mouseAt(*handle, {5.0f, 5.0f}, juce::ModifierKeys()));
    if (!groupHandles.empty())
        for (const float scale : {1.0f, 2.0f}) {
            const auto image = drawParts(groupHandles, editor.getLocalBounds(), scale);
            for (const auto& section : plan.sections) {
                auto* title = dynamic_cast<juce::Label*>(section.header);
                if (section.id.isEmpty() || editor.getGroupHandleForTest(section.id) == nullptr || title == nullptr ||
                    title->getText().isEmpty())
                    continue;
                const auto ink = inkInEditor(*title, cardInEditor, scale);
                if (ink.isEmpty())
                    continue;
                const auto point = strokeInside(image, scale, ink.expanded(kClear));
                EXPECT_FALSE(point.has_value()) << context << " scale " << scale << ": the group outline pixel at "
                                                << (point ? point->toString() : juce::String()) << " lies within "
                                                << kClear << " px of the group title \"" << title->getText() << "\"";
            }
        }
    for (auto* handle : groupHandles)
        handle->mouseExit(cardbody_test::mouseAt(*handle, {-5.0f, -5.0f}, juce::ModifierKeys()));
}

/** The built-in theme `theme`, installed as the default look and feel for the cards built while it lives. */
struct ThemeScope {
    explicit ThemeScope(const synth::theme::Theme& theme) {
        lnf.applyTheme(theme);
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    }
    ~ThemeScope() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    synth::theme::AppLookAndFeel lnf;
};

} // namespace

// Every module type in every chrome and parameter state it has, in every built-in theme: the editor opens, each
// caption keeps kCaptionClear of room inside its outline on all four sides (computed, and on the drawn pixels at rest,
// hovered and focused, at 1x and 2x), the outline stays inside the overlay and no two outlines touch.
TEST(CardBodyOverlap, EveryCardsEditLayoutOutlinesClearTheirCaptionsAndEachOther) {
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const char* extra : {"Track In", "Rec Tap", "Track Audio", "Channel Strip", "Master", "Amp Env", "Filter Env"})
        types.addIfNotAlreadyThere(extra);

    Counts counts;
    for (const auto& theme : synth::theme::builtInThemes()) {
        ThemeScope scope(theme);
        for (const auto& type : types)
            for (const auto& variant : variantsOf(type)) {
                OnCardRig rig;
                auto processor = synth::AIStateMapper::createModule(type);
                auto* module = dynamic_cast<ModuleBase*>(processor.get());
                if (module == nullptr)
                    continue;
                if (variant.module)
                    variant.module(*module);
                const auto id = rig.add(std::move(processor), 200, 200);
                auto* card = rig.card(id);
                ASSERT_NE(card, nullptr) << type;
                if (variant.card)
                    variant.card(*card);
                auto* editor = rig.openOnCard(id);
                if (editor == nullptr)
                    continue; // a card with no Edit Layout item (a hosted plugin has its picker instead)
                editor->runQueuedSyncForTest();
                checkEditor(theme.name + " / " + type + " / " + variant.name, *card, *editor, counts);
            }
    }
    EXPECT_GE(counts.cards, 4 * 15) << "the sweep reaches the module types in every theme";
    EXPECT_GT(counts.captions, 400) << "the sweep checks captions";
    EXPECT_TRUE(counts.filterTypeChecked) << "the Filter Type title is held whole inside its outline";
}
