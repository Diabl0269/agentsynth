#pragma once

#include "TrackColour.h"
#include <algorithm>
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
// juce::ColourSelector lives in juce_gui_extra, not juce_gui_basics — without this include the
// class fails to parse and every juce::Component base call below cascades into bogus errors.
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>

// ColourPickerPopup — a full colour picker (juce::ColourSelector + a favourites shelf) used
// anywhere a single juce::Colour needs to be chosen with a live preview: the track-header colour
// swatch (TimelineTrackHeaderComponent) and the Appearance tab's piano-roll note swatches
// (AppearanceSettingsTab). Modeled on AppearanceSettingsTab::openCableColourPicker's existing
// CallOutBox pattern, generalised into its own component so both callers share one popup rather
// than two near-identical ad hoc ColourSelectors.
//
// Favourites persist across sessions via a caller-owned juce::PropertiesFile (nullptr means
// "in-memory only for this popup instance", which is what keeps this class usable from a headless
// test with no ApplicationProperties at all).
namespace synth::ui {

//==============================================================================
// Favourites persistence — free functions, pure except for the PropertiesFile I/O at the edges,
// so the round-trip and junk-tolerance rules have a test surface with no juce::Component involved.
//==============================================================================

/** The ApplicationProperties key every reader/writer of the favourites list agrees on. */
inline const char* favouriteColoursKey() noexcept { return "favouriteColoursArgb"; }

/** Comma-separated AARRGGBB hex (e.g. "FF4FC1FF,FF7FD962"). A malformed token (wrong length, a
 *  non-hex character, an empty slot from a stray comma) is dropped rather than corrupting the
 *  whole list — one bad entry must not lose every other favourite. */
inline juce::String serializeFavouriteColours(const std::vector<juce::Colour>& colours) {
    juce::StringArray tokens;
    for (const auto& c : colours)
        tokens.add(juce::String::toHexString((juce::int64)c.getARGB()).paddedLeft('0', 8).toUpperCase());
    return tokens.joinIntoString(",");
}

inline std::vector<juce::Colour> parseFavouriteColours(const juce::String& raw) {
    std::vector<juce::Colour> out;
    if (raw.isEmpty())
        return out;

    const auto tokens = juce::StringArray::fromTokens(raw, ",", "");
    for (const auto& rawToken : tokens) {
        auto token = rawToken.trim();
        if (token.startsWithIgnoreCase("0x"))
            token = token.substring(2);
        if (token.length() != 8 || !token.containsOnly("0123456789abcdefABCDEF"))
            continue; // junk entry — skip it, keep the rest of the list intact
        const auto argb = (juce::uint32)token.getHexValue64();
        out.push_back(juce::Colour(argb));
    }
    return out;
}

/** Adds `colour` if it is not already present (by exact ARGB match); preserves the existing
 *  order and appends the new entry at the end — "most recently added" reads left-to-right. */
inline void addFavourite(std::vector<juce::Colour>& colours, juce::Colour colour) {
    for (const auto& existing : colours)
        if (existing.getARGB() == colour.getARGB())
            return;
    colours.push_back(colour);
}

/** Removes every entry matching `colour` (there should only ever be one, given addFavourite
 *  dedupes on the way in); preserves the order of what remains. */
inline void removeFavourite(std::vector<juce::Colour>& colours, juce::Colour colour) {
    colours.erase(std::remove_if(colours.begin(), colours.end(),
                                 [colour](const juce::Colour& c) { return c.getARGB() == colour.getARGB(); }),
                 colours.end());
}

/** Loads the persisted favourites, or — when the key is absent (first run) — seeds the default
 *  set from the track palette so the shelf is never empty on a fresh install. A `props` of
 *  nullptr (in-memory-only caller) also seeds the defaults, since there is nothing to load from. */
inline std::vector<juce::Colour> loadFavouriteColours(juce::PropertiesFile* props) {
    if (props != nullptr && props->containsKey(favouriteColoursKey()))
        return parseFavouriteColours(props->getValue(favouriteColoursKey()));

    std::vector<juce::Colour> defaults;
    for (const auto argb : trackPaletteArgb())
        defaults.push_back(juce::Colour(argb));
    return defaults;
}

inline void saveFavouriteColours(juce::PropertiesFile* props, const std::vector<juce::Colour>& colours) {
    if (props == nullptr)
        return;
    props->setValue(favouriteColoursKey(), serializeFavouriteColours(colours));
    props->saveIfNeeded();
}

//==============================================================================
// ColourPickerPopup
//==============================================================================
class ColourPickerPopup
    : public juce::Component
    , private juce::ChangeListener {
public:
    ColourPickerPopup(juce::Colour initial, juce::PropertiesFile* props, std::function<void(juce::Colour)> onPreview,
                      std::function<void(juce::Colour)> onCommit)
        : props_(props)
        , favourites_(loadFavouriteColours(props))
        , onPreview_(std::move(onPreview))
        , onCommit_(std::move(onCommit))
        , lastColour_(initial) {
        setComponentID("colourPickerPopup");

        addAndMakeVisible(selector_);
        selector_.setCurrentColour(initial, juce::dontSendNotification);
        selector_.addChangeListener(this);

        addAndMakeVisible(addFavouriteButton_);
        addFavouriteButton_.setComponentID("colourPickerAddFavourite");
        addFavouriteButton_.setButtonText(juce::CharPointer_UTF8("\xE2\x98\x85")); // filled star
        addFavouriteButton_.setTooltip("Add the current colour to favourites");
        addFavouriteButton_.onClick = [this] {
            addFavourite(favourites_, lastColour_);
            saveFavouriteColours(props_, favourites_);
            rebuildFavouriteButtons();
        };

        rebuildFavouriteButtons();
        setSize(280, 400);
    }

    ~ColourPickerPopup() override {
        selector_.removeChangeListener(this);
        commitOnce(); // the callout closing (or this component being torn down any other way)
                      // is the ONE close event — fire the deferred commit here if it never fired.
    }

    void resized() override {
        auto bounds = getLocalBounds();
        selector_.setBounds(bounds.removeFromTop(bounds.getHeight() - kFavouritesAreaHeight));

        auto favArea = bounds;
        addFavouriteButton_.setBounds(favArea.removeFromLeft(kSwatchSize).reduced(2));
        layoutFavouriteButtons(favArea);
    }

    // ---- Test seams (no CallOutBox involved) -----------------------------------------------
    juce::Colour getCurrentColourForTest() const { return lastColour_; }
    // dontSendNotification + a direct preview call, NOT sendNotificationSync: with sliders shown,
    // ColourSelector's sync path re-enters itself from the FIRST slider's callback and rebuilds
    // the colour from the three still-stale sliders (red lands, green/blue keep their old values).
    // The production path never hits this because a user drags one slider at a time.
    void setCurrentColourForTest(juce::Colour c) {
        selector_.setCurrentColour(c, juce::dontSendNotification);
        previewNow(c);
    }
    int getFavouriteCountForTest() const { return (int)favourites_.size(); }
    juce::Colour getFavouriteColourForTest(int index) const {
        return (index >= 0 && index < (int)favourites_.size()) ? favourites_[(size_t)index] : juce::Colours::transparentBlack;
    }
    void clickFavouriteForTest(int index) {
        if (index >= 0 && index < (int)favouriteButtons_.size())
            favouriteButtons_[(size_t)index]->onClick();
    }
    void removeFavouriteForTest(int index) {
        if (index < 0 || index >= (int)favourites_.size())
            return;
        removeFavourite(favourites_, favourites_[(size_t)index]);
        saveFavouriteColours(props_, favourites_);
        rebuildFavouriteButtons();
    }
    /** Fires onCommit exactly once with the current colour — the same thing destruction does,
     *  exposed so a test can close the popup without actually destroying the juce::Component. */
    void commitForTest() { commitOnce(); }

    /** Launches the popup in a real juce::CallOutBox anchored on `screenArea` (screen
     *  coordinates — the same shape AppearanceSettingsTab::openCableColourPicker's callers
     *  already compute via localAreaToGlobal). onPreview fires on every live change (selector
     *  drag or favourite click); onCommit fires exactly once, when the callout closes, with
     *  whatever colour was current at that point. */
    static void show(juce::Rectangle<int> screenArea, juce::Colour initial, juce::PropertiesFile* props,
                     std::function<void(juce::Colour)> onPreview, std::function<void(juce::Colour)> onCommit) {
        auto popup = std::make_unique<ColourPickerPopup>(initial, props, std::move(onPreview), std::move(onCommit));
        juce::CallOutBox::launchAsynchronously(std::move(popup), screenArea, nullptr);
    }

private:
    static constexpr int kSwatchSize = 28;
    static constexpr int kFavouritesAreaHeight = 40;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override {
        if (source != &selector_)
            return;
        previewNow(selector_.getCurrentColour());
    }

    // The ONE preview path — the async selector broadcast and the synchronous test seam both land
    // here, so the two can never disagree about what a "live change" does.
    void previewNow(juce::Colour c) {
        lastColour_ = c;
        if (onPreview_)
            onPreview_(lastColour_);
    }

    void commitOnce() {
        if (committed_)
            return;
        committed_ = true;
        if (onCommit_)
            onCommit_(lastColour_);
    }

    void rebuildFavouriteButtons() {
        favouriteButtons_.clear();
        for (int i = 0; i < (int)favourites_.size(); ++i) {
            const juce::Colour colour = favourites_[(size_t)i];
            auto button = std::make_unique<FavouriteSwatchButton>(colour);
            button->onClick = [this, colour] {
                lastColour_ = colour;
                selector_.setCurrentColour(colour, juce::dontSendNotification);
                if (onPreview_)
                    onPreview_(colour);
            };
            // Deferred: a right-click handler that rebuilds (and so destroys) the very button
            // whose event is still on the call stack would delete a component out from under its
            // own mouseDown — callAsync runs it once the event has finished unwinding.
            button->onRightClick = [this, colour] {
                juce::MessageManager::callAsync([this, colour] {
                    removeFavourite(favourites_, colour);
                    saveFavouriteColours(props_, favourites_);
                    rebuildFavouriteButtons();
                });
            };
            addAndMakeVisible(*button);
            favouriteButtons_.push_back(std::move(button));
        }
        resized();
    }

    void layoutFavouriteButtons(juce::Rectangle<int> area) {
        for (auto& button : favouriteButtons_) {
            button->setBounds(area.removeFromLeft(kSwatchSize).reduced(2));
        }
    }

    // A plain colour swatch that is a real Button (so onClick / hit-testing / focus work exactly
    // like every other control here) but reports a right-click through its own callback instead
    // of also firing onClick for it — a juce::ShapeButton/TextButton would trigger onClick on
    // either mouse button, which would fire preview AND remove from a single right-click.
    class FavouriteSwatchButton : public juce::Button {
    public:
        explicit FavouriteSwatchButton(juce::Colour c)
            : juce::Button("favouriteSwatch")
            , colour(c) {}

        void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
            auto bounds = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(down ? colour.darker(0.15f) : (highlighted ? colour.brighter(0.2f) : colour));
            g.fillRoundedRectangle(bounds, 3.0f);
            g.setColour(juce::Colours::black.withAlpha(0.4f));
            g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        }

        void mouseDown(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu()) {
                if (onRightClick)
                    onRightClick();
                return; // do not forward to Button::mouseDown — a right-click is not a click
            }
            juce::Button::mouseDown(e);
        }

        std::function<void()> onRightClick;
        juce::Colour colour;
    };

    juce::PropertiesFile* props_;
    std::vector<juce::Colour> favourites_;
    std::vector<std::unique_ptr<FavouriteSwatchButton>> favouriteButtons_;
    std::function<void(juce::Colour)> onPreview_;
    std::function<void(juce::Colour)> onCommit_;
    juce::Colour lastColour_;
    bool committed_ = false;

    juce::ColourSelector selector_{juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders |
                                   juce::ColourSelector::showColourspace};
    juce::TextButton addFavouriteButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColourPickerPopup)
};

} // namespace synth::ui
