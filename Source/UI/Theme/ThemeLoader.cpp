#include "ThemeLoader.h"

namespace gsynth::theme {

// ---------------------------------------------------------------------------
// Thread-local error string
// ---------------------------------------------------------------------------

static thread_local juce::String tl_lastError;

static void setLastError(const juce::String& reason) { tl_lastError = reason; }

static void clearLastError() { tl_lastError = {}; }

juce::String ThemeLoader::getLastError() { return tl_lastError; }

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

std::optional<juce::Colour> ThemeLoader::parseHexColour(const juce::String& s) {
    if (s.isEmpty() || s[0] != '#')
        return std::nullopt;

    // Strip the '#'
    juce::String hex = s.substring(1).toUpperCase();
    const int len = hex.length();

    // Validate: must be all hex digits
    for (int i = 0; i < len; ++i) {
        const juce::juce_wchar c = hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
            return std::nullopt;
    }

    if (len == 3) {
        // #RGB -> #AARRGGBB, each nibble doubled, alpha = 0xFF
        const auto r = (uint8_t)(juce::CharacterFunctions::getHexDigitValue(hex[0]) * 0x11);
        const auto g = (uint8_t)(juce::CharacterFunctions::getHexDigitValue(hex[1]) * 0x11);
        const auto b = (uint8_t)(juce::CharacterFunctions::getHexDigitValue(hex[2]) * 0x11);
        return juce::Colour(r, g, b);
    } else if (len == 6) {
        // #RRGGBB, alpha = 0xFF
        const auto r = (uint8_t)((juce::CharacterFunctions::getHexDigitValue(hex[0]) << 4) |
                                 juce::CharacterFunctions::getHexDigitValue(hex[1]));
        const auto g = (uint8_t)((juce::CharacterFunctions::getHexDigitValue(hex[2]) << 4) |
                                 juce::CharacterFunctions::getHexDigitValue(hex[3]));
        const auto b = (uint8_t)((juce::CharacterFunctions::getHexDigitValue(hex[4]) << 4) |
                                 juce::CharacterFunctions::getHexDigitValue(hex[5]));
        return juce::Colour(r, g, b);
    } else if (len == 8) {
        // #AARRGGBB
        const auto a = (uint8_t)((juce::CharacterFunctions::getHexDigitValue(hex[0]) << 4) |
                                 juce::CharacterFunctions::getHexDigitValue(hex[1]));
        const auto r = (uint8_t)((juce::CharacterFunctions::getHexDigitValue(hex[2]) << 4) |
                                 juce::CharacterFunctions::getHexDigitValue(hex[3]));
        const auto g = (uint8_t)((juce::CharacterFunctions::getHexDigitValue(hex[4]) << 4) |
                                 juce::CharacterFunctions::getHexDigitValue(hex[5]));
        const auto b = (uint8_t)((juce::CharacterFunctions::getHexDigitValue(hex[6]) << 4) |
                                 juce::CharacterFunctions::getHexDigitValue(hex[7]));
        return juce::Colour(r, g, b, a);
    }

    return std::nullopt;
}

std::optional<ThemeStyle> ThemeLoader::parseStyle(const juce::String& s) {
    juce::String lower = s.toLowerCase();
    if (lower == "flat")
        return ThemeStyle::Flat;
    if (lower == "glass")
        return ThemeStyle::Glass;
    if (lower == "textured")
        return ThemeStyle::Textured;
    return std::nullopt;
}

juce::String ThemeLoader::styleToString(ThemeStyle s) {
    switch (s) {
    case ThemeStyle::Flat:
        return "flat";
    case ThemeStyle::Glass:
        return "glass";
    case ThemeStyle::Textured:
        return "textured";
    default:
        return "flat";
    }
}

// ---------------------------------------------------------------------------
// Internal colour-to-hex serialization helper
// ---------------------------------------------------------------------------

static juce::String colourToHex(juce::Colour c) {
    // Emit as #AARRGGBB (8 hex digits, uppercase, with '#').
    // Use individual channel accessors to avoid signed/unsigned issues with toHexString.
    const auto toHex2 = [](uint8_t v) -> juce::String {
        static const char digits[] = "0123456789ABCDEF";
        char buf[3] = {digits[(v >> 4) & 0xF], digits[v & 0xF], '\0'};
        return juce::String(buf);
    };
    return "#" + toHex2(c.getAlpha()) + toHex2(c.getRed()) + toHex2(c.getGreen()) + toHex2(c.getBlue());
}

// ---------------------------------------------------------------------------
// parseTheme
// ---------------------------------------------------------------------------

// Helper: parse a named colour from an object var. Returns nullopt and sets lastError on fail.
// isRequired: if true, also fails when the key is absent.
static std::optional<juce::Colour> parseColourKey(const juce::var& obj, const juce::String& key, bool isRequired,
                                                  juce::Colour defaultValue) {
    const juce::var val = obj.getProperty(juce::Identifier(key), {});
    if (val.isVoid() || val.isUndefined()) {
        if (isRequired) {
            setLastError("missing required color \"" + key + "\"");
            return std::nullopt;
        }
        return defaultValue;
    }

    auto result = ThemeLoader::parseHexColour(val.toString());
    if (!result.has_value()) {
        setLastError("malformed color value for \"" + key + "\": \"" + val.toString() + "\"");
        return std::nullopt;
    }
    return result;
}

// Helper: parse an optional float from an object var, clamping to [min, max].
static float parseFloatClamped(const juce::var& obj, const juce::String& key, float defaultValue, float minVal = 0.0f,
                               float maxVal = 1.0f) {
    const juce::var val = obj.getProperty(juce::Identifier(key), {});
    if (val.isVoid() || val.isUndefined())
        return defaultValue;
    return juce::jlimit(minVal, maxVal, (float)val);
}

// Helper: parse an optional int from an object var.
static int parseIntOptional(const juce::var& obj, const juce::String& key, int defaultValue) {
    const juce::var val = obj.getProperty(juce::Identifier(key), {});
    if (val.isVoid() || val.isUndefined())
        return defaultValue;
    return (int)val;
}

// Helper: parse an optional string from an object var.
static juce::String parseStringOptional(const juce::var& obj, const juce::String& key,
                                        const juce::String& defaultValue) {
    const juce::var val = obj.getProperty(juce::Identifier(key), {});
    if (val.isVoid() || val.isUndefined())
        return defaultValue;
    return val.toString();
}

std::optional<Theme> ThemeLoader::parseTheme(const juce::var& json, const juce::String& suggestedId) {
    clearLastError();

    // Root must be an object
    if (!json.isObject()) {
        setLastError("root is not a JSON object");
        return std::nullopt;
    }

    // Schema version check (optional key; default 1; reject if > kSchemaVersion)
    const juce::var& schemaVal = json["schemaVersion"];
    if (!schemaVal.isVoid() && !schemaVal.isUndefined()) {
        const int schemaVersion = (int)schemaVal;
        if (schemaVersion > kSchemaVersion) {
            setLastError("unsupported schemaVersion " + juce::String(schemaVersion) +
                         " (max supported: " + juce::String(kSchemaVersion) + ")");
            return std::nullopt;
        }
    }

    // Required: "name"
    const juce::var& nameVal = json["name"];
    if (nameVal.isVoid() || nameVal.isUndefined() || nameVal.toString().isEmpty()) {
        setLastError("missing required key \"name\"");
        return std::nullopt;
    }

    // Required: "colors" object present
    const juce::var& colorsVar = json["colors"];
    if (!colorsVar.isObject()) {
        setLastError("missing required key \"colors\" (must be an object)");
        return std::nullopt;
    }

    Theme theme;
    theme.name = nameVal.toString();

    // id: from JSON "id" or suggestedId (filename slug)
    const juce::var& idVal = json["id"];
    if (!idVal.isVoid() && !idVal.isUndefined() && !idVal.toString().isEmpty())
        theme.id = idVal.toString();
    else if (suggestedId.isNotEmpty())
        theme.id = suggestedId;
    else
        theme.id = "untitled";

    theme.isUserTheme = false; // caller sets this after parsing

    // --------------- Colors ---------------
    // Required minimum: bg0, surface, accent, textPrimary, audioWire, modWire.
    // All other color tokens are optional (default to Obsidian values).
    Colors defaults; // default-constructed (Obsidian values)
    Colors colors;

    // Parse required colors first (fail fast)
    {
        auto parsed_bg0 = parseColourKey(colorsVar, "bg0", true, defaults.bg0);
        if (!parsed_bg0)
            return std::nullopt;
        colors.bg0 = *parsed_bg0;

        auto parsed_surface = parseColourKey(colorsVar, "surface", true, defaults.surface);
        if (!parsed_surface)
            return std::nullopt;
        colors.surface = *parsed_surface;

        auto parsed_accent = parseColourKey(colorsVar, "accent", true, defaults.accent);
        if (!parsed_accent)
            return std::nullopt;
        colors.accent = *parsed_accent;

        auto parsed_textPrimary = parseColourKey(colorsVar, "textPrimary", true, defaults.textPrimary);
        if (!parsed_textPrimary)
            return std::nullopt;
        colors.textPrimary = *parsed_textPrimary;

        auto parsed_audioWire = parseColourKey(colorsVar, "audioWire", true, defaults.audioWire);
        if (!parsed_audioWire)
            return std::nullopt;
        colors.audioWire = *parsed_audioWire;

        auto parsed_modWire = parseColourKey(colorsVar, "modWire", true, defaults.modWire);
        if (!parsed_modWire)
            return std::nullopt;
        colors.modWire = *parsed_modWire;
    }

    // Parse optional colors (malformed = fail, absent = default)
    {
        auto v = parseColourKey(colorsVar, "bg1", false, defaults.bg1);
        if (!v)
            return std::nullopt;
        colors.bg1 = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "surfaceHi", false, defaults.surfaceHi);
        if (!v)
            return std::nullopt;
        colors.surfaceHi = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "border", false, defaults.border);
        if (!v)
            return std::nullopt;
        colors.border = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "accent2", false, defaults.accent2);
        if (!v)
            return std::nullopt;
        colors.accent2 = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "pitchWire", false, defaults.pitchWire);
        if (!v)
            return std::nullopt;
        colors.pitchWire = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "gateWire", false, defaults.gateWire);
        if (!v)
            return std::nullopt;
        colors.gateWire = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "polyBusWire", false, defaults.polyBusWire);
        if (!v)
            return std::nullopt;
        colors.polyBusWire = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "textMuted", false, defaults.textMuted);
        if (!v)
            return std::nullopt;
        colors.textMuted = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "textDisabled", false, defaults.textDisabled);
        if (!v)
            return std::nullopt;
        colors.textDisabled = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "success", false, defaults.success);
        if (!v)
            return std::nullopt;
        colors.success = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "warning", false, defaults.warning);
        if (!v)
            return std::nullopt;
        colors.warning = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "error", false, defaults.error);
        if (!v)
            return std::nullopt;
        colors.error = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "knobBody", false, defaults.knobBody);
        if (!v)
            return std::nullopt;
        colors.knobBody = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "knobPointer", false, defaults.knobPointer);
        if (!v)
            return std::nullopt;
        colors.knobPointer = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "meterFill", false, defaults.meterFill);
        if (!v)
            return std::nullopt;
        colors.meterFill = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "modRingPositive", false, defaults.modRingPositive);
        if (!v)
            return std::nullopt;
        colors.modRingPositive = *v;
    }
    {
        auto v = parseColourKey(colorsVar, "modRingNegative", false, defaults.modRingNegative);
        if (!v)
            return std::nullopt;
        colors.modRingNegative = *v;
    }

    theme.colors = colors;

    // --------------- Metrics (all optional) ---------------
    {
        Metrics defaults_m;
        const juce::var& metricsVar = json["metrics"];
        Metrics m;

        if (metricsVar.isObject()) {
            m.cornerRadius = parseFloatClamped(metricsVar, "cornerRadius", defaults_m.cornerRadius, 0.0f, 1000.0f);
            m.windowRadius = parseFloatClamped(metricsVar, "windowRadius", defaults_m.windowRadius, 0.0f, 1000.0f);
            m.pillRadius = parseFloatClamped(metricsVar, "pillRadius", defaults_m.pillRadius, 0.0f, 1000.0f);
            m.padding = parseIntOptional(metricsVar, "padding", defaults_m.padding);
            m.spacingUnit = parseIntOptional(metricsVar, "spacingUnit", defaults_m.spacingUnit);
            m.knobTrackWidth = parseFloatClamped(metricsVar, "knobTrackWidth", defaults_m.knobTrackWidth, 0.0f, 100.0f);
            m.knobRingWidth = parseFloatClamped(metricsVar, "knobRingWidth", defaults_m.knobRingWidth, 0.0f, 100.0f);
            m.borderWidth = parseFloatClamped(metricsVar, "borderWidth", defaults_m.borderWidth, 0.0f, 100.0f);
            m.wireCoreWidth = parseFloatClamped(metricsVar, "wireCoreWidth", defaults_m.wireCoreWidth, 0.0f, 100.0f);
            m.wireCasingWidth =
                parseFloatClamped(metricsVar, "wireCasingWidth", defaults_m.wireCasingWidth, 0.0f, 100.0f);
        } else {
            m = defaults_m;
        }

        theme.metrics = m;
    }

    // --------------- Typography (all optional) ---------------
    {
        Typography defaults_t;
        const juce::var& typeVar = json["typography"];
        Typography t;

        if (typeVar.isObject()) {
            t.uiFamily = parseStringOptional(typeVar, "uiFamily", defaults_t.uiFamily);
            t.monoFamily = parseStringOptional(typeVar, "monoFamily", defaults_t.monoFamily);
            t.h1 = parseFloatClamped(typeVar, "h1", defaults_t.h1, 0.0f, 1000.0f);
            t.h2 = parseFloatClamped(typeVar, "h2", defaults_t.h2, 0.0f, 1000.0f);
            t.label = parseFloatClamped(typeVar, "label", defaults_t.label, 0.0f, 1000.0f);
            t.value = parseFloatClamped(typeVar, "value", defaults_t.value, 0.0f, 1000.0f);
            t.micro = parseFloatClamped(typeVar, "micro", defaults_t.micro, 0.0f, 1000.0f);
        } else {
            t = defaults_t;
        }

        theme.type = t;
    }

    // --------------- Treatment (all optional) ---------------
    {
        Treatment defaults_tr;
        const juce::var& treatVar = json["treatment"];
        Treatment tr;

        if (treatVar.isObject()) {
            // style string: optional, malformed = fail
            const juce::var& styleVal = treatVar["style"];
            if (!styleVal.isVoid() && !styleVal.isUndefined()) {
                auto parsedStyle = parseStyle(styleVal.toString());
                if (!parsedStyle.has_value()) {
                    setLastError("unknown treatment style: \"" + styleVal.toString() + "\"");
                    return std::nullopt;
                }
                tr.style = *parsedStyle;
            } else {
                tr.style = defaults_tr.style;
            }

            // Floats: clamped, NOT rejected if out of range
            tr.glow = parseFloatClamped(treatVar, "glow", defaults_tr.glow);
            tr.shadow = parseFloatClamped(treatVar, "shadow", defaults_tr.shadow);
            tr.blur = parseFloatClamped(treatVar, "blur", defaults_tr.blur);
            tr.texture = parseFloatClamped(treatVar, "texture", defaults_tr.texture);
        } else {
            tr = defaults_tr;
        }

        theme.treatment = tr;
    }

    clearLastError();
    return theme;
}

// ---------------------------------------------------------------------------
// themeToJson
// ---------------------------------------------------------------------------

juce::var ThemeLoader::themeToJson(const Theme& theme) {
    auto* root = new juce::DynamicObject();

    root->setProperty("schemaVersion", kSchemaVersion);
    root->setProperty("name", theme.name);
    root->setProperty("id", theme.id);

    // Colors
    {
        auto* colors = new juce::DynamicObject();
        const Colors& c = theme.colors;

        colors->setProperty("bg0", colourToHex(c.bg0));
        colors->setProperty("bg1", colourToHex(c.bg1));
        colors->setProperty("surface", colourToHex(c.surface));
        colors->setProperty("surfaceHi", colourToHex(c.surfaceHi));
        colors->setProperty("border", colourToHex(c.border));
        colors->setProperty("accent", colourToHex(c.accent));
        colors->setProperty("accent2", colourToHex(c.accent2));
        colors->setProperty("audioWire", colourToHex(c.audioWire));
        colors->setProperty("modWire", colourToHex(c.modWire));
        colors->setProperty("pitchWire", colourToHex(c.pitchWire));
        colors->setProperty("gateWire", colourToHex(c.gateWire));
        colors->setProperty("polyBusWire", colourToHex(c.polyBusWire));
        colors->setProperty("textPrimary", colourToHex(c.textPrimary));
        colors->setProperty("textMuted", colourToHex(c.textMuted));
        colors->setProperty("textDisabled", colourToHex(c.textDisabled));
        colors->setProperty("success", colourToHex(c.success));
        colors->setProperty("warning", colourToHex(c.warning));
        colors->setProperty("error", colourToHex(c.error));
        colors->setProperty("knobBody", colourToHex(c.knobBody));
        colors->setProperty("knobPointer", colourToHex(c.knobPointer));
        colors->setProperty("meterFill", colourToHex(c.meterFill));
        colors->setProperty("modRingPositive", colourToHex(c.modRingPositive));
        colors->setProperty("modRingNegative", colourToHex(c.modRingNegative));

        root->setProperty("colors", juce::var(colors));
    }

    // Metrics
    {
        auto* metrics = new juce::DynamicObject();
        const Metrics& m = theme.metrics;

        metrics->setProperty("cornerRadius", m.cornerRadius);
        metrics->setProperty("windowRadius", m.windowRadius);
        metrics->setProperty("pillRadius", m.pillRadius);
        metrics->setProperty("padding", m.padding);
        metrics->setProperty("spacingUnit", m.spacingUnit);
        metrics->setProperty("knobTrackWidth", m.knobTrackWidth);
        metrics->setProperty("knobRingWidth", m.knobRingWidth);
        metrics->setProperty("borderWidth", m.borderWidth);
        metrics->setProperty("wireCoreWidth", m.wireCoreWidth);
        metrics->setProperty("wireCasingWidth", m.wireCasingWidth);

        root->setProperty("metrics", juce::var(metrics));
    }

    // Typography
    {
        auto* type = new juce::DynamicObject();
        const Typography& t = theme.type;

        type->setProperty("uiFamily", t.uiFamily);
        type->setProperty("monoFamily", t.monoFamily);
        type->setProperty("h1", t.h1);
        type->setProperty("h2", t.h2);
        type->setProperty("label", t.label);
        type->setProperty("value", t.value);
        type->setProperty("micro", t.micro);

        root->setProperty("typography", juce::var(type));
    }

    // Treatment
    {
        auto* treatment = new juce::DynamicObject();
        const Treatment& tr = theme.treatment;

        treatment->setProperty("style", styleToString(tr.style));
        treatment->setProperty("glow", tr.glow);
        treatment->setProperty("shadow", tr.shadow);
        treatment->setProperty("blur", tr.blur);
        treatment->setProperty("texture", tr.texture);

        root->setProperty("treatment", juce::var(treatment));
    }

    return juce::var(root);
}

} // namespace gsynth::theme
