// ShapeListEditorLibrary.cpp -- how the shape window meets the shape library: the Start from menu that lists it, a
// library shape dropped on a chip, and Save shape.
#include "ShapeListEditor.h"
#include "ShapeSavePrompts.h"
#include <algorithm>

namespace synth::ui {

using synth::shapes::LibraryShape;
using synth::shapes::ShapeKind;
using synth::shapes::ShapeLibrary;

std::optional<ShapeKind> ShapeListEditor::libraryKind() const {
    return synth::shapes::kindFromWord(adapter_->getPreferenceKind());
}

// A factory shape that is already a built-in entry (the vowels, the ramps) is left out, so no curve is listed twice.
std::vector<ShapeStartFrom> ShapeListEditor::startFromEntries(int* builtInCount, int* savedCount) const {
    auto entries = adapter_->getStartFrom();
    const int builtIn = (int)entries.size();
    int saved = 0;
    if (const auto kind = libraryKind()) {
        const auto& library = ShapeLibrary::shared();
        for (auto& shape : library.userShapes(*kind)) {
            entries.push_back({shape.name, shape.curve});
            ++saved;
        }
        for (auto& shape : ShapeLibrary::factoryShapes(*kind)) {
            const bool known = std::any_of(entries.begin(), entries.begin() + builtIn,
                                           [&](const ShapeStartFrom& e) { return e.curve == shape.curve; });
            if (!known)
                entries.push_back({shape.name, shape.curve});
        }
    }
    if (builtInCount != nullptr)
        *builtInCount = builtIn;
    if (savedCount != nullptr)
        *savedCount = saved;
    return entries;
}

// Each item carries its curve, so a library that changes while the menu is open cannot shift what an item stamps.
void ShapeListEditor::openStartFromMenu() {
    juce::PopupMenu menu;
    juce::Component::SafePointer<ShapeListEditor> safe(this);
    int builtIn = 0;
    int saved = 0;
    const auto entries = startFromEntries(&builtIn, &saved);
    for (int i = 0; i < (int)entries.size(); ++i) {
        if (i == builtIn && saved > 0) {
            menu.addSeparator();
            menu.addSectionHeader("Your shapes");
        } else if (i == builtIn + saved && i < (int)entries.size() && i > 0 && libraryKind().has_value()) {
            menu.addSeparator();
            menu.addSectionHeader("Factory shapes");
        }
        menu.addItem(entries[(size_t)i].name, [safe, curve = entries[(size_t)i].curve] {
            if (safe != nullptr)
                safe->stampCurveOnSelected(curve);
        });
    }
    showMenuNow(menu, startFromButton_);
}

void ShapeListEditor::stampCurveOnSelected(const LfoCustomWave& curve) {
    if (shapes_.empty())
        return;
    edit([&](std::vector<ShapeItem>& shapes) { shapes[(size_t)selected_].curve = curve; });
}

void ShapeListEditor::stampLibraryShape(int index, const LibraryShape& shape) {
    if (index < 0 || index >= (int)shapes_.size() || !acceptsLibraryShape(shape.kind))
        return;
    edit([&](std::vector<ShapeItem>& shapes) { shapes[(size_t)index].curve = shape.curve; });
    setSelected(index);
}

void ShapeListEditor::saveSelectedShape() {
    if (shapes_.empty() || !libraryKind().has_value())
        return;
    juce::Component::SafePointer<ShapeListEditor> safe(this);
    const auto curve = shapes_[(size_t)selected_].curve;
    askShapeName(shapes_[(size_t)selected_].name, {}, [safe, curve](juce::String typed) {
        if (safe != nullptr)
            safe->finishSave(typed, curve);
    });
}

// An empty name asks again rather than saving nothing; a name already saved asks before it is replaced.
void ShapeListEditor::finishSave(const juce::String& typed, const LfoCustomWave& curve) {
    const auto kind = libraryKind();
    const auto name = ShapeLibrary::sanitiseName(typed);
    juce::Component::SafePointer<ShapeListEditor> safe(this);
    if (name.isEmpty()) {
        askShapeName({}, "Give the shape a name.", [safe, curve](juce::String again) {
            if (safe != nullptr)
                safe->finishSave(again, curve);
        });
        return;
    }
    if (kind.has_value() && ShapeLibrary::shared().exists(*kind, name)) {
        askShapeReplace(name, [safe, name, curve](bool replace) {
            if (replace && safe != nullptr)
                safe->writeShape(name, curve, true);
        });
        return;
    }
    writeShape(name, curve, false);
}

void ShapeListEditor::writeShape(const juce::String& name, const LfoCustomWave& curve, bool replace) {
    const auto kind = libraryKind();
    if (!kind.has_value())
        return;
    const auto result = ShapeLibrary::shared().save(*kind, name, curve, replace);
    const bool ok = result == ShapeLibrary::SaveResult::Saved || result == ShapeLibrary::SaveResult::Replaced;
    juce::AccessibilityHandler::postAnnouncement(ok ? "Saved shape " + name : "Could not save shape " + name,
                                                 juce::AccessibilityHandler::AnnouncementPriority::medium);
}

} // namespace synth::ui
