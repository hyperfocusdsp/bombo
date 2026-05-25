// Source/GUI/BBS/Game/HighScores.cpp
#include "HighScores.h"
#include <algorithm>
#include <chrono>
#include <ctime>

namespace bombo::game
{
    juce::File defaultHighScoresPath()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("Bombo");
        dir.createDirectory();
        return dir.getChildFile("HighScores.json");
    }

    uint32_t dailySeedToday() noexcept
    {
        const auto now = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        return static_cast<uint32_t>((tm.tm_year + 1900) * 10000
                                     + (tm.tm_mon + 1) * 100
                                     + tm.tm_mday);
    }

    HighScores::HighScores(juce::File p) : path_(std::move(p)) {}

    void HighScores::load()
    {
        if (! path_.existsAsFile()) return;
        const auto parsed = juce::JSON::parse(path_);
        if (! parsed.isObject()) return;

        top_.clear();
        if (auto* arr = parsed["scores"].getArray())
        {
            for (const auto& v : *arr)
            {
                ScoreEntry e;
                e.initials = v["initials"].toString();
                e.score    = (int) v["score"];
                e.wave     = (int) v["wave"];
                e.date     = v["date"].toString();
                e.daily    = (bool) v["daily"];
                e.seed     = (uint32_t)(int) v["seed"];
                e.won      = (bool) v["won"];        // absent in old files → false
                e.ngPlus   = (int)  v["ngPlus"];     // absent → 0
                top_.push_back(e);
            }
        }
        cabinetLit_     = (bool) parsed["discoveryFlags"]["cabinetLit"];
        firstInvaderAt_ = parsed["discoveryFlags"]["firstInvaderSeenAt"].toString();
        maxNgPlus_      = (int)  parsed["maxNgPlus"];   // absent in old files → 0
    }

    void HighScores::save()
    {
        if (path_ == juce::File{}) return;   // in-memory only

        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("schemaVersion", 1);

        juce::Array<juce::var> arr;
        for (const auto& e : top_)
        {
            juce::DynamicObject::Ptr eo = new juce::DynamicObject();
            eo->setProperty("initials", e.initials);
            eo->setProperty("score",    e.score);
            eo->setProperty("wave",     e.wave);
            eo->setProperty("date",     e.date);
            eo->setProperty("daily",    e.daily);
            eo->setProperty("seed",     (int) e.seed);
            eo->setProperty("won",      e.won);
            eo->setProperty("ngPlus",   e.ngPlus);
            arr.add(juce::var(eo.get()));
        }
        root->setProperty("scores", arr);

        juce::DynamicObject::Ptr flags = new juce::DynamicObject();
        flags->setProperty("cabinetLit", cabinetLit_);
        flags->setProperty("firstInvaderSeenAt", firstInvaderAt_);
        root->setProperty("discoveryFlags", juce::var(flags.get()));
        root->setProperty("maxNgPlus", maxNgPlus_);

        path_.replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
    }

    bool HighScores::qualifiesForTopTen(int score) const
    {
        if ((int) top_.size() < 10) return true;
        return score > top_.back().score;
    }

    void HighScores::recordRun(const ScoreEntry& e)
    {
        top_.push_back(e);
        std::sort(top_.begin(), top_.end(),
                  [](const ScoreEntry& a, const ScoreEntry& b) { return a.score > b.score; });
        if (top_.size() > 10) top_.resize(10);
    }

    void HighScores::setMaxNgPlus(int tier)
    {
        if (tier > maxNgPlus_)
        {
            maxNgPlus_ = tier;
            save();
        }
    }

    void HighScores::setCabinetLit(bool v)
    {
        if (v && ! cabinetLit_)
            firstInvaderAt_ = juce::Time::getCurrentTime().toISO8601(true);
        cabinetLit_ = v;
        save();
    }
}
