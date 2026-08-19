#include "LocalHistoryStore.h"
#include "../Branding.h"
#include <algorithm>

namespace synth {

namespace {

// juce::Time::fromISO8601() returns a default-constructed (epoch) Time on parse failure, which is
// indistinguishable from a genuinely epoch timestamp — but a real conversation's updatedAt is
// never actually 1970-01-01, so treating "parses to epoch" as "unparseable" is safe here. Pruning
// must treat an unparseable timestamp as "keep" (not "infinitely old"), or corrupt data quietly
// turns into data loss.
bool tryParseIso(const juce::String& iso, juce::Time& out) {
    if (iso.isEmpty())
        return false;
    auto parsed = juce::Time::fromISO8601(iso);
    if (parsed == juce::Time())
        return false;
    out = parsed;
    return true;
}

} // namespace

juce::File LocalHistoryStore::getDefaultHistoryDirectory() {
    juce::File folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile(synth::branding::kSettingsFolderName)
                            .getChildFile("History");

    if (!folder.exists())
        folder.createDirectory();

    return folder;
}

juce::String LocalHistoryStore::newConversationId() { return juce::Uuid().toString(); }

// ---------------------------------------------------------------------------------------
// Pure JSON transforms
// ---------------------------------------------------------------------------------------

juce::var LocalHistoryStore::conversationToVar(const LocalConversation& conversation) {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("id", conversation.id);
    root->setProperty("title", conversation.title);
    root->setProperty("createdAt", conversation.createdAt);
    root->setProperty("updatedAt", conversation.updatedAt);

    juce::Array<juce::var> messages;
    for (const auto& m : conversation.messages) {
        juce::DynamicObject::Ptr mo = new juce::DynamicObject();
        mo->setProperty("role", m.role);
        mo->setProperty("content", m.content);
        mo->setProperty("createdAt", m.createdAt);
        messages.add(juce::var(mo.get()));
    }
    root->setProperty("messages", messages);

    return juce::var(root.get());
}

bool LocalHistoryStore::conversationFromVar(const juce::var& v, LocalConversation& out) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty("id"))
        return false;

    juce::String id = obj->getProperty("id").toString();
    if (id.isEmpty())
        return false;

    LocalConversation result;
    result.id = id;
    result.title = obj->hasProperty("title") ? obj->getProperty("title").toString() : juce::String();
    result.createdAt = obj->hasProperty("createdAt") ? obj->getProperty("createdAt").toString() : juce::String();
    result.updatedAt = obj->hasProperty("updatedAt") ? obj->getProperty("updatedAt").toString() : juce::String();

    if (obj->hasProperty("messages")) {
        if (auto* arr = obj->getProperty("messages").getArray()) {
            for (const auto& mv : *arr) {
                auto* mo = mv.getDynamicObject();
                if (mo == nullptr)
                    continue;
                LocalConversationMessage msg;
                msg.role = mo->hasProperty("role") ? mo->getProperty("role").toString() : juce::String();
                msg.content = mo->hasProperty("content") ? mo->getProperty("content").toString() : juce::String();
                msg.createdAt = mo->hasProperty("createdAt") ? mo->getProperty("createdAt").toString() : juce::String();
                result.messages.push_back(std::move(msg));
            }
        }
    }

    out = std::move(result);
    return true;
}

bool LocalHistoryStore::summaryFromVar(const juce::var& v, LocalConversationSummary& out) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty("id"))
        return false;

    juce::String id = obj->getProperty("id").toString();
    if (id.isEmpty())
        return false;

    LocalConversationSummary result;
    result.id = id;
    result.title = obj->hasProperty("title") ? obj->getProperty("title").toString() : juce::String();
    result.createdAt = obj->hasProperty("createdAt") ? obj->getProperty("createdAt").toString() : juce::String();
    result.updatedAt = obj->hasProperty("updatedAt") ? obj->getProperty("updatedAt").toString() : juce::String();

    out = std::move(result);
    return true;
}

// ---------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------

juce::File LocalHistoryStore::fileForId(const juce::File& dir, const juce::String& id) {
    auto safe = juce::File::createLegalFileName(id.trim());
    if (safe.isEmpty())
        return {};
    return dir.getChildFile(safe + ".json");
}

int LocalHistoryStore::sanitiseRetentionDays(int retentionDays) {
    switch (retentionDays) {
    case 30:
    case 90:
    case 180:
    case 365:
    case kRetainForever:
        return retentionDays;
    default:
        return kDefaultRetentionDays;
    }
}

bool LocalHistoryStore::save(const juce::File& dir, const LocalConversation& conversation, int retentionDays) {
    if (conversation.id.isEmpty())
        return false;

    if (!dir.exists() && !dir.createDirectory())
        return false;

    auto file = fileForId(dir, conversation.id);
    if (file == juce::File())
        return false;

    bool ok = file.replaceWithText(juce::JSON::toString(conversationToVar(conversation)));
    if (ok)
        pruneOldConversations(dir, retentionDays);
    return ok;
}

std::vector<LocalConversationSummary> LocalHistoryStore::list(const juce::File& dir) {
    std::vector<LocalConversationSummary> result;
    if (!dir.isDirectory())
        return result;

    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, false, "*.json");

    for (const auto& file : files) {
        auto json = juce::JSON::parse(file.loadFileAsString());
        LocalConversationSummary summary;
        if (!summaryFromVar(json, summary))
            continue; // unreadable/corrupt file: skip it rather than surface a broken row
        result.push_back(std::move(summary));
    }

    // ISO8601 timestamps sort lexicographically the same as chronologically, so a plain string
    // comparison is enough — no need to parse every timestamp just to order the list.
    std::sort(result.begin(), result.end(), [](const LocalConversationSummary& a, const LocalConversationSummary& b) {
        return a.updatedAt > b.updatedAt;
    });

    return result;
}

bool LocalHistoryStore::get(const juce::File& dir, const juce::String& id, LocalConversation& out) {
    auto file = fileForId(dir, id);
    if (file == juce::File() || !file.existsAsFile())
        return false;

    auto json = juce::JSON::parse(file.loadFileAsString());
    return conversationFromVar(json, out);
}

bool LocalHistoryStore::deleteOne(const juce::File& dir, const juce::String& id) {
    auto file = fileForId(dir, id);
    if (file == juce::File() || !file.existsAsFile())
        return false;
    return file.deleteFile();
}

int LocalHistoryStore::deleteAll(const juce::File& dir) {
    if (!dir.isDirectory())
        return 0;

    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, false, "*.json");

    int count = 0;
    for (auto& file : files)
        if (file.deleteFile())
            ++count;
    return count;
}

void LocalHistoryStore::pruneOldConversations(const juce::File& dir, int retentionDays) {
    const int days = sanitiseRetentionDays(retentionDays);

    if (days != kRetainForever) {
        const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days((double)days);
        for (const auto& summary : list(dir)) {
            juce::Time updated;
            if (!tryParseIso(summary.updatedAt, updated))
                continue; // corrupt/unparseable timestamp: keep the file, don't treat as ancient
            if (updated < cutoff)
                deleteOne(dir, summary.id);
        }
    }

    // Hard cap, independent of (and enforced after) the age-based rule above.
    auto remaining = list(dir); // re-list: age pruning above may have removed files
    if ((int)remaining.size() > kHardCapFiles) {
        // remaining is sorted updatedAt descending (most recent first) — drop from the tail
        // (oldest) down to the cap.
        for (size_t i = (size_t)kHardCapFiles; i < remaining.size(); ++i)
            deleteOne(dir, remaining[i].id);
    }
}

} // namespace synth
