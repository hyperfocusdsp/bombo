// Source/GUI/BBS/Game/HighScores.h
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <map>

namespace bombo::game
{
    struct ScoreEntry
    {
        juce::String initials;
        int          score = 0;
        int          wave  = 0;
        juce::String date;       // YYYY-MM-DD
        bool         daily = false;
        uint32_t     seed  = 0;  // 0 if non-daily
    };

    class HighScores
    {
    public:
        explicit HighScores(juce::File path);
        void load();
        void save();

        bool qualifiesForTopTen(int score) const;
        void recordRun(const ScoreEntry& e);   // inserts + sorts + caps at 10 (in memory; call save() to persist)

        const std::vector<ScoreEntry>& topTen() const noexcept { return top_; }

        bool isCabinetLit() const noexcept { return cabinetLit_; }
        void setCabinetLit(bool v);             // persists immediately

    private:
        juce::File path_;
        std::vector<ScoreEntry> top_;
        std::map<juce::String, ScoreEntry> dailyBest_;   // date -> best (reserved for future use)
        bool cabinetLit_ = false;
        juce::String firstInvaderAt_;
    };

    uint32_t  dailySeedToday() noexcept;
    juce::File defaultHighScoresPath();    // userApplicationDataDirectory / Bombo / HighScores.json
}
