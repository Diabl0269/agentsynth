// SourceScan.h -- reading Source/ from a test, for the discovery ("registration") tests: the theme sweep's dialog
// call sites, the drag surfaces and the popup openers. A new instance that no registry knows about fails by name.
#pragma once

#include "TestPaths.h"
#include <gtest/gtest.h>
#include <iterator>
#include <juce_core/juce_core.h>
#include <map>
#include <regex>
#include <set>
#include <string>

namespace synth::test::source_scan {

inline juce::File sourceDir() { return juce::File(TESTS_ROOT_DIR).getParentDirectory().getChildFile("Source"); }

/** `text` without its comments (string literals are left alone). */
inline std::string stripComments(const std::string& text) {
    std::string out;
    bool inString = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            out += c;
            if (c == '\\' && i + 1 < text.size())
                out += text[++i];
            else if (c == '"')
                inString = false;
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n')
                ++i;
            out += '\n';
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'))
                ++i;
            ++i;
        } else {
            out += c;
            inString = c == '"';
        }
    }
    return out;
}

/** Every Source file (repo-relative path, e.g. "Source/UI/X.h") with its comment-free text. */
inline std::map<std::string, std::string> sourceTexts() {
    std::map<std::string, std::string> texts;
    juce::Array<juce::File> files;
    sourceDir().findChildFiles(files, juce::File::findFiles, true, "*.cpp;*.h;*.mm");
    for (const auto& file : files)
        texts["Source/" + file.getRelativePathFrom(sourceDir()).replaceCharacter('\\', '/').toStdString()] =
            stripComments(file.loadFileAsString().toStdString());
    return texts;
}

/** Occurrences of `pattern` per Source file (repo-relative path), comments excluded. */
inline std::map<std::string, int> countCallSites(const std::regex& pattern, const std::string& skipPrefix) {
    std::map<std::string, int> counts;
    for (const auto& [relative, text] : sourceTexts()) {
        if (!skipPrefix.empty() && relative.rfind(skipPrefix, 0) == 0)
            continue;
        const auto n =
            (int)std::distance(std::sregex_iterator(text.begin(), text.end(), pattern), std::sregex_iterator());
        if (n > 0)
            counts[relative] = n;
    }
    return counts;
}

/** Fails for every file whose scanned count differs from the registered one, naming the file. */
inline void expectCountsMatch(const char* what, const std::map<std::string, int>& scanned,
                              const std::map<std::string, int>& registered, const char* registry) {
    std::set<std::string> files;
    for (const auto& [file, n] : scanned)
        files.insert(file);
    for (const auto& [file, n] : registered)
        files.insert(file);
    for (const auto& file : files) {
        const int found = scanned.count(file) ? scanned.at(file) : 0;
        const int listed = registered.count(file) ? registered.at(file) : 0;
        if (found > listed)
            ADD_FAILURE() << "not registered: " << file << " (" << found << " " << what << " call sites, " << listed
                          << " registered) - add it to " << registry;
        else if (found < listed)
            ADD_FAILURE() << "stale: " << file << " has " << found << " " << what << " call sites but " << listed
                          << " are registered - remove the extra from " << registry;
    }
}

/** The class or struct whose body contains `pos`, found by walking back out of the braces; "" when there is none. */
inline std::string enclosingClass(const std::string& text, size_t pos) {
    static const std::regex head(R"(\b(?:class|struct)\s+(\w+))");
    int depth = 0;
    for (size_t i = pos; i-- > 0;) {
        if (text[i] == '}') {
            ++depth;
        } else if (text[i] == '{' && depth-- == 0) {
            const size_t from = text.find_last_of(";}{", i == 0 ? 0 : i - 1);
            const auto declaration = text.substr(from == std::string::npos ? 0 : from + 1, i - (from + 1));
            for (std::sregex_iterator it(declaration.begin(), declaration.end(), head), end; it != end; ++it) {
                const auto before = declaration.find_last_not_of(" \t\n", (size_t)it->position(0) - 1);
                const bool templateArg =
                    before != std::string::npos && (declaration[before] == '<' || declaration[before] == ',');
                const bool enumClass =
                    before != std::string::npos && before >= 3 && declaration.compare(before - 3, 4, "enum") == 0;
                if (!templateArg && !enumClass)
                    return (*it)[1];
            }
            depth = 0;
        }
    }
    return "";
}

/** Every class that overrides mouseDrag(const juce::MouseEvent&), with the file that declares it. */
inline std::map<std::string, std::string> classesOverridingMouseDrag() {
    static const std::regex pattern(R"(void\s+mouseDrag\s*\(\s*const\s+juce::MouseEvent\s*&[^)]*\)\s*override)");
    std::map<std::string, std::string> classes;
    for (const auto& [file, text] : sourceTexts()) {
        if (text.find("mouseDrag") == std::string::npos)
            continue;
        for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
            const auto cls = enclosingClass(text, (size_t)it->position(0));
            classes[cls.empty() ? "?" + file : cls] = file;
        }
    }
    return classes;
}

} // namespace synth::test::source_scan
