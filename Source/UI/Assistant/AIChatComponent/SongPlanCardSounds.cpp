// SongPlanCardSounds.cpp -- the plan card's offer to download the core instruments before building a song that
// names them (docs/modules/sound-pack-downloads.md#song-builds-and-the-core-instruments).
#include "SongPlanCard.h"
#include "SoundPacks/CoreSounds.h"

namespace synth {

namespace {

using Installer = synth::soundpacks::SoundPackInstaller;
using synth::soundpacks::kCorePackId;

juce::String percent(float progress) {
    return progress < 0.0f ? juce::String() : " " + juce::String(juce::roundToInt(progress * 100.0f)) + "%";
}

} // namespace

bool SongPlanCard::coreInstalled() const {
    const auto row = installer_->row(kCorePackId);
    return row && (row->state == Installer::State::Installed || row->state == Installer::State::UpdateAvailable);
}

// The offer is made while the plan names core instruments and they are not on this machine.
bool SongPlanCard::offerNeeded() const { return !session.reply.plan.coreSounds().empty() && !coreInstalled(); }

bool SongPlanCard::downloadBusy() const {
    const auto row = installer_->row(kCorePackId);
    const bool installing =
        row && (row->state == Installer::State::Downloading || row->state == Installer::State::Verifying ||
                row->state == Installer::State::Installing);
    return awaitingIndex_ || installing;
}

void SongPlanCard::startDownload() {
    downloadFailed_ = false;
    session.buildAfterDownload = true;
    const auto row = installer_->row(kCorePackId);
    if (installer_->indexStatus() == Installer::IndexStatus::Loaded && row && row->inIndex) {
        installer_->install(kCorePackId); // may finish, and build, before it returns
        return;
    }
    awaitingIndex_ = true;
    installer_->refreshIndex();
    refreshOffer();
    layoutChanged();
}

void SongPlanCard::continueWithSynths() {
    session.buildAfterDownload = false;
    awaitingIndex_ = false;
    if (actions.onBuild)
        actions.onBuild();
}

// The card is a view of the shared installer: a download started here or in Settings shows the same progress.
void SongPlanCard::soundPacksChanged() {
    if (awaitingIndex_) {
        const auto row = installer_->row(kCorePackId);
        if (installer_->indexStatus() == Installer::IndexStatus::Loaded && row && row->inIndex) {
            awaitingIndex_ = false;
            installer_->install(kCorePackId);
            return;
        }
        if (installer_->indexStatus() == Installer::IndexStatus::Failed) {
            awaitingIndex_ = false;
            session.buildAfterDownload = false;
            downloadFailed_ = true;
        }
    }
    if (session.buildAfterDownload) {
        const auto row = installer_->row(kCorePackId);
        if (coreInstalled()) {
            session.buildAfterDownload = false;
            if (actions.onBuild)
                actions.onBuild(); // the chat may replace this card; nothing below touches it
            return;
        }
        if (!downloadBusy() && row && row->error.isNotEmpty()) {
            session.buildAfterDownload = false;
            downloadFailed_ = true;
        }
    }
    refreshOffer();
    applyPhase();
    layoutChanged();
}

// The offer line, its bar and the Build button's wording for the installer's current state.
void SongPlanCard::refreshOffer() {
    const bool needed = offerNeeded();
    if (needed && installer_->indexStatus() == Installer::IndexStatus::NotLoaded)
        installer_->refreshIndex(); // for the size in the line
    const auto row = installer_->row(kCorePackId);
    const bool busy = downloadBusy();

    juce::String line;
    if (busy) {
        line = row && row->state == Installer::State::Downloading
                   ? "Downloading the core instruments" + percent(row->progress)
               : row && (row->state == Installer::State::Verifying || row->state == Installer::State::Installing)
                   ? juce::String("Installing the core instruments")
                   : juce::String("Checking for the core instruments...");
    } else if (downloadFailed_) {
        line = "Couldn't download the instruments.";
    } else {
        const juce::String size = row && row->inIndex && row->downloadBytes > 0
                                      ? " (" + synth::soundpacks::formatPackSize(row->downloadBytes) + ")"
                                      : juce::String();
        line = "Uses real instruments: " + synth::soundpacks::coreSoundNames(session.reply.plan.coreSounds()) +
               ". Download the core instruments" + size + " first?";
    }
    soundsLabel.setText(line, juce::dontSendNotification);
    soundsLabel.setDescription(line);
    soundsProgress.setFraction(row && row->state == Installer::State::Downloading ? juce::jmax(0.0f, row->progress)
                                                                                  : 1.0f);
    soundsBarFade_.setShown(needed && busy && session.phase != SongSession::Phase::dismissed);

    if (needed) {
        const juce::String size = row && row->inIndex && row->downloadBytes > 0
                                      ? " (" + synth::soundpacks::formatPackSize(row->downloadBytes) + ")"
                                      : juce::String();
        const int units = session.reply.units;
        const juce::String uses = juce::String(units) + (units == 1 ? " use" : " uses");
        buildButton.setButtonText("Download first");
        buildButton.setTitle("Download first, then build song, " + uses);
        buildButton.setTooltip("Download the core instruments" + size + ", then spend " + uses + " to build this song");
    }
}

juce::String SongPlanCard::soundsLineForTesting() const { return soundsLabel.getText(); }
bool SongPlanCard::soundsOfferShownForTesting() const { return soundsFade_.isShown(); }

} // namespace synth
