#include "ThemeLoader.h"

namespace bombo
{

namespace
{
bool parseHexColour(const juce::String& hex, juce::Colour& out)
{
    // Expect format "#AARRGGBB" (length 9, '#' prefix).
    if (hex.length() != 9 || hex[0] != '#') return false;
    const auto n = static_cast<uint32_t>(hex.substring(1).getHexValue64());
    out = juce::Colour(n);
    return true;
}

// Field-by-field assignment with per-field validation.
// Returns false on first missing/invalid field, setting err.
bool fillPalette(const juce::var& pal, Palette& out, std::string& err)
{
    if (! pal.isObject()) { err = "palette is not an object"; return false; }

    auto take = [&](const char* key, juce::Colour& dest) -> bool
    {
        if (! pal.hasProperty(key))
        {
            err = std::string("missing field: ") + key;
            return false;
        }
        auto s = pal[key].toString();
        if (! parseHexColour(s, dest))
        {
            err = std::string("bad colour for ") + key + " = " + s.toStdString();
            return false;
        }
        return true;
    };

    // Optional fallbacks for the Phase 2e Mini-Nuke chassis fields. Themes
    // authored before 2026-05-17 (bandw/phosphor/nightrun) may omit these;
    // they get sensible derivations from the rest of the palette so a legacy
    // JSON keeps parsing without manual edits.
    auto takeOr = [&](const char* key, juce::Colour& dest, juce::Colour fallback)
    {
        if (! pal.hasProperty(key)) { dest = fallback; return; }
        auto s = pal[key].toString();
        if (! parseHexColour(s, dest)) dest = fallback;
    };

    const bool baseOk =
           take("graphite",    out.graphite)
        && take("graphiteHi",  out.graphiteHi)
        && take("ink",         out.ink)
        && take("bone",        out.bone)
        && take("boneDim",     out.boneDim)
        && take("voice",       out.voice)
        && take("drive",       out.drive)
        && take("delayC",      out.delayC)
        && take("reverb",      out.reverb)
        && take("filterC",     out.filterC)
        && take("duck",        out.duck)
        && take("knobCap",     out.knobCap)
        && take("knobBevel",   out.knobBevel)
        && take("knobRubber",  out.knobRubber)
        && take("accentAmber", out.accentAmber);

    if (! baseOk) return false;

    takeOr("bodyHi",     out.bodyHi,     out.graphiteHi);
    takeOr("bodyLo",     out.bodyLo,     out.graphite);
    takeOr("cap",        out.cap,        out.ink);
    takeOr("noseRed",    out.noseRed,    out.drive);
    takeOr("bandYellow", out.bandYellow, out.accentAmber);
    return true;
}
} // anonymous namespace

ThemeLoader::Result ThemeLoader::parse(juce::StringRef jsonText)
{
    Result r;
    juce::var parsed;
    auto pr = juce::JSON::parse(jsonText, parsed);
    if (pr.failed())
    {
        r.error = pr.getErrorMessage().toStdString();
        return r;
    }
    if (! parsed.isObject())
    {
        r.error = "root is not an object";
        return r;
    }
    if (! parsed.hasProperty("palette"))
    {
        r.error = "missing top-level 'palette'";
        return r;
    }

    r.name        = parsed.getProperty("name", "").toString().toStdString();
    r.displayName = parsed.getProperty("displayName", "").toString().toStdString();

    if (! fillPalette(parsed["palette"], r.palette, r.error))
        return r;

    r.ok = true;
    return r;
}

} // namespace bombo
