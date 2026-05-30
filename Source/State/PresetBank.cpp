#include "PresetBank.h"

#include <BinaryData.h>

#include <algorithm>
#include <set>
#include <string>

#include "../ParameterIds.h"

namespace bombo
{

namespace
{

// Built once from the single source of truth in ParameterIds.h.
const std::set<std::string>& excludedIds()
{
    static const std::set<std::string> ids = [] {
        std::set<std::string> s;
        for (const char* id : kExcludedFromPresets) s.emplace(id);
        return s;
    }();
    return ids;
}

// Extract a Preset from an already-parsed JSON object. Shared by the
// BinaryData/file loaders (via parseBlob) and the bank importer (per array
// element), so every entry point yields the identical Preset shape.
bool parseVar(const juce::var& parsed, PresetBank::Preset& out)
{
    if (! parsed.isObject()) return false;

    out.name        = parsed.getProperty("name", "").toString().toStdString();
    out.displayName = parsed.getProperty("displayName", "").toString().toStdString();
    if (out.displayName.empty()) out.displayName = out.name;
    if (out.name.empty()) return false;

    out.params.clear();
    if (parsed.hasProperty("params"))
    {
        const auto& pv = parsed["params"];
        if (auto* obj = pv.getDynamicObject())
        {
            for (const auto& kv : obj->getProperties())
            {
                const auto id = kv.name.toString().toStdString();
                if (excludedIds().count(id) != 0) continue;
                out.params.emplace_back(id,
                                        static_cast<float>((double) kv.value));
            }
        }
    }

    // FX chain order — optional, absent on legacy presets. Format:
    //   "fxOrder": ["drive", "filter", "delay", "reverb"]
    out.fxOrder.reset();
    if (parsed.hasProperty("fxOrder"))
    {
        const auto& fv = parsed["fxOrder"];
        if (auto* arr = fv.getArray(); arr != nullptr && arr->size() == 4)
        {
            FxOrder o{};
            bool ok = true;
            for (int i = 0; i < 4; ++i)
                ok = ok && fxIdFromString((*arr)[i].toString(),
                                          o[(std::size_t) i]);
            if (ok && isValidFxOrder(o)) out.fxOrder = o;
        }
    }
    return true;
}

bool parseBlob(const char* data, int size, PresetBank::Preset& out)
{
    if (data == nullptr || size <= 0) return false;
    juce::var parsed;
    if (juce::JSON::parse(juce::String(juce::CharPointer_UTF8(data),
                                       (size_t) size), parsed).failed())
        return false;
    return parseVar(parsed, out);
}

// Reset every non-excluded APVTS param to its default. Shared by
// applyByIndex (so a sparse preset can't inherit the previous preset's
// values) and applyDefaults (the Init path). Excluded ids — master out,
// BPM, transport, section mutes — are left untouched in both cases.
void resetNonExcludedToDefaults(juce::AudioProcessorValueTreeState& apvts)
{
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (rp == nullptr) continue;
        if (excludedIds().count(rp->getParameterID().toStdString()) != 0)
            continue;
        const float d = rp->getDefaultValue();
        rp->beginChangeGesture();
        rp->setValueNotifyingHost(d);
        rp->endChangeGesture();
    }
}

// Snapshot APVTS to a (id, plain-value) list, excluding the same ids that
// applyByIndex skips. Plain (not normalized) — preset JSON stores plain so
// it's human-readable and survives param-range tweaks gracefully.
std::vector<std::pair<std::string, float>>
snapshotApvts(juce::AudioProcessorValueTreeState& apvts)
{
    std::vector<std::pair<std::string, float>> out;
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (rp == nullptr) continue;
        const auto id = rp->getParameterID().toStdString();
        if (excludedIds().count(id) != 0) continue;
        const float plain = rp->convertFrom0to1(rp->getValue());
        out.emplace_back(id, plain);
    }
    return out;
}

// Serialize one preset to a JSON object. Shared by the single-file writer and
// the bank exporter so both emit the identical shape.
juce::var presetToVar(const juce::String& name,
                      const juce::String& displayName,
                      const std::vector<std::pair<std::string, float>>& params,
                      const std::optional<FxOrder>& fxOrder)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("name", name);
    root->setProperty("displayName", displayName);

    juce::DynamicObject::Ptr paramsObj = new juce::DynamicObject();
    for (const auto& kv : params)
        paramsObj->setProperty(juce::String(kv.first), kv.second);
    root->setProperty("params", juce::var(paramsObj.get()));

    if (fxOrder.has_value())
    {
        juce::Array<juce::var> arr;
        for (auto f : *fxOrder)
            arr.add(juce::var(juce::String(fxIdToString(f))));
        root->setProperty("fxOrder", juce::var(arr));
    }
    return juce::var(root.get());
}

bool writePresetJson(const juce::File& file,
                     const juce::String& name,
                     const juce::String& displayName,
                     const std::vector<std::pair<std::string, float>>& params,
                     const std::optional<FxOrder>& fxOrder)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText(
        juce::JSON::toString(presetToVar(name, displayName, params, fxOrder), true));
}

} // anonymous namespace

PresetBank::PresetBank()
{
    migrateLegacyUserPresets();
    rebuildAll();
}

void PresetBank::loadFactoryFromBinaryData()
{
    struct Entry { juce::String origName; const char* resource; };
    std::vector<Entry> ordered;
    ordered.reserve((size_t) BinaryData::namedResourceListSize);

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* res  = BinaryData::namedResourceList[i];
        const char* orig = BinaryData::getNamedResourceOriginalFilename(res);
        if (orig == nullptr) continue;
        juce::String s(orig);
        if (! s.endsWithIgnoreCase(".json")) continue;
        // Factory presets follow Resources/Presets/NN_<slug>.json — the
        // leading digit lets us tell them apart from theme JSONs without
        // tracking the source folder.
        if (s.length() < 4 || ! juce::CharacterFunctions::isDigit(s[0])) continue;
        ordered.push_back({ s, res });
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const Entry& a, const Entry& b) { return a.origName < b.origName; });

    for (const auto& e : ordered)
    {
        int sz = 0;
        const char* data = BinaryData::getNamedResource(e.resource, sz);
        Preset preset;
        preset.source = Source::Factory;
        if (! parseBlob(data, sz, preset)) continue;
        // Apply session-only factory edits (see header): hide deleted ones,
        // override the display name of renamed ones (keyed by canonical name).
        if (std::find(hiddenFactoryNames_.begin(), hiddenFactoryNames_.end(),
                      preset.name) != hiddenFactoryNames_.end())
            continue;
        for (const auto& r : renamedFactory_)
            if (r.first == preset.name) { preset.displayName = r.second; break; }
        presets_.push_back(std::move(preset));
    }
}

void PresetBank::rebuildAll()
{
    juce::String anchorName;
    if (current_ >= 0 && current_ < (int) presets_.size())
        anchorName = juce::String(presets_[(size_t) current_].displayName);

    presets_.clear();
    loadFactoryFromBinaryData();
    refreshUserPresets();   // appends user presets + re-anchors current_
    applyFactoryDedup();    // hide factory presets shadowed by a user copy

    if (anchorName.isNotEmpty())
    {
        current_ = findByDisplayName(anchorName);
        if (current_ < 0) current_ = presets_.empty() ? -1 : 0;
    }
}

void PresetBank::applyFactoryDedup()
{
    // Canonical names of every user preset, lowercased for case-insensitive
    // match. A factory preset is dropped when a user preset shadows it.
    std::set<std::string> userNames;
    for (const auto& p : presets_)
        if (p.source == Source::User)
            userNames.insert(juce::String(p.name).toLowerCase().toStdString());

    if (userNames.empty()) return;

    presets_.erase(std::remove_if(presets_.begin(), presets_.end(),
                       [&](const Preset& p)
                       {
                           return p.source == Source::Factory
                               && userNames.count(juce::String(p.name)
                                                      .toLowerCase().toStdString()) != 0;
                       }),
                   presets_.end());
}

void PresetBank::refreshUserPresets()
{
    // Remember the current preset by display name so we can re-anchor after
    // the rebuild (factory ordering doesn't change, user ordering can).
    juce::String anchorName;
    if (current_ >= 0 && current_ < (int) presets_.size())
        anchorName = juce::String(presets_[(size_t) current_].displayName);

    presets_.erase(std::remove_if(presets_.begin(), presets_.end(),
                                  [](const Preset& p) { return p.source == Source::User; }),
                   presets_.end());

    const auto dir = userPresetsDir();
    if (dir.isDirectory())
    {
        juce::Array<juce::File> files;
        dir.findChildFiles(files, juce::File::findFiles, false, "*.json");
        std::vector<juce::File> sorted(files.begin(), files.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const juce::File& a, const juce::File& b)
                  { return a.getFileName().compareIgnoreCase(b.getFileName()) < 0; });

        for (const auto& f : sorted)
        {
            const auto text = f.loadFileAsString();
            Preset preset;
            preset.source   = Source::User;
            preset.filePath = f;
            if (parseBlob(text.toRawUTF8(), text.getNumBytesAsUTF8(), preset))
                presets_.push_back(std::move(preset));
        }
    }

    if (anchorName.isNotEmpty())
    {
        current_ = findByDisplayName(anchorName);
        if (current_ < 0) current_ = presets_.empty() ? -1 : 0;
    }
}

const std::string& PresetBank::currentName() const
{
    static const std::string empty;
    if (current_ < 0 || current_ >= (int) presets_.size()) return empty;
    return presets_[(size_t) current_].name;
}

const std::string& PresetBank::currentDisplayName() const
{
    static const std::string empty;
    if (current_ < 0 || current_ >= (int) presets_.size()) return empty;
    return presets_[(size_t) current_].displayName;
}

bool PresetBank::isCurrentUserPreset() const noexcept
{
    if (current_ < 0 || current_ >= (int) presets_.size()) return false;
    return presets_[(size_t) current_].source == Source::User;
}

void PresetBank::applyByIndex(int idx, juce::AudioProcessorValueTreeState& apvts)
{
    if (idx < 0 || idx >= (int) presets_.size()) return;
    const auto& preset = presets_[(size_t) idx];
    // Factory presets are sparse (hand-authored, only the salient params).
    // Without a reset first, any param a preset omits keeps the previously
    // loaded preset's value — so the same preset sounds different depending
    // on what was loaded before, and params added after a preset was
    // authored (kbtrk, voice_b_synth_on, reverb_type, dec_routing) never
    // return to their defaults. Reset, then overlay, makes every preset
    // deterministic from any starting state.
    resetNonExcludedToDefaults(apvts);
    for (const auto& kv : preset.params)
    {
        auto* p = apvts.getParameter(juce::String(kv.first));
        if (p == nullptr) continue;
        const float norm = p->convertTo0to1(kv.second);
        p->beginChangeGesture();
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
        p->endChangeGesture();
    }
    current_ = idx;
    if (onPresetApplied) onPresetApplied(preset);
}

void PresetBank::next(juce::AudioProcessorValueTreeState& apvts)
{
    if (presets_.empty()) return;
    const int n = (int) presets_.size();
    applyByIndex((current_ < 0) ? 0 : (current_ + 1) % n, apvts);
}

void PresetBank::prev(juce::AudioProcessorValueTreeState& apvts)
{
    if (presets_.empty()) return;
    const int n = (int) presets_.size();
    applyByIndex((current_ <= 0) ? n - 1 : current_ - 1, apvts);
}

void PresetBank::applyDefaults(juce::AudioProcessorValueTreeState& apvts)
{
    resetNonExcludedToDefaults(apvts);

    // No factory/user preset is "current" after init — the bar should
    // reflect that with its "N presets" empty state rather than keep
    // showing the previously-loaded name.
    current_ = -1;

    // Synthesize a minimal sentinel preset and fire onPresetApplied so
    // the editor's hook runs (currently: requestPresetTailReset()).
    // Without this the just-loaded preset's reverb tail / delay buffer
    // bleeds into the first trigger after init, exactly the same bug as
    // missing the tail reset on normal preset switches.
    if (onPresetApplied)
    {
        Preset sentinel;
        sentinel.source      = Source::Factory;
        sentinel.name        = "init";
        sentinel.displayName = "Init";
        // params + fxOrder intentionally empty: applyDefaults already
        // wrote every param above, and editor's onPresetApplied skips the
        // FX-order rewrite when fxOrder is nullopt (legacy presets path).
        onPresetApplied(sentinel);
    }
}

int PresetBank::saveAs(const juce::String& displayName,
                       juce::AudioProcessorValueTreeState& apvts)
{
    const auto safeStem = sanitizeFilename(displayName);
    if (safeStem.isEmpty()) return -1;
    const auto dir  = userPresetsDir();
    // First-save on a fresh install — the directory doesn't exist yet, so
    // writePresetJson would fail silently. Create it (and any parents) up
    // front so the JSON write below has somewhere to land.
    if (! dir.isDirectory())
    {
        const auto res = dir.createDirectory();
        if (res.failed()) return -1;
    }
    const auto file = dir.getChildFile(safeStem + ".json");
    if (file.existsAsFile()) return -1;   // refuse to clobber

    const auto fxOrderToSave = fxOrderProvider ? fxOrderProvider() : std::nullopt;
    if (! writePresetJson(file, displayName.toUpperCase(), displayName,
                          snapshotApvts(apvts), fxOrderToSave))
        return -1;

    rebuildAll();
    const int newIdx = findByDisplayName(displayName);
    if (newIdx >= 0) current_ = newIdx;
    return newIdx;
}

bool PresetBank::overwriteCurrent(juce::AudioProcessorValueTreeState& apvts)
{
    if (current_ < 0 || current_ >= (int) presets_.size()) return false;
    const auto& cur = presets_[(size_t) current_];
    const auto fxOrderToSave = fxOrderProvider ? fxOrderProvider() : std::nullopt;

    // User preset: overwrite its own file in place.
    if (cur.source == Source::User)
    {
        if (cur.filePath == juce::File()) return false;
        if (! writePresetJson(cur.filePath, juce::String(cur.name),
                              juce::String(cur.displayName), snapshotApvts(apvts),
                              fxOrderToSave))
            return false;
        rebuildAll();
        return true;
    }

    // Factory preset: materialise a user copy carrying the factory's canonical
    // name so applyFactoryDedup() shadows the compiled-in original. This is the
    // pre-lock authoring path — edit a factory sound, Save, and the edit
    // persists as a user preset that replaces the factory entry. Baking the
    // final bank back into Resources/Presets/ re-locks it later.
    const auto canonicalName = juce::String(cur.name);
    const auto displayName   = juce::String(cur.displayName);
    const auto safeStem      = sanitizeFilename(displayName);
    if (safeStem.isEmpty()) return false;
    const auto dir = userPresetsDir();
    if (! dir.isDirectory() && dir.createDirectory().failed()) return false;
    const auto file = dir.getChildFile(safeStem + ".json");
    if (! writePresetJson(file, canonicalName, displayName,
                          snapshotApvts(apvts), fxOrderToSave))
        return false;

    rebuildAll();
    current_ = findByDisplayName(displayName);
    if (current_ < 0) current_ = presets_.empty() ? -1 : 0;
    return true;
}

bool PresetBank::renameAt(int idx, const juce::String& newDisplayName)
{
    if (idx < 0 || idx >= (int) presets_.size()) return false;
    auto& p = presets_[(size_t) idx];

    // Factory presets: materialise a user copy under the new display name,
    // carrying the factory's canonical `name` so applyFactoryDedup() hides the
    // compiled-in original. Persists across reloads (unlike the old session-
    // only override), so the bank can be re-titled while authoring before it's
    // baked back into Resources/Presets/. Renames the SOUND verbatim — copies
    // the factory's stored params + fxOrder, not a fresh APVTS snapshot.
    if (p.source == Source::Factory)
    {
        if (newDisplayName.trim().isEmpty()) return false;
        const auto safeStem = sanitizeFilename(newDisplayName);
        if (safeStem.isEmpty()) return false;
        const auto dir = userPresetsDir();
        if (! dir.isDirectory() && dir.createDirectory().failed()) return false;
        const auto file = dir.getChildFile(safeStem + ".json");
        if (! writePresetJson(file, juce::String(p.name), newDisplayName,
                              p.params, p.fxOrder))
            return false;
        rebuildAll();
        current_ = findByDisplayName(newDisplayName);
        if (current_ < 0) current_ = presets_.empty() ? -1 : 0;
        return true;
    }

    const auto safeStem = sanitizeFilename(newDisplayName);
    if (safeStem.isEmpty()) return false;
    const auto dir     = userPresetsDir();
    const auto newFile = dir.getChildFile(safeStem + ".json");
    if (newFile.existsAsFile() && newFile != p.filePath) return false;

    // Write into the new location first, then delete the old one — safer
    // than rename + write if the destination is on a different filesystem.
    // Rename preserves the preset's existing fxOrder verbatim — this is a
    // pure file move + display-name update, not a re-snapshot.
    if (! writePresetJson(newFile, newDisplayName.toUpperCase(), newDisplayName,
                          p.params, p.fxOrder))
        return false;
    if (newFile != p.filePath) p.filePath.deleteFile();
    refreshUserPresets();
    current_ = findByDisplayName(newDisplayName);
    if (current_ < 0) current_ = presets_.empty() ? -1 : 0;
    return true;
}

bool PresetBank::deleteAt(int idx)
{
    if (idx < 0 || idx >= (int) presets_.size()) return false;
    auto& p = presets_[(size_t) idx];
    const int newCurrent = juce::jmax(0, idx - 1);

    // Factory presets: session-only hide (can't remove BinaryData). Persisted
    // bank edit = remove the entry from Resources/Presets/ and rebuild.
    if (p.source == Source::Factory)
    {
        hiddenFactoryNames_.push_back(p.name);
        rebuildAll();
        current_ = juce::jlimit(-1, (int) presets_.size() - 1, newCurrent);
        return true;
    }

    const auto file = p.filePath;
    if (! file.deleteFile()) return false;
    refreshUserPresets();
    current_ = juce::jlimit(-1, (int) presets_.size() - 1, newCurrent);
    return true;
}

int PresetBank::findByDisplayName(const juce::String& name) const
{
    for (int i = 0; i < (int) presets_.size(); ++i)
        if (juce::String(presets_[(size_t) i].displayName).equalsIgnoreCase(name))
            return i;
    return -1;
}

int PresetBank::importBankFile(const juce::File& file)
{
    if (! file.existsAsFile()) return 0;

    juce::var parsed;
    if (juce::JSON::parse(file.loadFileAsString(), parsed).failed())
        return 0;

    const auto dir = userPresetsDir();
    if (! dir.isDirectory() && dir.createDirectory().failed()) return 0;

    auto importOne = [&](const juce::var& v) -> bool
    {
        Preset p;
        p.source = Source::User;
        if (! parseVar(v, p)) return false;

        auto stem = sanitizeFilename(juce::String(p.displayName));
        if (stem.isEmpty()) return false;

        // Never clobber an existing preset — suffix _2, _3, ... on collision.
        juce::File out = dir.getChildFile(stem + ".json");
        for (int n = 2; out.existsAsFile(); ++n)
            out = dir.getChildFile(stem + "_" + juce::String(n) + ".json");

        return writePresetJson(out, juce::String(p.name),
                               juce::String(p.displayName), p.params, p.fxOrder);
    };

    int imported = 0;
    if (auto* arr = parsed.getArray())
    {
        for (const auto& v : *arr)
            if (importOne(v)) ++imported;
    }
    else if (importOne(parsed))
    {
        ++imported;
    }

    if (imported > 0) rebuildAll();
    return imported;
}

bool PresetBank::exportBankToFile(const juce::File& file, bool includeFactory)
{
    juce::Array<juce::var> arr;
    for (const auto& p : presets_)
    {
        if (! includeFactory && p.source != Source::User) continue;
        arr.add(presetToVar(juce::String(p.name), juce::String(p.displayName),
                            p.params, p.fxOrder));
    }
    if (arr.isEmpty()) return false;

    file.getParentDirectory().createDirectory();
    return file.replaceWithText(juce::JSON::toString(juce::var(arr), true));
}

juce::File PresetBank::userPresetsDir()
{
    // userApplicationDataDirectory already resolves to the per-user config root
    // on every platform — Linux: XDG_CONFIG_HOME / ~/.config; macOS: ~/Library;
    // Windows: %APPDATA%. The previous Linux branch prepended an EXTRA ".config",
    // yielding ~/.config/.config/Bombo/Presets. migrateLegacyUserPresets() moves
    // anything left at that old path here.
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    return base.getChildFile("Bombo/Presets");
}

void PresetBank::migrateLegacyUserPresets()
{
   #if JUCE_LINUX || JUCE_BSD
    auto base   = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    auto legacy = base.getChildFile(".config/Bombo/Presets");   // the old doubled path
    auto dest   = userPresetsDir();
    if (legacy == dest || ! legacy.isDirectory()) return;

    dest.createDirectory();
    juce::Array<juce::File> files;
    legacy.findChildFiles(files, juce::File::findFiles, false, "*.json");
    for (const auto& f : files)
    {
        auto target = dest.getChildFile(f.getFileName());
        if (! target.existsAsFile())   // never clobber a preset already at the new path
            f.moveFileTo(target);
    }
   #endif
}

juce::String PresetBank::sanitizeFilename(const juce::String& displayName)
{
    juce::String out;
    for (auto c : displayName)
    {
        const auto ch = static_cast<juce::juce_wchar>(c);
        if (juce::CharacterFunctions::isLetterOrDigit(ch))
            out += juce::String::charToString(juce::CharacterFunctions::toLowerCase(ch));
        else if (ch == ' ' || ch == '-' || ch == '_')
            out += '_';
        // anything else (`/`, `\`, control chars, punctuation) is dropped
    }
    while (out.startsWithChar('_')) out = out.substring(1);
    while (out.endsWithChar  ('_')) out = out.dropLastCharacters(1);
    if (out.length() > 48) out = out.substring(0, 48);
    return out;
}

} // namespace bombo
