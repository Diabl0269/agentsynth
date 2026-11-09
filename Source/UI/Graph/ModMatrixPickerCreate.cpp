// Concern: the Mod Matrix source picker's create rows (see ModMatrixPickerCreate.h).
#include "UI/Graph/ModMatrixPickerCreate.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModSourceCatalog.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <algorithm>
#include <iterator>

namespace synth::ui {

namespace {
// Clear of the (node uid << 8 | channel) ids of real sources and of the hosted-parameter ids above them.
constexpr int kCreateIdBase = 0x7D000000;

// "Create new LFO"; the merged envelope key reads as "envelope" like the timeline's "New envelope".
juce::String rowText(const NewModuleSource& type) {
    return "Create new " + (type.label == "Env" ? juce::String("envelope") : type.label);
}

// The middle of the largest strip of the editor the Mod Matrix panel leaves free, in canvas coordinates.
juce::Point<int> visibleFreeCentre(GraphEditor& editor) {
    const auto area = editor.getLocalBounds();
    auto free = area;
    if (editor.isModMatrixVisible()) {
        const auto panel = editor.getModMatrix().getBounds().getIntersection(area);
        const juce::Rectangle<int> strips[] = {area.withRight(panel.getX()), area.withLeft(panel.getRight()),
                                               area.withBottom(panel.getY()), area.withTop(panel.getBottom())};
        free = *std::max_element(std::begin(strips), std::end(strips), [](const auto& a, const auto& b) {
            return a.getWidth() * a.getHeight() < b.getWidth() * b.getHeight();
        });
        if (panel.isEmpty())
            free = area;
    }
    const auto visible = editor.getVisibleCanvasRect();
    return {
        juce::roundToInt(visible.getX() + (float)free.getCentreX() * visible.getWidth() / (float)area.getWidth()),
        juce::roundToInt(visible.getY() + (float)free.getCentreY() * visible.getHeight() / (float)area.getHeight())};
}

ModuleComponent* cardFor(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == id)
            return comp;
    return nullptr;
}
} // namespace

std::vector<ModMatrixPicker::Item> createSourceItems() {
    std::vector<ModMatrixPicker::Item> items;
    int index = 0;
    for (const auto& type : newModuleSources()) {
        ModMatrixPicker::Item item;
        item.id = kCreateIdBase + index++;
        item.category = "Create";
        item.text = rowText(type);
        item.searchText = type.label + " " + type.aliases + " " + type.typeName;
        item.isNew = true;
        item.createsOnMatch = true;
        items.push_back(std::move(item));
    }
    return items;
}

std::optional<CreateSourcePick> createSourcePickFor(int id) {
    const auto& types = newModuleSources();
    const int index = id - kCreateIdBase;
    if (index < 0 || index >= (int)types.size())
        return std::nullopt;
    return CreateSourcePick{types[(size_t)index].typeName, types[(size_t)index].channel};
}

// The card is placed once it exists (its real size is known then) and enters inside the glide Scope, so it grows in
// like a card added from the library rather than popping.
juce::AudioProcessorGraph::NodeID createSourceModule(GraphEditor& editor, AppUndoManager* undo,
                                                     const CreateSourcePick& pick) {
    auto& graph = editor.getAudioEngine().getGraph();
    juce::AudioProcessorGraph::NodeID added;
    const auto mutation = [&] {
        auto processor = synth::AIStateMapper::createModule(pick.typeName);
        if (processor == nullptr)
            return;
        auto node = graph.addNode(std::move(processor));
        if (node == nullptr)
            return;
        synth::AIStateMapper::ensureNodeUuid(node.get());
        const auto centre = visibleFreeCentre(editor);
        node->properties.set("x", centre.x);
        node->properties.set("y", centre.y);
        added = node->nodeID;
        CardGlideAnimator::Scope glide(editor.getCardGlide());
        editor.updateComponents();
        if (auto* card = cardFor(editor, added)) {
            const auto size = card->getBounds();
            card->setTopLeftPosition(
                editor.resolvePlacement({centre.x - size.getWidth() / 2, centre.y - size.getHeight() / 2},
                                        size.getWidth(), size.getHeight(), added));
            editor.updateModulePosition(card);
            editor.getCardGlide().noteEnter(card, added.uid);
        }
    };
    if (undo != nullptr)
        undo->recordGraphAndMacroChange(graph, editor.getMacros(), mutation);
    else
        mutation();
    return added;
}

} // namespace synth::ui
