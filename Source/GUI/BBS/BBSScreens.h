#pragma once
#include <functional>

namespace bombo
{

enum class BBSScreen { Intro, BoomFeed, MyDownloads, Game };

// Minimal state machine. BBSComponent drives it; sub-screen components
// own their own paint + keyboard logic.
class BBSScreens
{
public:
    BBSScreen current() const noexcept { return current_; }

    void transitionTo(BBSScreen s)
    {
        if (current_ == s) return;
        current_ = s;
        if (onTransition) onTransition(s);
    }

    // Called by the intro animation when typewriter completes.
    void onIntroComplete() { transitionTo(BBSScreen::BoomFeed); }

    std::function<void(BBSScreen)> onTransition;

private:
    BBSScreen current_ = BBSScreen::Intro;
};

} // namespace bombo
