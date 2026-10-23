// ShapeLibrary.cpp -- the user's saved shapes on disk. Anything read from a file is untrusted: the size is capped
// before the file is read, the curve goes through LfoCustomWave's sanitising and a file that does not hold a readable
// shape of the right kind is skipped.
#include "Modules/Shapes/ShapeLibrary.h"
#include "Modules/Shapes/MorphShapes.h"
#include "UserSettings.h"
#include <algorithm>
#include <cmath>

namespace synth::shapes {

namespace {
constexpr int kFileVersion = 1;

const char* folderName(ShapeKind kind) { return kind == ShapeKind::Filter ? "Filter" : "LFO"; }

int countFinitePoints(const juce::var& curve) {
    int count = 0;
    if (const auto* obj = curve.getDynamicObject())
        if (const auto points = obj->getProperty("points"); points.isArray())
            for (const auto& point : *points.getArray()) {
                const auto* po = point.getDynamicObject();
                if (po != nullptr && morph::isFiniteNumber(po->getProperty("x")) &&
                    morph::isFiniteNumber(po->getProperty("y")))
                    ++count;
            }
    return count;
}
} // namespace

const char* kindWord(ShapeKind kind) { return kind == ShapeKind::Filter ? "filter" : "lfo"; }

std::optional<ShapeKind> kindFromWord(const juce::String& word) {
    if (word == "filter")
        return ShapeKind::Filter;
    if (word == "lfo")
        return ShapeKind::Lfo;
    return std::nullopt;
}

ShapeLibrary::ShapeLibrary(juce::File root)
    : root_(std::move(root)) {}

ShapeLibrary& ShapeLibrary::shared() {
    static auto* instance = new ShapeLibrary();
    return *instance;
}

juce::File ShapeLibrary::defaultRoot() { return synth::userSettingsRootDirectory().getChildFile("Shapes"); }

juce::File ShapeLibrary::root() const { return root_ == juce::File() ? defaultRoot() : root_; }

void ShapeLibrary::setRootForTest(const juce::File& root) { root_ = root; }

juce::File ShapeLibrary::folderFor(ShapeKind kind) const { return root().getChildFile(folderName(kind)); }

juce::String ShapeLibrary::sanitiseName(const juce::String& raw) {
    auto cleaned = juce::File::createLegalFileName(raw.trim()).trim();
    while (cleaned.startsWithChar('.'))
        cleaned = cleaned.substring(1);
    return cleaned.substring(0, kMaxNameLength).trim();
}

juce::File ShapeLibrary::fileFor(ShapeKind kind, const juce::String& name) const {
    const auto safe = sanitiseName(name);
    return safe.isEmpty() ? juce::File() : folderFor(kind).getChildFile(safe + ".json");
}

bool ShapeLibrary::exists(ShapeKind kind, const juce::String& name) const {
    const auto file = fileFor(kind, name);
    return file != juce::File() && file.existsAsFile();
}

std::optional<LibraryShape> ShapeLibrary::loadFile(ShapeKind kind, const juce::File& file) const {
    if (!file.existsAsFile() || file.getSize() > kMaxFileBytes)
        return std::nullopt;
    juce::var parsed;
    if (!juce::JSON::parse(file.loadFileAsString(), parsed).wasOk())
        return std::nullopt;
    const auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return std::nullopt;
    if (obj->hasProperty("version") &&
        (!morph::isFiniteNumber(obj->getProperty("version")) || (int)obj->getProperty("version") > kFileVersion))
        return std::nullopt;
    if (const auto stored = kindFromWord(obj->getProperty("kind").toString()); !stored.has_value() || *stored != kind)
        return std::nullopt;
    const auto curve = obj->getProperty("curve");
    if (!morph::hasReadableCurve(curve) || countFinitePoints(curve) < LfoCustomWave::kMinPoints)
        return std::nullopt;

    LibraryShape shape;
    shape.kind = kind;
    shape.name = sanitiseName(obj->getProperty("name").toString());
    if (shape.name.isEmpty())
        shape.name = sanitiseName(file.getFileNameWithoutExtension());
    if (shape.name.isEmpty())
        return std::nullopt;
    shape.curve = LfoCustomWave::fromVar(curve);
    return shape;
}

std::vector<LibraryShape> ShapeLibrary::userShapes(ShapeKind kind) const {
    std::vector<LibraryShape> out;
    const auto folder = folderFor(kind);
    if (!folder.isDirectory())
        return out;
    auto files = folder.findChildFiles(juce::File::findFiles, false, "*.json");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });
    for (const auto& file : files) {
        if ((int)out.size() >= kMaxShapesPerKind)
            break;
        auto shape = loadFile(kind, file);
        if (!shape.has_value())
            continue;
        const bool repeated = std::any_of(out.begin(), out.end(), [&](const LibraryShape& other) {
            return other.name.equalsIgnoreCase(shape->name);
        });
        if (!repeated)
            out.push_back(std::move(*shape));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const LibraryShape& a, const LibraryShape& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return out;
}

std::vector<LibraryShape> ShapeLibrary::allShapes(ShapeKind kind) const {
    auto out = userShapes(kind);
    for (auto& shape : factoryShapes(kind))
        out.push_back(std::move(shape));
    return out;
}

std::optional<LibraryShape> ShapeLibrary::find(ShapeKind kind, bool factory, const juce::String& name) const {
    if (factory) {
        for (auto& shape : factoryShapes(kind))
            if (shape.name.equalsIgnoreCase(name))
                return std::move(shape);
        return std::nullopt;
    }
    const auto file = fileFor(kind, name);
    return file == juce::File() ? std::nullopt : loadFile(kind, file);
}

ShapeLibrary::SaveResult ShapeLibrary::save(ShapeKind kind, const juce::String& name, const LfoCustomWave& curve,
                                            bool replace) {
    const auto safe = sanitiseName(name);
    if (safe.isEmpty())
        return SaveResult::EmptyName;
    const auto file = fileFor(kind, safe);
    const bool existed = file.existsAsFile();
    if (existed && !replace)
        return SaveResult::Exists;
    if (!file.getParentDirectory().createDirectory().wasOk())
        return SaveResult::Failed;

    auto sane = curve;
    sane.sanitise();
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", kFileVersion);
    obj->setProperty("kind", kindWord(kind));
    obj->setProperty("name", safe);
    obj->setProperty("curve", sane.toVar());
    if (!file.replaceWithText(juce::JSON::toString(juce::var(obj))))
        return SaveResult::Failed;
    listeners_.call([](Listener& l) { l.shapeLibraryChanged(); });
    return existed ? SaveResult::Replaced : SaveResult::Saved;
}

bool ShapeLibrary::remove(ShapeKind kind, const juce::String& name) {
    const auto file = fileFor(kind, name);
    if (file == juce::File() || !file.existsAsFile() || !file.deleteFile())
        return false;
    listeners_.call([](Listener& l) { l.shapeLibraryChanged(); });
    return true;
}

void ShapeLibrary::addListener(Listener* listener) { listeners_.add(listener); }

void ShapeLibrary::removeListener(Listener* listener) { listeners_.remove(listener); }

} // namespace synth::shapes
