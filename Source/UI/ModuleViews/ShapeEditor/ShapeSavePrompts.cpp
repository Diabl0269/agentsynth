// ShapeSavePrompts.cpp -- the name prompt and the replace question of Save shape.
#include "ShapeSavePrompts.h"
#include "Modules/Shapes/ShapeLibrary.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::ui {

namespace test_hooks {
std::function<void(const juce::String&, const juce::String&, std::function<void(juce::String)>)>&
shapeNamePromptHookForTest() {
    static std::function<void(const juce::String&, const juce::String&, std::function<void(juce::String)>)> hook;
    return hook;
}
std::function<void(const juce::String&, std::function<void(bool)>)>& shapeReplacePromptHookForTest() {
    static std::function<void(const juce::String&, std::function<void(bool)>)> hook;
    return hook;
}
std::function<void(const juce::String&, std::function<void(bool)>)>& shapeDeletePromptHookForTest() {
    static std::function<void(const juce::String&, std::function<void(bool)>)> hook;
    return hook;
}
} // namespace test_hooks

// The same async AlertWindow idiom as saving a snippet; the window frees itself when the callback has run.
void askShapeName(const juce::String& suggested, const juce::String& problem, std::function<void(juce::String)> done) {
    if (auto& hook = test_hooks::shapeNamePromptHookForTest()) {
        hook(suggested, problem, std::move(done));
        return;
    }
    auto* window = new juce::AlertWindow("Save shape", problem.isEmpty() ? "Name this shape:" : problem,
                                         juce::AlertWindow::NoIcon);
    window->addTextEditor("name", suggested, "Shape name:");
    if (auto* editor = window->getTextEditor("name"))
        editor->setInputRestrictions(synth::shapes::ShapeLibrary::kMaxNameLength);
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    PopupMotion::attach(*window);
    window->enterModalState(true, juce::ModalCallbackFunction::create([window, done = std::move(done)](int result) {
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (result == 1 && done)
                                    done(owned->getTextEditorContents("name"));
                            }),
                            false);
}

void askShapeReplace(const juce::String& name, std::function<void(bool)> done) {
    if (auto& hook = test_hooks::shapeReplacePromptHookForTest()) {
        hook(name, std::move(done));
        return;
    }
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::WarningIcon)
                       .withTitle("Replace shape")
                       .withMessage("A shape called \"" + name + "\" is already saved. Replace it?")
                       .withButton("Replace")
                       .withButton("Cancel");
    juce::AlertWindow::showAsync(options, [done = std::move(done)](int result) {
        if (done)
            done(result == 1);
    });
}

void askShapeDelete(const juce::String& name, std::function<void(bool)> done) {
    if (auto& hook = test_hooks::shapeDeletePromptHookForTest()) {
        hook(name, std::move(done));
        return;
    }
    auto options =
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Delete shape")
            .withMessage("Delete the saved shape \"" + name + "\"? This removes its file and cannot be undone.")
            .withButton("Delete")
            .withButton("Cancel");
    juce::AlertWindow::showAsync(options, [done = std::move(done)](int result) {
        if (done)
            done(result == 1);
    });
}

} // namespace synth::ui
