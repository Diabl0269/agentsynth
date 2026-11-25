// OnCardEditorTests.cpp -- opening the on-card layout editor from the real module menu: one accessible
// outline per control over the card, the card's positions surviving the move to free placement, the
// overlay following the card a write rebuilds, and leaving the canvas when it closes.

#include "../../../Accessibility/AccessibilityAudit.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutGroupHandle.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <algorithm>
#include <functional>
#include <iterator>

using namespace oncard_test;

namespace {

// What the Filter card draws as controls: every item with a visible widget, the footer's included.
juce::StringArray visibleControls(ModuleComponent& card) {
    juce::StringArray ids;
    const auto& plan = card.getCardBody()->getPlan();
    for (const auto& item : plan.items)
        if (item.param != nullptr && item.widget != nullptr && item.widget->isVisible() && item.section >= 0)
            ids.add(item.param->paramID);
    return ids;
}

} // namespace

TEST(OnCardEditor, EditLayoutFromTheModuleMenuOpensOverTheCardWithAnOutlinePerControl) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 200, 200);
    auto* card = rig.card(id);
    const auto expected = visibleControls(*card);
    ASSERT_GE(expected.size(), 5);

    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->getParentComponent(), card->getParentComponent()) << "a sibling of the card, not its child";
    EXPECT_EQ(editor->getBounds().withHeight(card->getHeight()), card->getBounds())
        << "over the card, and the strip with its Add control under it";
    EXPECT_EQ(editor->getOutlineCountForTest(), expected.size());
    for (const auto& paramId : expected) {
        SCOPED_TRACE(paramId.toStdString());
        auto* outline = editor->getOutlineForTest(paramId);
        ASSERT_NE(outline, nullptr);
        const bool panelOnly = outline->isPanelOnly();
        EXPECT_EQ(outline->getTitle(),
                  cardlayouteditor_test::captionOf(*card, paramId) +
                      (panelOnly ? ", layout: Return for options" : ", layout: drag to move, Return for options"));
        EXPECT_EQ(outline->getTooltip(),
                  panelOnly
                      ? "Backspace removes. Right-click for options"
                      : "Drag to move (arrow keys nudge, Shift for 8px, Backspace removes). Right-click for options");
        EXPECT_TRUE(outline->getWantsKeyboardFocus());
    }
    for (const auto* id2 : {"cutoff", "resonance", "drive", "outputLevel"})
        EXPECT_NE(editor->getOutlineForTest(id2), nullptr) << id2;
    ASSERT_NE(editor->getOutlineForTest("poly"), nullptr) << "the footer's controls get one too";
    EXPECT_TRUE(editor->getOutlineForTest("poly")->isPanelOnly());
    EXPECT_FALSE(editor->getOutlineForTest("cutoff")->isPanelOnly());
}

// Regression test for FRO704: the footer's outlines were widened to a body cell's box and drawn off the card.
TEST(OnCardEditor, EveryOutlineLiesInsideTheCard) {
    const auto check = [](std::unique_ptr<ModuleBase> module) {
        OnCardRig rig;
        const auto id = rig.add(std::move(module), 200, 200);
        auto* card = rig.card(id);
        auto* editor = rig.openOnCard(id);
        ASSERT_NE(editor, nullptr);
        for (const auto& cell : synth::ui::collectCells(*card)) {
            SCOPED_TRACE(cell.key.toStdString());
            auto* outline = editor->getOutlineForTest(cell.key);
            ASSERT_NE(outline, nullptr);
            EXPECT_TRUE(card->getLocalBounds().contains(outline->getBounds())) << outline->getBounds().toString();
        }
    };
    check(std::make_unique<FilterModule>());
    check(std::make_unique<OscillatorModule>());
}

TEST(OnCardEditor, EditLayoutFromAControlsMenuOpensTheSameEditor) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* card = rig.card(id);
    const auto menu = cardbody_test::rightClickOnCard(*card, *widgetOf(*card, "cutoff"));
    const auto* item = cardbody_test::menuItem(menu, "Edit Layout...");
    ASSERT_NE(item, nullptr);
    item->action();
    EXPECT_NE(dynamic_cast<CardLayoutOnCardEditor*>(rig.launched.get()), nullptr);
}

TEST(OnCardEditor, EveryOutlineIsAKeyboardReachableNamedControlWithATooltipAndTheBarIsNamed) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->getEditBarForTest().getCancelButton().getButtonText(), "Cancel");
    EXPECT_EQ(editor->getEditBarForTest().getDoneButton().getButtonText(), "Done");
    EXPECT_EQ(editor->getEditBarForTest().getDoneButton().getTooltip(), "Keep the layout");
    EXPECT_EQ(editor->getEditBarForTest().getCancelButton().getTooltip(), "Undo everything since Edit layout (Esc)");

    const auto gaps = synth::test::auditAccessibility(*editor);
    EXPECT_TRUE(gaps.empty()) << "gaps: " << gaps.size();
}

// The first write puts a position on every control of the section; each must land exactly where the
// flowing layout had it, or the whole card would jump on the first drag.
TEST(OnCardEditor, FreePlacementOfAFlowingSectionLeavesEveryControlWhereItWas) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* card = rig.card(id);
    auto before = synth::ui::collectCells(*card);
    std::erase_if(before, [](const synth::ui::OnCardCell& cell) { return cell.panelOnly; });
    ASSERT_GE(before.size(), 5u);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(card->getWidth());

    auto layout = card->getCardBody()->explicitLayout();
    const auto& plan = card->getCardBody()->getPlan();
    std::vector<int> relativeY;
    for (const auto& cell : before)
        relativeY.push_back(cell.rect.getY() - plan.sections[(size_t)cell.section].cellTop);
    for (int section = 0; section < (int)plan.sections.size(); ++section) {
        std::vector<synth::ui::OnCardCell> cells;
        for (const auto& cell : before)
            if (cell.section == section)
                cells.push_back(cell);
        layout = synth::ui::withCellPositions(layout, cells, {}, g.contentX, plan.sections[(size_t)section].cellTop);
    }
    synth::ui::BuiltInCardLayoutSource source(rig.canvas.editor, nullptr, id);
    source.apply(layout, false);

    auto* after = rig.card(id);
    ASSERT_NE(after, nullptr);
    auto placed = synth::ui::collectCells(*after);
    std::erase_if(placed, [](const synth::ui::OnCardCell& cell) { return cell.panelOnly; });
    ASSERT_EQ(placed.size(), before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        SCOPED_TRACE(before[i].key.toStdString());
        EXPECT_EQ(placed[i].key, before[i].key);
        // A positioned group is 6 px taller than a flowing one, so the groups under it sit a little lower.
        EXPECT_EQ(placed[i].rect.getX(), before[i].rect.getX());
        EXPECT_EQ(placed[i].rect.getWidth(), before[i].rect.getWidth());
        EXPECT_EQ(placed[i].rect.getHeight(), before[i].rect.getHeight());
        EXPECT_EQ(placed[i].widgetRel, before[i].widgetRel);
        EXPECT_EQ(placed[i].rect.getY() - after->getCardBody()->getPlan().sections[(size_t)placed[i].section].cellTop,
                  relativeY[i]);
    }
}

TEST(OnCardEditor, AfterAWriteTheOverlayFollowsTheRebuiltCardAndKeepsItsOutlines) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* cardBefore = rig.card(id);
    const int count = editor->getOutlineCountForTest();
    auto* outlineBefore = editor->getOutlineForTest("cutoff");

    Pointer pointer(*editor, "cutoff");
    pointer.moveBy({40, 90}, juce::ModifierKeys::commandModifier);
    pointer.release();

    auto* cardAfter = rig.card(id);
    ASSERT_NE(cardAfter, nullptr);
    EXPECT_NE(cardAfter, cardBefore) << "the write rebuilt the card";
    EXPECT_EQ(editor->getBounds().withHeight(cardAfter->getHeight()), cardAfter->getBounds());
    EXPECT_EQ(editor->getOutlineCountForTest(), count);
    EXPECT_EQ(editor->getOutlineForTest("cutoff"), outlineBefore) << "an outline survives a write";
    EXPECT_EQ(editor->getParentComponent(), cardAfter->getParentComponent());
    const auto cell = editor->getCellRectForTest("cutoff");
    for (const auto& onCard : synth::ui::collectCells(*cardAfter))
        if (onCard.key == "cutoff")
            EXPECT_EQ(onCard.rect, cell) << "the overlay's cell is where the rebuilt card draws the knob";
    EXPECT_EQ(outlineBefore->getBounds(), cell.expanded(CardLayoutOutline::kPad));
}

TEST(OnCardEditor, ClosingTakesTheOverlayOffTheCanvas) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* parent = editor->getParentComponent();
    ASSERT_NE(parent, nullptr);
    const int children = parent->getNumChildComponents();

    editor->done();
    EXPECT_TRUE(editor->isClosed());
    EXPECT_EQ(editor->getParentComponent(), nullptr);
    EXPECT_EQ(parent->getNumChildComponents(), children - 1);
}

TEST(OnCardEditor, OpeningAnotherEditorWhileOneRunsLeavesTheFirstSessionAsDone) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* first = rig.openOnCard(id);
    ASSERT_NE(first, nullptr);
    Pointer pointer(*first, "cutoff");
    pointer.moveBy({0, 90}, juce::ModifierKeys::commandModifier);
    pointer.release();
    ASSERT_TRUE(rig.storedLayout(id).has_value());

    EXPECT_NE(rig.openOnCard(id), nullptr) << "the seam replaces the launched editor, ending the first";
    EXPECT_TRUE(rig.storedLayout(id).has_value()) << "the first session's layout stays";
}

TEST(OnCardEditor, TheNodeGoingAwayClosesTheEditor) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    rig.canvas.engine.getGraph().removeNode(id);
    rig.canvas.editor.updateComponents();
    editor->runQueuedSyncForTest();
    EXPECT_TRUE(editor->isClosed());
}

// The caption-clearance sweep: the layout-overlap guard for the Edit Layout state of every card: each caption's
// words keep kCaptionClear of clear room inside their own outline on all four sides, measured on drawn pixels, and no
// two outlines touch.
// Regression test for FRO2144: the Filter Type outline touched its title after an earlier fix
// (the old guard demanded 0 px, measured a computed line box and skipped cells in abutting rows).

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
 * itself (any component: a widget's dial, ticks and value box too). */
juce::Rectangle<float> inkOf(juce::Component& label, float scale) {
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
juce::Rectangle<float> inkInEditor(juce::Component& label, juce::Point<int> cardInEditor, float scale) {
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
    int widgets = 0;
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
        juce::Component* label;
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
        // The ADSR's Sync outline lies over the switch it passes the mouse to; a lane's Sync button sits in the caption
        // of the control it syncs, inside that control's outline.
        if (!outline->isPassThrough() && !item.isLaneSync(plan))
            areas.emplace_back(context + " " + item.param->paramID, area);
        EXPECT_TRUE(editorBounds.contains(area))
            << context << ": the outline of " << item.param->paramID << " " << area.toString()
            << " is cut by the overlay " << editorBounds.toString();
        auto* label = dynamic_cast<juce::Label*>(item.label);
        const bool captioned = label != nullptr && label->isVisible() && label->getText().isNotEmpty();
        // The widget (dial and ticks, value box, pill) is held like its caption: kCaptionRoom of clear room on all
        // four sides, measured on its drawn pixels, and the outline is as far from the control's content on every
        // side (within a pixel), whatever the width of the cell it sits in.
        if (!outline->isPassThrough() && !item.isLaneSync(plan)) {
            const auto ink = inkInEditor(*item.widget, cardInEditor, 2.0f);
            if (!ink.isEmpty()) {
                EXPECT_TRUE(area.contains(ink.expanded(kClear + 1.0f)))
                    << context << ": the outline of " << item.param->paramID << " " << area.toString()
                    << " is closer than " << kClear << " px to its widget's ink " << ink.toString();
                held.push_back({item.param->paramID + " widget", outline, area, ink, item.widget});
                ++counts.widgets;
            }
            auto content =
                synth::ui::widgetInkRect(*item.widget).translated((float)cardInEditor.x, (float)cardInEditor.y);
            if (captioned) {
                const auto words =
                    synth::ui::captionTextRect(*label).translated((float)cardInEditor.x, (float)cardInEditor.y);
                content = content.isEmpty() ? words : content.getUnion(words);
            }
            if (!content.isEmpty()) {
                const float gaps[] = {content.getX() - area.getX(), area.getRight() - content.getRight(),
                                      content.getY() - area.getY(), area.getBottom() - content.getBottom()};
                const auto [least, most] = std::minmax_element(std::begin(gaps), std::end(gaps));
                EXPECT_LE(*most - *least, 1.0f)
                    << context << ": the outline of " << item.param->paramID << " " << area.toString()
                    << " keeps uneven gaps to its content " << content.toString() << " (left " << gaps[0] << ", right "
                    << gaps[1] << ", top " << gaps[2] << ", bottom " << gaps[3] << ")";
            }
        }
        if (!captioned)
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
// caption and each widget (dial, value box, pill) keeps kCaptionClear of room inside its outline on all four sides, the
// outline is equally far from its control's content on every side (computed, and on the drawn pixels at rest,
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
    EXPECT_GT(counts.widgets, 400) << "the sweep checks widgets";
    EXPECT_TRUE(counts.filterTypeChecked) << "the Filter Type title is held whole inside its outline";
}

namespace {

/** The count of `label`'s ink pixels (alpha of at least a fifth) painted with nothing clipping it, and painted the way
 * the card shows it (clipped to its own bounds), at `scale` with spare room all round. A caption whose letters are
 * cut has more ink in the first than in the second. */
std::pair<int, int> captionInkFreeAndClipped(juce::Label& label, float scale) {
    const int margin = (int)std::ceil(12.0f * scale);
    const int w = (int)std::ceil((float)label.getWidth() * scale) + 2 * margin;
    const int h = (int)std::ceil((float)label.getHeight() * scale) + 2 * margin;
    auto count = [&](bool clip) {
        juce::Image image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        {
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale).translated((float)margin, (float)margin));
            if (clip)
                label.paintEntireComponent(g, true);
            else
                label.getLookAndFeel().drawLabel(g, label);
        }
        int ink = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                ink += image.getPixelAt(x, y).getAlpha() >= 48 ? 1 : 0;
        return ink;
    };
    return {count(false), count(true)};
}

} // namespace

// Regression test for FRO2265: the Filter's "Filter Type" title lost the tails of its y and p. Every caption on every
// card, with Edit Layout open (its outlines over the card) and folded or open More controls, in every built-in theme at
// 1x and 2x pixel scale, is drawn with and without its label's clip; any ink the clip removes fails.
TEST(CardBodyOverlap, NoCaptionLosesInkToItsOwnClipInAnyThemeAtAnyScaleWithEditLayoutOpen) {
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const char* extra : {"Track In", "Rec Tap", "Track Audio", "Channel Strip", "Master", "Amp Env", "Filter Env"})
        types.addIfNotAlreadyThere(extra);

    int captions = 0;
    bool filterTypeChecked = false;
    for (const auto& theme : synth::theme::builtInThemes()) {
        ThemeScope scope(theme);
        for (const auto& type : types)
            for (const bool moreOpen : {false, true}) {
                OnCardRig rig;
                auto processor = synth::AIStateMapper::createModule(type);
                if (processor == nullptr)
                    continue;
                const auto id = rig.add(std::move(processor), 200, 200);
                auto* card = rig.card(id);
                ASSERT_NE(card, nullptr) << type;
                if (card->getCardBody() == nullptr)
                    continue;
                card->getCardBody()->setMoreUnfolded(moreOpen);
                if (auto* editor = rig.openOnCard(id))
                    editor->runQueuedSyncForTest();
                for (const auto& item : card->getCardBody()->getPlan().items) {
                    auto* label = dynamic_cast<juce::Label*>(item.label);
                    if (label == nullptr || !label->isVisible() || label->getBounds().isEmpty() ||
                        label->getText().isEmpty())
                        continue;
                    for (const float scale : {1.0f, 2.0f}) {
                        const auto [free, clipped] = captionInkFreeAndClipped(*label, scale);
                        EXPECT_EQ(free, clipped)
                            << theme.name << " / " << type << " caption \"" << label->getText().toStdString()
                            << "\" at " << scale << "x loses " << free - clipped << " ink pixels to its clip";
                        ++captions;
                    }
                    if (item.param != nullptr && item.param->paramID == "filterType")
                        filterTypeChecked = true;
                }
            }
    }
    EXPECT_GT(captions, 1000);
    EXPECT_TRUE(filterTypeChecked) << "the Filter Type caption is in the sweep";
}

// A scenario drags a control by its id, never by a pixel that goes stale when a card's layout changes; and an
// outline says whether it can be dragged at all (lane cells and the footer open the options panel only), so a
// scenario can assert it before it drags (the drag of a panel-only outline does nothing, by design).
TEST(OnCardEditor, EveryOutlineCarriesAnIdAScenarioFindsItByAndItsTitleSaysWhetherItDrags) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 200, 200);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    ASSERT_GE(editor->getOutlineCountForTest(), 5);
    for (const auto& paramId : visibleControls(*rig.card(id))) {
        SCOPED_TRACE(paramId.toStdString());
        auto* outline = editor->getOutlineForTest(paramId);
        ASSERT_NE(outline, nullptr);
        EXPECT_EQ(outline->getComponentID(), "layoutOutline:" + paramId);
        EXPECT_EQ(outline->getComponentID(), CardLayoutOutline::idFor(paramId));
        EXPECT_EQ(outline->getTitle().contains("drag to move"), !outline->isPanelOnly());
    }
}
