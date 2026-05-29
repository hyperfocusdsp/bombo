#include "ThemeLoader.h"

namespace bombo
{

namespace
{
bool parseHexColour(const juce::String& hex, juce::Colour& out)
{
    // Accept "#AARRGGBB" (length 9) or "#RRGGBB" (length 7, implicit alpha=FF).
    // 2026-05-24: matrix.json / cyber.json / plasma.json shipped with 7-char
    // values, which silently failed parsing, so those palettes never got
    // registered and BOMBO_FORCE_THEME=cyber was a no-op.
    if (hex[0] != '#') return false;
    uint32_t n = 0;
    if (hex.length() == 9)
    {
        n = static_cast<uint32_t>(hex.substring(1).getHexValue64());
    }
    else if (hex.length() == 7)
    {
        n = 0xFF000000u
          | static_cast<uint32_t>(hex.substring(1).getHexValue64());
    }
    else
    {
        return false;
    }
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

    if (pal.hasProperty("chassisOverlayOpacity"))
        out.chassisOverlayOpacity = juce::jlimit(0.0f, 1.0f,
                                                  static_cast<float>(static_cast<double>(pal["chassisOverlayOpacity"])));

    // Optional chassis interior treatment. Defaults to Grain (legacy look).
    if (pal.hasProperty("bodyStyle"))
    {
        const auto s = pal["bodyStyle"].toString().trim().toLowerCase();
        if (s == "flat")      out.bodyStyle = BodyStyle::Flat;
        else if (s == "camo") out.bodyStyle = BodyStyle::Camo;
        else                  out.bodyStyle = BodyStyle::Grain;
    }

    // Optional named chassis art (e.g. "fallout"). Only the FALLOUT theme
    // ships it; every other theme leaves this empty and stays procedural.
    if (pal.hasProperty("chassisArt"))
        out.chassisArt = pal["chassisArt"].toString();

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
