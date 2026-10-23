// The library of named shape curves: what the user's saved files round-trip, how a bad file is treated, and the
// factory set the app ships.
#include "Modules/Filter/CustomFilterBlend.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "Modules/Shapes/ShapeLibrary.h"
#include <cmath>
#include <gtest/gtest.h>

using synth::CustomFilterShapes;
using synth::LfoCustomWave;
using synth::shapes::ShapeKind;
using synth::shapes::ShapeLibrary;

namespace {

struct TempLibrary {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("shape-library-" + juce::Uuid().toDashedString());
    ShapeLibrary library{dir};
    ~TempLibrary() { dir.deleteRecursively(); }

    juce::File fileOf(ShapeKind kind, const juce::String& stem) const {
        return dir.getChildFile(kind == ShapeKind::Filter ? "Filter" : "LFO").getChildFile(stem + ".json");
    }
    void writeRaw(ShapeKind kind, const juce::String& stem, const juce::String& text) {
        fileOf(kind, stem).getParentDirectory().createDirectory();
        fileOf(kind, stem).replaceWithText(text);
    }
};

LfoCustomWave sampleCurve() {
    LfoCustomWave wave;
    wave.points = {{0.0f, 0.2f, 0.0f}, {0.4f, 0.9f, 0.3f}, {1.0f, 0.1f, 0.0f}};
    return wave;
}

float dbAtHz(const LfoCustomWave& curve, float hz) {
    return CustomFilterShapes::yToDb(curve.evaluate(CustomFilterShapes::hzToX(hz)));
}

} // namespace

TEST(ShapeLibraryTest, ASavedShapeLoadsBackWithTheSameNameKindAndCurve) {
    TempLibrary t;
    EXPECT_EQ(t.library.save(ShapeKind::Filter, "my vowel", sampleCurve()), ShapeLibrary::SaveResult::Saved);
    EXPECT_EQ(t.library.save(ShapeKind::Lfo, "my pulse", sampleCurve()), ShapeLibrary::SaveResult::Saved);

    const auto filter = t.library.userShapes(ShapeKind::Filter);
    ASSERT_EQ(filter.size(), 1u);
    EXPECT_EQ(filter[0].name, "my vowel");
    EXPECT_EQ(filter[0].kind, ShapeKind::Filter);
    EXPECT_FALSE(filter[0].factory);
    EXPECT_EQ(filter[0].curve, sampleCurve());
    // Each kind lists only its own folder.
    ASSERT_EQ(t.library.userShapes(ShapeKind::Lfo).size(), 1u);
    EXPECT_EQ(t.library.userShapes(ShapeKind::Lfo)[0].name, "my pulse");
    EXPECT_TRUE(t.fileOf(ShapeKind::Filter, "my vowel").existsAsFile());
}

TEST(ShapeLibraryTest, SavingOverAnExistingNameAsksFirstAndThenReplacesIt) {
    TempLibrary t;
    ASSERT_EQ(t.library.save(ShapeKind::Filter, "ring", sampleCurve()), ShapeLibrary::SaveResult::Saved);
    auto other = LfoCustomWave::preset(LfoCustomWave::Preset::RampUp);
    EXPECT_EQ(t.library.save(ShapeKind::Filter, "ring", other), ShapeLibrary::SaveResult::Exists);
    EXPECT_EQ(t.library.userShapes(ShapeKind::Filter)[0].curve, sampleCurve()); // not touched
    // The disk ignores case, so a differently cased name is the same shape.
    EXPECT_TRUE(t.library.exists(ShapeKind::Filter, "RING"));
    EXPECT_EQ(t.library.save(ShapeKind::Filter, "ring", other, true), ShapeLibrary::SaveResult::Replaced);
    ASSERT_EQ(t.library.userShapes(ShapeKind::Filter).size(), 1u);
    EXPECT_EQ(t.library.userShapes(ShapeKind::Filter)[0].curve, other);
}

TEST(ShapeLibraryTest, AnEmptyOrUnusableNameIsRefusedAndWritesNothing) {
    TempLibrary t;
    EXPECT_EQ(t.library.save(ShapeKind::Filter, "", sampleCurve()), ShapeLibrary::SaveResult::EmptyName);
    EXPECT_EQ(t.library.save(ShapeKind::Filter, "   ", sampleCurve()), ShapeLibrary::SaveResult::EmptyName);
    EXPECT_EQ(t.library.save(ShapeKind::Filter, "...", sampleCurve()), ShapeLibrary::SaveResult::EmptyName);
    EXPECT_FALSE(t.dir.exists());
    // A name that tries to leave the folder is cut down to a plain file name.
    EXPECT_FALSE(ShapeLibrary::sanitiseName("../../etc/passwd").contains("/"));
    ASSERT_EQ(t.library.save(ShapeKind::Filter, "../evil", sampleCurve()), ShapeLibrary::SaveResult::Saved);
    EXPECT_FALSE(t.dir.getParentDirectory().getChildFile("evil.json").exists());
    ASSERT_EQ(t.library.userShapes(ShapeKind::Filter).size(), 1u);
}

TEST(ShapeLibraryTest, ANameIsCutToTheLimit) {
    EXPECT_EQ(ShapeLibrary::sanitiseName(juce::String::repeatedString("a", 80)).length(), ShapeLibrary::kMaxNameLength);
}

TEST(ShapeLibraryTest, ABadOrCorruptFileIsSkippedWithoutHidingTheGoodOnes) {
    TempLibrary t;
    ASSERT_EQ(t.library.save(ShapeKind::Filter, "good", sampleCurve()), ShapeLibrary::SaveResult::Saved);
    t.writeRaw(ShapeKind::Filter, "garbage", "this is {not json");
    t.writeRaw(ShapeKind::Filter, "empty", "");
    t.writeRaw(ShapeKind::Filter, "array", "[1,2,3]");
    t.writeRaw(ShapeKind::Filter, "nocurve", R"({"version":1,"kind":"filter","name":"nocurve"})");
    t.writeRaw(ShapeKind::Filter, "onepoint",
               R"({"version":1,"kind":"filter","name":"onepoint","curve":{"points":[{"x":0,"y":0.5}]}})");
    t.writeRaw(ShapeKind::Filter, "wrongkind",
               R"({"version":1,"kind":"lfo","name":"wrongkind","curve":{"points":[{"x":0,"y":0},{"x":1,"y":1}]}})");
    t.writeRaw(ShapeKind::Filter, "newer",
               R"({"version":9,"kind":"filter","name":"newer","curve":{"points":[{"x":0,"y":0},{"x":1,"y":1}]}})");
    t.writeRaw(ShapeKind::Filter, "huge", juce::String::repeatedString(" ", (int)ShapeLibrary::kMaxFileBytes + 10));

    const auto shapes = t.library.userShapes(ShapeKind::Filter);
    ASSERT_EQ(shapes.size(), 1u);
    EXPECT_EQ(shapes[0].name, "good");
    EXPECT_FALSE(t.library.find(ShapeKind::Filter, false, "garbage").has_value());
}

TEST(ShapeLibraryTest, ValuesReadFromDiskAreClampedAndNeverNonFinite) {
    TempLibrary t;
    t.writeRaw(ShapeKind::Lfo, "wild", R"({"version":1,"kind":"lfo","name":"wild","curve":{"version":1,"points":[
        {"x":-4,"y":9,"bend":7},{"x":0.5,"y":1e999,"bend":0},{"x":0.6,"y":-1e999,"bend":0},
        {"x":0.7,"y":0.5,"bend":1e999},{"x":3,"y":-2,"bend":2}]}})");
    const auto shape = t.library.find(ShapeKind::Lfo, false, "wild");
    ASSERT_TRUE(shape.has_value());
    ASSERT_GE(shape->curve.points.size(), 2u);
    for (const auto& p : shape->curve.points) {
        EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.bend));
        EXPECT_GE(p.x, 0.0f);
        EXPECT_LE(p.x, 1.0f);
        EXPECT_GE(p.y, 0.0f);
        EXPECT_LE(p.y, 1.0f);
        EXPECT_GE(p.bend, -1.0f);
        EXPECT_LE(p.bend, 1.0f);
    }
    EXPECT_EQ(shape->curve.points.front().x, 0.0f);
    EXPECT_EQ(shape->curve.points.back().x, 1.0f);
}

TEST(ShapeLibraryTest, ATooLongPointListIsCutToTheCurveLimit) {
    TempLibrary t;
    juce::String points;
    for (int i = 0; i < 400; ++i)
        points += (i ? "," : "") + juce::String("{\"x\":") + juce::String(i / 400.0) + ",\"y\":0.5}";
    t.writeRaw(ShapeKind::Filter, "long",
               R"({"version":1,"kind":"filter","name":"long","curve":{"points":[)" + points + "]}}");
    const auto shape = t.library.find(ShapeKind::Filter, false, "long");
    ASSERT_TRUE(shape.has_value());
    EXPECT_LE((int)shape->curve.points.size(), LfoCustomWave::kMaxPoints);
}

TEST(ShapeLibraryTest, UserShapesAreListedBeforeTheFactorySetAndRemoveForgetsOne) {
    TempLibrary t;
    ASSERT_EQ(t.library.save(ShapeKind::Lfo, "zebra", sampleCurve()), ShapeLibrary::SaveResult::Saved);
    ASSERT_EQ(t.library.save(ShapeKind::Lfo, "alpha", sampleCurve()), ShapeLibrary::SaveResult::Saved);
    const auto all = t.library.allShapes(ShapeKind::Lfo);
    ASSERT_EQ(all.size(), 10u);
    EXPECT_EQ(all[0].name, "alpha");
    EXPECT_EQ(all[1].name, "zebra");
    EXPECT_TRUE(all[2].factory);
    EXPECT_TRUE(t.library.remove(ShapeKind::Lfo, "alpha"));
    EXPECT_FALSE(t.library.remove(ShapeKind::Lfo, "alpha"));
    EXPECT_EQ(t.library.userShapes(ShapeKind::Lfo).size(), 1u);
}

TEST(ShapeLibraryTest, ListenersHearASaveAndARemove) {
    struct Counter : ShapeLibrary::Listener {
        int calls = 0;
        void shapeLibraryChanged() override { ++calls; }
    } counter;
    TempLibrary t;
    t.library.addListener(&counter);
    t.library.save(ShapeKind::Filter, "a", sampleCurve());
    t.library.save(ShapeKind::Filter, "a", sampleCurve()); // refused: no change
    t.library.remove(ShapeKind::Filter, "a");
    t.library.removeListener(&counter);
    EXPECT_EQ(counter.calls, 2);
}

TEST(ShapeFactoryTest, EachKindShipsEightShapesWithTheRightNamesAndKinds) {
    const auto filter = ShapeLibrary::factoryShapes(ShapeKind::Filter);
    const auto lfo = ShapeLibrary::factoryShapes(ShapeKind::Lfo);
    const juce::StringArray filterNames{"ah", "ee", "oo", "eh", "ih", "telephone band", "comb", "tilt"};
    const juce::StringArray lfoNames{"ramp up", "ramp down",        "exponential decay", "sidechain duck",
                                     "bounce",  "4-step staircase", "smooth random",     "1/16 gate"};
    ASSERT_EQ(filter.size(), 8u);
    ASSERT_EQ(lfo.size(), 8u);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(filter[i].name, filterNames[(int)i]);
        EXPECT_EQ(filter[i].kind, ShapeKind::Filter);
        EXPECT_TRUE(filter[i].factory);
        EXPECT_EQ(lfo[i].name, lfoNames[(int)i]);
        EXPECT_EQ(lfo[i].kind, ShapeKind::Lfo);
        EXPECT_TRUE(lfo[i].factory);
    }
}

TEST(ShapeFactoryTest, TheVowelsAreTheFilterPresetCurvesNotCopies) {
    const auto filter = ShapeLibrary::factoryShapes(ShapeKind::Filter);
    using P = CustomFilterShapes::Preset;
    const P presets[] = {P::Ah, P::Ee, P::Oo, P::Eh, P::Ih};
    for (size_t i = 0; i < 5; ++i)
        EXPECT_EQ(filter[i].curve, CustomFilterShapes::presetCurve(presets[i]));
}

TEST(ShapeFactoryTest, EveryFilterShapeStaysUnderTheGainCeilingAndIsAValidCurve) {
    for (const auto& shape : ShapeLibrary::factoryShapes(ShapeKind::Filter)) {
        SCOPED_TRACE(shape.name.toStdString());
        float peakDb = -1000.0f;
        for (const auto& p : shape.curve.points) {
            EXPECT_GE(p.y, 0.0f);
            EXPECT_LE(p.y, 1.0f);
            peakDb = std::max(peakDb, CustomFilterShapes::yToDb(p.y));
        }
        EXPECT_LE(peakDb, 6.0f);
        EXPECT_LE(peakDb, 0.0f + 2.5f); // and at most the vowels' small boost
        EXPECT_GE((int)shape.curve.points.size(), LfoCustomWave::kMinPoints);
        EXPECT_LE((int)shape.curve.points.size(), LfoCustomWave::kMaxPoints);
        auto again = shape.curve;
        again.sanitise();
        EXPECT_EQ(again, shape.curve);
    }
}

TEST(ShapeFactoryTest, TheTelephoneBandPassesItsBandAndIsDownAtOneHundredHertzAndEightKilohertz) {
    const auto shapes = ShapeLibrary::factoryShapes(ShapeKind::Filter);
    const auto& phone = shapes[5];
    ASSERT_EQ(phone.name, "telephone band");
    EXPECT_NEAR(dbAtHz(phone.curve, 1000.0f), 0.0f, 0.5f);
    EXPECT_NEAR(dbAtHz(phone.curve, 300.0f), -3.0f, 0.5f);
    EXPECT_NEAR(dbAtHz(phone.curve, 3400.0f), -3.0f, 0.5f);
    EXPECT_LT(dbAtHz(phone.curve, 100.0f), -20.0f);
    EXPECT_LT(dbAtHz(phone.curve, 8000.0f), -20.0f);

    // The same reading the card's response view and the kernel design use.
    synth::CustomFilterShapes list;
    list.shapes.push_back({"phone", phone.curve, {0.5f, 0.5f}});
    const auto grid = synth::customfilter::responseGridDb(list, 0.5f, 0.5f, 0.0f, 1.0f);
    const auto at = [&](float hz) {
        return grid[(size_t)std::lround(CustomFilterShapes::hzToX(hz) * (float)(grid.size() - 1))];
    };
    EXPECT_LT(at(100.0f), at(1000.0f) - 20.0f);
    EXPECT_LT(at(8000.0f), at(1000.0f) - 20.0f);
}

TEST(ShapeFactoryTest, TheCombHasEvenlySpacedNotchesAndTheTiltSlopesDown) {
    const auto shapes = ShapeLibrary::factoryShapes(ShapeKind::Filter);
    const auto& comb = shapes[6].curve;
    for (float hz : {2000.0f, 4000.0f, 8000.0f, 12000.0f})
        EXPECT_LT(dbAtHz(comb, hz), -20.0f);
    for (float hz : {2500.0f, 4500.0f, 8500.0f})
        EXPECT_GT(dbAtHz(comb, hz), -3.0f);
    const auto& tilt = shapes[7].curve;
    EXPECT_GT(dbAtHz(tilt, 50.0f), dbAtHz(tilt, 1000.0f));
    EXPECT_GT(dbAtHz(tilt, 1000.0f), dbAtHz(tilt, 15000.0f));
}

TEST(ShapeFactoryTest, TheLfoShapesDoWhatTheirNamesSay) {
    const auto lfo = ShapeLibrary::factoryShapes(ShapeKind::Lfo);
    const auto at = [&](size_t i, float phase) { return lfo[i].curve.evaluate(phase); };
    EXPECT_LT(at(0, 0.1f), at(0, 0.9f));                             // ramp up
    EXPECT_GT(at(1, 0.1f), at(1, 0.9f));                             // ramp down
    EXPECT_NEAR(at(2, 0.0f), 1.0f, 0.01f);                           // exponential decay starts at the top
    EXPECT_GT(at(2, 0.1f) - at(2, 0.2f), at(2, 0.8f) - at(2, 0.9f)); // and falls fast, then slowly
    EXPECT_NEAR(at(3, 0.0f), 0.0f, 0.01f);                           // sidechain duck: ducked at the start
    EXPECT_NEAR(at(3, 0.9f), 1.0f, 0.01f);                           // and back up
    EXPECT_NEAR(at(4, 0.25f), 1.0f, 0.02f);                          // bounce: the first arc peaks
    EXPECT_LT(at(4, 0.65f), 0.6f);                                   // and the next one is lower
    EXPECT_NEAR(at(5, 0.1f), 0.0f, 0.01f);                           // staircase: four levels
    EXPECT_NEAR(at(5, 0.3f), 1.0f / 3.0f, 0.01f);
    EXPECT_NEAR(at(5, 0.6f), 2.0f / 3.0f, 0.01f);
    EXPECT_NEAR(at(5, 0.9f), 1.0f, 0.01f);
    EXPECT_NEAR(at(6, 0.0f), lfo[6].curve.evaluate(0.99999f), 0.02f); // smooth random meets itself
    // 1/16 gate: on for the first half of each sixteenth, off for the second.
    for (int cell = 0; cell < 16; ++cell) {
        EXPECT_EQ(at(7, ((float)cell + 0.25f) / 16.0f), 1.0f) << cell;
        EXPECT_EQ(at(7, ((float)cell + 0.75f) / 16.0f), 0.0f) << cell;
    }
}

TEST(ShapeFactoryTest, AFactoryShapeIsFoundByNameIgnoringCase) {
    ShapeLibrary library{juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("no-such-shapes")};
    const auto found = library.find(ShapeKind::Filter, true, "Telephone Band");
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->factory);
    EXPECT_FALSE(library.find(ShapeKind::Lfo, true, "tilt").has_value());
}

TEST(ShapeLibraryTest, RenameMovesTheFileRefusesATakenNameAndAllowsACaseChange) {
    TempLibrary t;
    t.library.save(ShapeKind::Filter, "one", sampleCurve());
    t.library.save(ShapeKind::Filter, "two", sampleCurve());
    using R = ShapeLibrary::RenameResult;
    int heard = 0;
    struct L : ShapeLibrary::Listener {
        int* n;
        void shapeLibraryChanged() override { ++*n; }
    } listener;
    listener.n = &heard;
    t.library.addListener(&listener);

    EXPECT_EQ(t.library.rename(ShapeKind::Filter, "one", "two"), R::Exists);
    EXPECT_EQ(t.library.rename(ShapeKind::Filter, "one", "  "), R::EmptyName);
    EXPECT_EQ(t.library.rename(ShapeKind::Filter, "nope", "x"), R::Missing);
    EXPECT_EQ(heard, 0);
    EXPECT_EQ(t.library.rename(ShapeKind::Filter, "one", "three"), R::Renamed);
    EXPECT_FALSE(t.library.exists(ShapeKind::Filter, "one"));
    ASSERT_TRUE(t.library.find(ShapeKind::Filter, false, "three").has_value());
    EXPECT_EQ(t.library.find(ShapeKind::Filter, false, "three")->curve, sampleCurve());
    EXPECT_EQ(heard, 1);
    EXPECT_EQ(t.library.rename(ShapeKind::Filter, "three", "Three"), R::Renamed);
    EXPECT_EQ(t.library.userShapes(ShapeKind::Filter).size(), 2u);
    t.library.removeListener(&listener);
}

TEST(ShapeLibraryTest, ShapeIdIsTheDragPayloadSpelling) {
    EXPECT_EQ(synth::shapes::shapeId(ShapeKind::Lfo, true, "ramp up"), "shape:lfo:factory:ramp up");
    EXPECT_EQ(synth::shapes::shapeId(ShapeKind::Filter, false, "mine"), "shape:filter:user:mine");
}
