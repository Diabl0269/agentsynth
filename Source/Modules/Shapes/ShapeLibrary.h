#pragma once

// ShapeLibrary.h -- the named shape curves the Sounds view lists and the shape windows stamp: the factory set compiled
// into the app and the user's saved shapes, one JSON file each. A shape here is the breakpoint curve both shape
// editors already use (LfoCustomWave), a name and the kind of module it is for. Message thread only.
#include "Modules/Lfo/LfoCustomWave.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace synth::shapes {

/** Which editor a shape is for: a Filter shape is a magnitude curve over log frequency, an LFO shape one cycle. */
enum class ShapeKind { Filter, Lfo };

/** "filter" or "lfo": the spelling the files, the drag payload and the settings use. */
const char* kindWord(ShapeKind kind);
/** The kind a word names; none for any other text. */
std::optional<ShapeKind> kindFromWord(const juce::String& word);

/** The id the drag payload, Favourites and Used recently use for a shape: "shape:<filter|lfo>:<factory|user>:<name>".
 */
juce::String shapeId(ShapeKind kind, bool factory, const juce::String& name);

struct LibraryShape {
    ShapeKind kind = ShapeKind::Filter;
    juce::String name;
    LfoCustomWave curve;
    bool factory = false; ///< Built into the app; never written or removed.
};

class ShapeLibrary {
public:
    class Listener {
    public:
        virtual ~Listener() = default;
        /** A shape was saved or removed. */
        virtual void shapeLibraryChanged() = 0;
        /** A shape was stamped onto a chip or a card (by drag, Start from or Return). */
        virtual void shapeLibraryUsed(const LibraryShape&) {}
    };

    static constexpr int kMaxNameLength = 24;
    static constexpr int kMaxShapesPerKind = 500;
    static constexpr juce::int64 kMaxFileBytes = 64 * 1024;

    /** Keeps shapes under `root` (`Filter/` and `LFO/` inside it); an empty `root` means the default folder. */
    explicit ShapeLibrary(juce::File root = {});

    /** The library the app uses; never destroyed, so a component can remove itself from it at process exit. */
    static ShapeLibrary& shared();
    /** `<user settings folder>/Shapes`. */
    static juce::File defaultRoot();

    /** The file a saved shape of that name lives in (it may not exist); empty when the name is unusable. */
    juce::File fileOf(ShapeKind kind, const juce::String& name) const { return fileFor(kind, name); }
    /** Where this library keeps its files. Not created until the first save. */
    juce::File root() const;
    /** Tests only: keeps this library's files under `root` (empty restores the default folder). */
    void setRootForTest(const juce::File& root);

    /** The shapes built into the app, in the order the Sounds view lists them: 8 for each kind. */
    static std::vector<LibraryShape> factoryShapes(ShapeKind kind);
    /** The user's saved shapes, by name; a file that cannot be trusted is left out. */
    std::vector<LibraryShape> userShapes(ShapeKind kind) const;
    /** The user's shapes, then the factory set. */
    std::vector<LibraryShape> allShapes(ShapeKind kind) const;
    /** One shape by name; a factory name is matched ignoring case. */
    std::optional<LibraryShape> find(ShapeKind kind, bool factory, const juce::String& name) const;

    /** A typed name made safe as a file name, trimmed and cut to kMaxNameLength; empty when nothing usable is left. */
    static juce::String sanitiseName(const juce::String& raw);
    /** Whether a saved shape of that name is already there (file names compare ignoring case, as the disk does). */
    bool exists(ShapeKind kind, const juce::String& name) const;

    enum class SaveResult { Saved, Replaced, Exists, EmptyName, Failed };
    /** Writes the curve under `name`. An existing name is left alone and reported as Exists unless `replace`. */
    SaveResult save(ShapeKind kind, const juce::String& name, const LfoCustomWave& curve, bool replace = false);
    /** Deletes a saved shape; false when there was none. */
    bool remove(ShapeKind kind, const juce::String& name);

    enum class RenameResult { Renamed, Exists, EmptyName, Missing, Failed };
    /** Gives the saved shape `from` the name `to`. A name already taken by another shape is left alone (Exists); a
     *  change of letter case only is a rename. */
    RenameResult rename(ShapeKind kind, const juce::String& from, const juce::String& to);
    /** Tells the listeners `shape` was stamped (it changes nothing on disk). */
    void noteUsed(const LibraryShape& shape);

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    juce::File folderFor(ShapeKind kind) const;
    juce::File fileFor(ShapeKind kind, const juce::String& name) const;
    std::optional<LibraryShape> loadFile(ShapeKind kind, const juce::File& file) const;

    juce::File root_;
    juce::ListenerList<Listener> listeners_;
};

} // namespace synth::shapes
