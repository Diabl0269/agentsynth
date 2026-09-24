// Concern: the module cards' contribution to the pick-target overlay's candidate list.

#include "UI/Graph/PickTargetOverlay/GraphPickCandidates.h"

#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth::ui {

void collectGraphPickCandidates(GraphEditor& editor, std::vector<PickCandidate>& out) {
    for (auto* module : editor.getModuleComponents())
        if (module != nullptr)
            module->collectPickCandidates(out);
}

} // namespace synth::ui
