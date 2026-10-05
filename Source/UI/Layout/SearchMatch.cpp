#include "SearchMatch.h"

#include <algorithm>

namespace synth::ui {

namespace {

juce::StringArray queryWords(const juce::String& query) {
    juce::StringArray words;
    words.addTokens(query, " \t\r\n", "");
    words.removeEmptyStrings();
    return words;
}

// 0/1/2 rank of the best occurrence of `word` in `text`, or -1 when it does not occur.
int wordRank(const juce::String& text, const juce::String& word) {
    int best = -1;
    int from = 0;
    while (from + word.length() <= text.length()) {
        const int hit = text.indexOfIgnoreCase(from, word);
        if (hit < 0)
            break;
        const int rank = hit == 0 ? 0 : (juce::CharacterFunctions::isLetterOrDigit(text[hit - 1]) ? 2 : 1);
        if (best < 0 || rank < best)
            best = rank;
        if (best == 0)
            break;
        from = hit + 1;
    }
    return best;
}

} // namespace

juce::String moduleSearchAliases(const juce::String& moduleName) {
    auto base = moduleName.trim();
    while (base.isNotEmpty() && juce::CharacterFunctions::isDigit(base.getLastCharacter()))
        base = base.dropLastCharacters(1).trimEnd();
    if (base.equalsIgnoreCase("ADSR") || base.equalsIgnoreCase("Amp Env") || base.equalsIgnoreCase("Filter Env"))
        return "env envelope eg contour";
    if (base.equalsIgnoreCase("Oscillator"))
        return "vco";
    if (base.equalsIgnoreCase("Filter"))
        return "vcf";
    if (base.equalsIgnoreCase("VCA"))
        return "amp amplifier";
    return {};
}

bool searchMatches(const juce::String& text, const juce::String& query) {
    for (const auto& word : queryWords(query))
        if (!text.containsIgnoreCase(word))
            return false;
    return true;
}

int searchScore(const juce::String& text, const juce::String& query) {
    int worst = 0;
    for (const auto& word : queryWords(query)) {
        const int rank = wordRank(text, word);
        if (rank < 0)
            return -1;
        worst = std::max(worst, rank);
    }
    return worst;
}

std::vector<SearchSpan> searchHighlightSpans(const juce::String& text, const juce::String& query) {
    std::vector<SearchSpan> hits;
    if (text.isEmpty())
        return hits;
    for (const auto& word : queryWords(query)) {
        int from = 0;
        while (from + word.length() <= text.length()) {
            const int hit = text.indexOfIgnoreCase(from, word);
            if (hit < 0)
                break;
            hits.push_back({hit, word.length()});
            from = hit + word.length();
        }
    }
    std::sort(hits.begin(), hits.end(), [](const SearchSpan& a, const SearchSpan& b) { return a.start < b.start; });

    std::vector<SearchSpan> merged;
    for (const auto& hit : hits) {
        if (!merged.empty() && hit.start <= merged.back().start + merged.back().length) {
            auto& last = merged.back();
            last.length = std::max(last.length, hit.start + hit.length - last.start);
        } else {
            merged.push_back(hit);
        }
    }
    return merged;
}

void drawSearchHighlightedText(juce::Graphics& g, const juce::String& text, const juce::String& query,
                               juce::Rectangle<int> bounds, const juce::Font& font, juce::Colour normal,
                               juce::Colour highlightFill, juce::Colour highlightText,
                               juce::Justification justification) {
    g.setFont(font);
    const auto spans = searchHighlightSpans(text, query);
    if (spans.empty()) {
        g.setColour(normal);
        g.drawText(text, bounds, justification, true);
        return;
    }

    // Where the (unclipped) run starts, so the fills line up with the glyphs whatever the justification.
    const float totalW = font.getStringWidthFloat(text);
    float baseX = (float)bounds.getX();
    if (justification.testFlags(juce::Justification::right))
        baseX = (float)bounds.getRight() - totalW;
    else if (justification.testFlags(juce::Justification::horizontallyCentred))
        baseX = (float)bounds.getCentreX() - totalW * 0.5f;

    for (const auto& span : spans) {
        const float preW = font.getStringWidthFloat(text.substring(0, span.start));
        const float matchW = font.getStringWidthFloat(text.substring(span.start, span.start + span.length));
        g.setColour(highlightFill);
        g.fillRoundedRectangle(baseX + preW - 1.0f, (float)bounds.getY() + 4.0f, matchW + 2.0f,
                               juce::jmax(8.0f, (float)bounds.getHeight() - 8.0f), 2.0f);
    }

    juce::AttributedString as;
    as.setJustification(justification);
    as.setWordWrap(juce::AttributedString::none);
    int pos = 0;
    for (const auto& span : spans) {
        if (span.start > pos)
            as.append(text.substring(pos, span.start), font, normal);
        as.append(text.substring(span.start, span.start + span.length), font, highlightText);
        pos = span.start + span.length;
    }
    if (pos < text.length())
        as.append(text.substring(pos), font, normal);
    as.draw(g, bounds.toFloat());
}

} // namespace synth::ui
