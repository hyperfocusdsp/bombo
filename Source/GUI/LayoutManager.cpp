#include "LayoutManager.h"

namespace bombo
{

LayoutManager::LayoutManager()
{
    reload();
}

juce::File LayoutManager::resolveFile() const
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("BOMBO_LAYOUT_JSON", {});
        env.isNotEmpty())
    {
        juce::File f (env);
        if (f.existsAsFile())
            return f;
    }

    auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (int i = 0; i < 8 && dir.exists(); ++i)
    {
        auto candidate = dir.getChildFile ("Resources/Layout.json");
        if (candidate.existsAsFile())
            return candidate;
        dir = dir.getParentDirectory();
    }

    return {};
}

void LayoutManager::reload()
{
    loaded = false;
    sourcePath = {};
    root = juce::var();

    auto file = resolveFile();
    if (! file.existsAsFile())
        return;

    auto parsed = juce::JSON::parse (file);
    if (! parsed.isObject())
        return;

    root = parsed;
    sourcePath = file.getFullPathName();
    loaded = true;
}

static juce::StringArray splitId (const juce::String& id)
{
    juce::StringArray parts;
    parts.addTokens (id, ".", "");
    parts.removeEmptyStrings();
    return parts;
}

juce::Rectangle<int> LayoutManager::boundsOr (const juce::String& id,
                                              juce::Rectangle<int> fallback) const
{
    if (! loaded)
        return fallback;

    juce::var node = root;
    int start = 0;
    for (int i = 0; i <= id.length(); ++i)
    {
        if (i == id.length() || id[i] == '.')
        {
            auto key = id.substring (start, i);
            if (! node.isObject())
                return fallback;
            node = node.getProperty (key, juce::var());
            start = i + 1;
        }
    }

    if (! node.isObject())
        return fallback;

    auto x = (int) node.getProperty ("x", fallback.getX());
    auto y = (int) node.getProperty ("y", fallback.getY());
    auto w = (int) node.getProperty ("w", fallback.getWidth());
    auto h = (int) node.getProperty ("h", fallback.getHeight());
    return { x, y, w, h };
}

namespace
{
    struct LeafPath
    {
        juce::DynamicObject::Ptr parent;
        juce::String key;
    };

    LeafPath ensureLeafPath (juce::var& root, const juce::StringArray& parts)
    {
        juce::DynamicObject::Ptr rootObj;
        if (root.isObject())
            rootObj = root.getDynamicObject();
        if (rootObj == nullptr)
        {
            rootObj = new juce::DynamicObject();
            root = juce::var (rootObj.get());
        }

        juce::DynamicObject::Ptr node = rootObj;
        for (int i = 0; i < parts.size() - 1; ++i)
        {
            auto child = node->getProperty (parts[i]);
            juce::DynamicObject::Ptr childObj;
            if (child.isObject())
                childObj = child.getDynamicObject();
            if (childObj == nullptr)
            {
                childObj = new juce::DynamicObject();
                node->setProperty (parts[i], juce::var (childObj.get()));
            }
            node = childObj;
        }
        return { node, parts[parts.size() - 1] };
    }

    juce::DynamicObject::Ptr ensureLeafObject (juce::var& root, const juce::StringArray& parts)
    {
        auto path = ensureLeafPath (root, parts);
        if (path.parent == nullptr) return nullptr;
        auto existing = path.parent->getProperty (path.key);
        juce::DynamicObject::Ptr leafObj;
        if (existing.isObject())
            leafObj = existing.getDynamicObject();
        if (leafObj == nullptr)
        {
            leafObj = new juce::DynamicObject();
            path.parent->setProperty (path.key, juce::var (leafObj.get()));
        }
        return leafObj;
    }
}

void LayoutManager::setBounds (const juce::String& id, juce::Rectangle<int> r,
                               const juce::String& type)
{
    auto parts = splitId (id);
    if (parts.isEmpty()) return;

    auto leafObj = ensureLeafObject (root, parts);
    if (leafObj == nullptr) return;

    leafObj->setProperty ("x", r.getX());
    leafObj->setProperty ("y", r.getY());
    leafObj->setProperty ("w", r.getWidth());
    leafObj->setProperty ("h", r.getHeight());
    if (type.isNotEmpty())
        leafObj->setProperty ("type", type);

    loaded = true;
}

bool LayoutManager::isLocked (const juce::String& id) const
{
    if (! loaded) return false;

    juce::var node = root;
    int start = 0;
    for (int i = 0; i <= id.length(); ++i)
    {
        if (i == id.length() || id[i] == '.')
        {
            auto key = id.substring (start, i);
            if (! node.isObject()) return false;
            node = node.getProperty (key, juce::var());
            start = i + 1;
        }
    }
    if (! node.isObject()) return false;
    return (bool) node.getProperty ("locked", false);
}

void LayoutManager::setLocked (const juce::String& id, bool locked)
{
    auto parts = splitId (id);
    if (parts.isEmpty()) return;

    auto leafObj = ensureLeafObject (root, parts);
    if (leafObj == nullptr) return;

    if (locked)
        leafObj->setProperty ("locked", true);
    else
        leafObj->removeProperty ("locked");

    loaded = true;
}

bool LayoutManager::save()
{
    juce::File target;
    if (sourcePath.isNotEmpty())
        target = juce::File (sourcePath);
    else
        target = resolveFile();

    if (target == juce::File())
    {
        auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 8 && dir.exists(); ++i)
        {
            auto res = dir.getChildFile ("Resources");
            if (res.isDirectory())
            {
                target = res.getChildFile ("Layout.json");
                break;
            }
            dir = dir.getParentDirectory();
        }
    }

    if (target == juce::File())
        return false;

    target.getParentDirectory().createDirectory();
    auto json = juce::JSON::toString (root, true);
    if (! target.replaceWithText (json))
        return false;

    sourcePath = target.getFullPathName();
    return true;
}

} // namespace bombo
