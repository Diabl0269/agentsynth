// MainComponentCollectArchive.cpp — the "Collect & Archive..." menu item: saves the project if it
// has never been saved, copies every external module file into the bundle (synth::ProjectCollector),
// saves again so project.json points at the copies, and optionally zips the bundle for sending.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "MainComponentInternal.h"
#include "Project/ProjectCollector.h"
#include "UI/Chrome/ProgressTaskWindow.h"
#include <memory>

using synth::CollectArchiveChoice;

namespace {

// Longest list of paths a report spells out; the rest is summarised as a count.
constexpr int kMaxListedPaths = 8;

juce::String listPaths(const juce::String& heading, const juce::StringArray& paths) {
    if (paths.isEmpty())
        return {};
    juce::String text = "\n\n" + heading + ":";
    for (int i = 0; i < juce::jmin(paths.size(), kMaxListedPaths); ++i)
        text << "\n  " << paths[i];
    if (paths.size() > kMaxListedPaths)
        text << "\n  ...and " << (paths.size() - kMaxListedPaths) << " more";
    return text;
}

juce::String describeCollect(const synth::CollectResult& result) {
    juce::String text = result.filesCopied == 0
                            ? juce::String("Everything the project uses was already inside it.")
                            : "Collected " + juce::String(result.filesCopied) + " file(s) into the project.";
    text << listPaths("Not found (left as they were)", result.missing);
    text << listPaths("Could not copy (left as they were)", result.failures);
    return text;
}

} // namespace

// Collect writes into the bundle, so the normal save flow runs first: a never-saved project gets
// its Save dialog, and a cancelled or failed save abandons the command.
void MainComponent::promptCollectAndArchive() {
    juce::Component::SafePointer<MainComponent> safeThis(this);
    auto onChoice = [safeThis](CollectArchiveChoice choice) {
        auto* self = safeThis.getComponent();
        if (self == nullptr || choice == CollectArchiveChoice::Cancel)
            return;
        const bool thenZip = choice == CollectArchiveChoice::CollectAndZip;
        self->performSaveProject(false, [safeThis, thenZip](bool saved) {
            if (auto* target = safeThis.getComponent(); target != nullptr && saved)
                target->startCollect(thenZip);
        });
    };
    if (collectArchiveSeams.choicePrompt) {
        collectArchiveSeams.choicePrompt(onChoice);
        return;
    }
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Collect & Archive")
                       .withMessage("Copy every sample and wavetable the project uses from elsewhere on disk into "
                                    "the project folder, then save. Originals are never moved or deleted.")
                       .withButton("Collect")
                       .withButton("Collect and make zip...")
                       .withButton("Cancel");
    // The first button returns 1, the second 2; Cancel and a dismissed window return 0.
    juce::AlertWindow::showAsync(options, [onChoice](int result) {
        onChoice(result == 1   ? CollectArchiveChoice::CollectOnly
                 : result == 2 ? CollectArchiveChoice::CollectAndZip
                               : CollectArchiveChoice::Cancel);
    });
}

// The plan reads the live graph here on the message thread; only the file copies go to the
// background, and nothing is rewritten until they are all done.
void MainComponent::startCollect(bool thenZip) {
    isCollectInProgress_ = true;
    auto plan =
        std::make_shared<synth::CollectPlan>(synth::ProjectCollector::plan(audioEngine.getGraph(), currentBundleDir_));
    auto result = std::make_shared<synth::CollectResult>();
    juce::Component::SafePointer<MainComponent> safeThis(this);
    runProjectTask(
        "Collecting project files", true,
        [plan, result](const synth::CollectArchiveSeams::ProgressFn& progress) {
            *result = synth::ProjectCollector::copy(*plan, progress);
        },
        [safeThis, result, thenZip](bool cancelled) {
            if (auto* self = safeThis.getComponent())
                self->finishCollect(*result, cancelled || result->cancelled, thenZip);
        });
}

// Collect is not an undo step: rewriting module state records none (loading a sample never has),
// so the project is saved right away and memory and disk agree. It is non-destructive instead —
// it only added copies and repointed modules, and a later undo past this point restores the old
// paths while the copies stay put.
void MainComponent::finishCollect(const synth::CollectResult& result, bool cancelled, bool thenZip) {
    isCollectInProgress_ = false;
    if (cancelled) {
        reportCollectArchive("Collect cancelled", "The project was not changed. Files copied before you "
                                                  "cancelled stay in the project folder.");
        return;
    }
    synth::ProjectCollector::apply(audioEngine.getGraph(), result);
    if (!saveToFile(currentBundleDir_)) {
        reportCollectArchive("Collect failed", "The files were copied, but the project could not be saved.");
        return;
    }
    const auto summary = describeCollect(result);
    if (result.missing.isEmpty() && result.failures.isEmpty())
        statusBar.showMessage(summary);
    else
        reportCollectArchive("Collect & Archive", summary);
    if (thenZip)
        promptArchiveFile();
}

void MainComponent::promptArchiveFile() {
    juce::Component::SafePointer<MainComponent> safeThis(this);
    auto onFile = [safeThis](const juce::File& picked) {
        if (auto* self = safeThis.getComponent(); self != nullptr && picked != juce::File{})
            self->writeProjectArchive(picked.hasFileExtension("zip") ? picked : picked.withFileExtension("zip"));
    };
    if (collectArchiveSeams.zipFilePrompt) {
        collectArchiveSeams.zipFilePrompt(onFile);
        return;
    }
    const auto suggested = currentBundleDir_.getSiblingFile(currentBundleDir_.getFileNameWithoutExtension() + ".zip");
    fileChooser = std::make_unique<juce::FileChooser>("Save Project Archive", suggested, "*.zip");
    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                 juce::FileBrowserComponent::warnAboutOverwriting;
    fileChooser->launchAsync(flags, [onFile](const juce::FileChooser& fc) { onFile(fc.getResult()); });
}

// Exports/ holds renders, which are outputs the recipient can make again, so it stays out of the
// zip. No Cancel button: the zip writer cannot stop halfway (see ProgressTaskWindow).
void MainComponent::writeProjectArchive(const juce::File& zipFile) {
    isCollectInProgress_ = true;
    auto error = std::make_shared<juce::String>();
    const auto bundleDir = currentBundleDir_;
    juce::Component::SafePointer<MainComponent> safeThis(this);
    runProjectTask(
        "Writing project archive", false,
        [bundleDir, zipFile, error](const synth::CollectArchiveSeams::ProgressFn& progress) {
            progress(-1.0);
            *error = synth::ProjectCollector::writeArchive(bundleDir, zipFile, {detail::kExportsFolderName}, nullptr);
        },
        [safeThis, error, zipFile](bool) {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->isCollectInProgress_ = false;
            if (error->isEmpty())
                self->statusBar.showMessage("Archived to " + zipFile.getFileName());
            else
                self->reportCollectArchive("Archive failed", *error);
        });
}

void MainComponent::runProjectTask(const juce::String& title, bool cancellable, synth::CollectArchiveSeams::Work work,
                                   std::function<void(bool)> done) {
    if (collectArchiveSeams.runTask) {
        collectArchiveSeams.runTask(title, cancellable, std::move(work), std::move(done));
        return;
    }
    synth::ui::ProgressTaskWindow::launch(title, cancellable, std::move(work), std::move(done));
}

void MainComponent::reportCollectArchive(const juce::String& title, const juce::String& message) {
    statusBar.showMessage(title);
    if (collectArchiveSeams.report) {
        collectArchiveSeams.report(title, message);
        return;
    }
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, message);
}

// Greyed out while a collect or zip is running (one progress window at a time) and while a render
// owns the engine, since Collect ends in a save.
bool MainComponent::isCollectArchiveAvailable() const { return !isCollectInProgress_ && !isBounceInProgress_; }
