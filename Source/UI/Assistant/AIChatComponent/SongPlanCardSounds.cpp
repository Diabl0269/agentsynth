// SongPlanCardSounds.cpp -- the plan card's offer to download the core instruments before building a song that
// names them (docs/modules/sound-pack-downloads.md#song-builds-and-the-core-instruments).
#include "SongPlanCard.h"
#include "SoundPacks/CoreSounds.h"
#include <algorithm>

namespace synth {

namespace {

using Installer = synth::soundpacks::SoundPackInstaller;
using synth::soundpacks::kCorePackId;

juce::String percent(float progress) {
    return progress < 0.0f ? juce::String() : " " + juce::String(juce::roundToInt(progress * 100.0f)) + "%";
}

bool isInstalled(const std::optional<Installer::Row>& row) {
    return row && (row->state == Installer::State::Installed || row->state == Installer::State::UpdateAvailable);
}

bool isInstalling(const Installer::Row& row) {
    return row.state == Installer::State::Downloading || row.state == Installer::State::Verifying ||
           row.state == Installer::State::Installing;
}

} // namespace

// The distinct packs the plan's sounds come from, in the order the plan names them.
std::vector<juce::String> SongPlanCard::planPacks() const {
    std::vector<juce::String> packs;
    for (const auto& id : session.reply.plan.coreSounds())
        if (const auto* sound = synth::soundpacks::findCoreSound(id))
            if (const auto pack = synth::soundpacks::packIdOf(*sound);
                std::find(packs.begin(), packs.end(), pack) == packs.end())
                packs.push_back(pack);
    return packs;
}

std::vector<juce::String> SongPlanCard::neededPacks() const {
    auto packs = planPacks();
    packs.erase(
        std::remove_if(packs.begin(), packs.end(), [this](const auto& id) { return isInstalled(installer_->row(id)); }),
        packs.end());
    return packs;
}

bool SongPlanCard::packsInstalled() const { return neededPacks().empty(); }

// The offer is made while the plan names sounds from a pack that is not on this machine.
bool SongPlanCard::offerNeeded() const { return !neededPacks().empty(); }

bool SongPlanCard::downloadBusy() const {
    bool installing = false;
    for (const auto& id : neededPacks())
        if (const auto row = installer_->row(id))
            installing = installing || isInstalling(*row);
    return awaitingIndex_ || installing;
}

void SongPlanCard::startDownload() {
    downloadFailed_ = false;
    session.buildAfterDownload = true;
    const auto needed = neededPacks();
    const bool listed = installer_->indexStatus() == Installer::IndexStatus::Loaded &&
                        std::all_of(needed.begin(), needed.end(), [this](const auto& id) {
                            const auto row = installer_->row(id);
                            return row && row->inIndex;
                        });
    if (listed) {
        installAll(needed); // may finish, and build, before it returns
        return;
    }
    awaitingIndex_ = true;
    installer_->refreshIndex();
    refreshOffer();
    layoutChanged();
}

// Starts every pack in `packs`; a pack that finished while an earlier one installed is skipped, and a cancel stops the
// rest.
void SongPlanCard::installAll(const std::vector<juce::String>& packs) {
    for (const auto& id : packs)
        if (session.buildAfterDownload && !isInstalled(installer_->row(id)))
            installer_->install(id);
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
        const auto needed = neededPacks();
        const bool listed = installer_->indexStatus() == Installer::IndexStatus::Loaded &&
                            std::all_of(needed.begin(), needed.end(), [this](const auto& id) {
                                const auto row = installer_->row(id);
                                return row && row->inIndex;
                            });
        if (listed) {
            awaitingIndex_ = false;
            installAll(needed);
            return;
        }
        if (installer_->indexStatus() == Installer::IndexStatus::Failed) {
            awaitingIndex_ = false;
            session.buildAfterDownload = false;
            downloadFailed_ = true;
        }
    }
    if (session.buildAfterDownload) {
        if (packsInstalled()) {
            session.buildAfterDownload = false;
            if (actions.onBuild)
                actions.onBuild(); // the chat may replace this card; nothing below touches it
            return;
        }
        bool failed = false;
        for (const auto& id : neededPacks())
            if (const auto row = installer_->row(id))
                failed = failed || (!isInstalling(*row) && row->error.isNotEmpty());
        if (!awaitingIndex_ && failed) {
            session.buildAfterDownload = false;
            downloadFailed_ = true;
        }
    }
    refreshOffer();
    applyPhase();
    layoutChanged();
}

// What the offer calls the packs: the old wording when only the core pack is missing, else the pack names.
juce::String SongPlanCard::packsPhrase(const std::vector<juce::String>& packs) const {
    if (packs.size() == 1 && packs.front() == kCorePackId)
        return "the core instruments";
    juce::StringArray names;
    for (const auto& id : packs) {
        const auto row = installer_->row(id);
        names.add(row && row->name.isNotEmpty() ? row->name : id);
    }
    return names.joinIntoString(" and ");
}

// The offer line, its bar and the Build button's wording for the installer's current state.
void SongPlanCard::refreshOffer() {
    const auto needed = neededPacks();
    const bool isNeeded = !needed.empty();
    if (isNeeded && installer_->indexStatus() == Installer::IndexStatus::NotLoaded)
        installer_->refreshIndex(); // for the size in the line
    const bool busy = downloadBusy();

    // Every pack of the plan counts, so the bar keeps its place as one pack finishes and the next starts.
    juce::int64 totalBytes = 0, doneBytes = 0, shownBytes = 0;
    bool downloading = false, installing = false, sizeKnown = true;
    for (const auto& id : planPacks()) {
        const auto row = installer_->row(id);
        if (!row || isInstalled(row))
            continue;
        const juce::int64 weight = row->downloadBytes > 0 ? row->downloadBytes : 1;
        totalBytes += weight;
        shownBytes += row->inIndex ? row->downloadBytes : 0;
        downloading = downloading || row->state == Installer::State::Downloading;
        installing =
            installing || row->state == Installer::State::Verifying || row->state == Installer::State::Installing;
        if (row->state == Installer::State::Downloading && row->progress < 0.0f)
            sizeKnown = false;
        doneBytes += row->state == Installer::State::Downloading
                         ? (juce::int64)(juce::jmax(0.0f, row->progress) * (float)weight)
                     : row->state == Installer::State::Verifying || row->state == Installer::State::Installing ? weight
                                                                                                               : 0;
    }
    const float progress = !sizeKnown ? -1.0f : totalBytes > 0 ? (float)doneBytes / (float)totalBytes : 0.0f;
    const juce::String size =
        shownBytes > 0 ? " (" + synth::soundpacks::formatPackSize(shownBytes) + ")" : juce::String();
    const bool coreOnly = needed.size() == 1 && needed.front() == kCorePackId;
    const juce::String what = coreOnly ? "the core instruments" : "the sounds";

    juce::String line;
    if (busy) {
        line = downloading  ? "Downloading " + what + percent(progress)
               : installing ? "Installing " + what
                            : "Checking for " + what + "...";
    } else if (downloadFailed_) {
        line = coreOnly ? "Couldn't download the instruments." : "Couldn't download the sounds.";
    } else {
        const juce::String names = synth::soundpacks::coreSoundNames(session.reply.plan.coreSounds());
        line = coreOnly ? "Uses real instruments: " + names + ". Download the core instruments" + size + " first?"
                        : "Uses real sounds: " + names + ". Download " + packsPhrase(needed) + size + " first?";
    }
    soundsLabel.setText(line, juce::dontSendNotification);
    soundsLabel.setDescription(line);
    soundsProgress.setFraction(downloading ? juce::jmax(0.0f, progress) : 1.0f);
    soundsBarFade_.setShown(isNeeded && busy && session.phase != SongSession::Phase::dismissed);

    if (isNeeded) {
        const int units = session.reply.units;
        const juce::String uses = juce::String(units) + (units == 1 ? " use" : " uses");
        buildButton.setButtonText("Download first" + juce::String::fromUTF8(" \xC2\xB7 ") + uses);
        buildButton.setTitle("Download first, then build song, " + uses);
        buildButton.setTooltip("Download " + packsPhrase(needed) + size + ", then spend " + uses +
                               " to build this song");
    }
}

juce::String SongPlanCard::soundsLineForTesting() const { return soundsLabel.getText(); }
bool SongPlanCard::soundsOfferShownForTesting() const { return soundsFade_.isShown(); }

} // namespace synth
