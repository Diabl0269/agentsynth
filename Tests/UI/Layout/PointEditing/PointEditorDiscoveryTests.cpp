// PointEditorDiscoveryTests.cpp -- every component in Source/UI that edits points is a row of the point editor
// behaviour matrix (PointEditorBehaviourMatrixTests.cpp) or is named below with the reason it is not one. A new point
// editor that is in neither fails here by name, with the signals that flagged it, so it cannot ship without being
// checked against the shared behaviours.
//
// What is scanned: each header under Source/UI that declares a class deriving directly from juce::Component, read with
// the .cpp files beside it that start with the class name (or its name without "Component"). The class edits points
// when its text shows any of these signals:
//   shared-model   it uses PointSelectionModel, LanePointSelection, PointBoxSelect, PointClipboard or pointKeyAction;
//   point-calls    it calls addBreakpoint, addPointAt or dragBandTo, or hands a double-click on empty space to onAddAt;
//   drag-on-dots   it overrides mouseDrag and hit-tests dots, nodes, bands or points (hitTestNode, hitTestHandle,
//                  hitTestBand, dotAt, nodeAt, bandAt, pointAt).
#include "../../SourceScan.h"
#include "PointEditorMatrixDriver.h"
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>
#include <string>

using synth::test::source_scan::sourceTexts;

namespace {

struct NotAnEditor {
    const char* cls;
    const char* reason;
};

// Components the scan flags that do not edit an ordered or mapped set of points the user adds, moves and removes.
const std::vector<NotAnEditor> kNotPointEditors = {};

std::string stem(const std::string& path) {
    const auto slash = path.find_last_of('/');
    const auto name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    return name.substr(0, name.find('.'));
}

std::string directory(const std::string& path) { return path.substr(0, path.find_last_of('/') + 1); }

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Class name -> the signals its header and units show, for every Source/UI component that edits points.
std::map<std::string, std::set<std::string>> findPointEditors() {
    static const std::regex classHead(R"(\b(?:class|struct)\s+(\w+)\s*(?:final\s*)?([^{;]*)\{)");
    static const std::regex derivesComponent(R"(\bpublic\s+juce::Component\b)");
    static const std::regex sharedModel(
        R"(\b(PointSelectionModel|LanePointSelection|PointBoxSelect|PointClipboard|pointKeyAction)\b)");
    static const std::regex pointCalls(R"(\b(addBreakpoint|addPointAt|dragBandTo|onAddAt)\b)");
    static const std::regex dragOnDots(
        R"(\b(hitTest(?:Node|Handle|Band|Point|Dot)\w*|dotAt|nodeAt|bandAt|pointAt)\s*\()");

    const auto& texts = sourceTexts();
    std::map<std::string, std::set<std::string>> found;
    for (const auto& [file, text] : texts) {
        if (file.rfind("Source/UI/", 0) != 0 || !endsWith(file, ".h"))
            continue;
        for (std::sregex_iterator it(text.begin(), text.end(), classHead), end; it != end; ++it) {
            const std::string cls = (*it)[1];
            const std::string bases = (*it)[2];
            if (!std::regex_search(bases, derivesComponent))
                continue;
            std::string unit = text;
            const auto shortName = endsWith(cls, "Component") ? cls.substr(0, cls.size() - 9) : cls;
            for (const auto& [other, otherText] : texts)
                if (endsWith(other, ".cpp") && directory(other) == directory(file) &&
                    (stem(other).rfind(cls, 0) == 0 || stem(other).rfind(shortName, 0) == 0))
                    unit += otherText;
            std::set<std::string> signals;
            std::smatch m;
            if (std::regex_search(unit, m, sharedModel))
                signals.insert("shared-model:" + m[1].str());
            if (std::regex_search(unit, m, pointCalls))
                signals.insert("point-calls:" + m[1].str());
            if (unit.find("mouseDrag") != std::string::npos && std::regex_search(unit, m, dragOnDots))
                signals.insert("drag-on-dots:" + m[1].str());
            if (!signals.empty())
                found[cls].insert(signals.begin(), signals.end());
        }
    }
    return found;
}

std::string describe(const std::map<std::string, std::set<std::string>>& found) {
    std::string out;
    for (const auto& [cls, signals] : found) {
        out += "\n  " + cls + ":";
        for (const auto& s : signals)
            out += " " + s;
    }
    return out;
}

} // namespace

TEST(PointEditorDiscovery, EveryComponentThatEditsPointsIsAMatrixRowOrNamedNotToBeOne) {
    const auto found = findPointEditors();
    ASSERT_GT(found.size(), 3u) << "the scan found almost nothing: have the signals or the class layout changed?"
                                << describe(found);

    std::set<std::string> rowClasses;
    for (const auto& row : point_matrix::matrixRows())
        if (!row.sourceClass.empty())
            rowClasses.insert(row.sourceClass);

    std::set<std::string> exempt;
    for (const auto& entry : kNotPointEditors) {
        EXPECT_TRUE(exempt.insert(entry.cls).second) << entry.cls << " is listed twice";
        EXPECT_GT(std::string(entry.reason).size(), 12u) << entry.cls << ": say why it is not a point editor";
        EXPECT_EQ(rowClasses.count(entry.cls), 0u) << entry.cls << " is a matrix row and also in kNotPointEditors";
        if (!found.count(entry.cls))
            ADD_FAILURE() << "stale: " << entry.cls
                          << " no longer shows a point editing signal - remove it from kNotPointEditors";
    }
    for (const auto& cls : rowClasses)
        if (!found.count(cls))
            ADD_FAILURE() << "stale: matrix row class " << cls << " shows no point editing signal any more";
    for (const auto& [cls, signals] : found)
        if (!rowClasses.count(cls) && !exempt.count(cls)) {
            std::string why;
            for (const auto& s : signals)
                why += " " + s;
            ADD_FAILURE() << "point editor not in the matrix: " << cls << " (signals:" << why
                          << "). Add a driver row to PointEditorMatrix*Rows.cpp, or name it in kNotPointEditors "
                             "with the reason it edits no points.\nFound:"
                          << describe(found);
        }
}
