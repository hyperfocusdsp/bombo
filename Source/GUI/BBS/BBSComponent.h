#pragma once
#include "BBSScreens.h"
#include "BBSLookAndFeel.h"
#include "BombImpactGame.h"
#include "ProgressionManager.h"
#include "BoomFeed.h"
#include "SysopContent.h"
#include "../Theme/ThemedComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#if BOMBO_GAME_V2
#include "Game/Game.h"
#include "Game/Framebuffer.h"
#include "Game/Palette.h"
#include "Game/Discovery.h"
#include "Game/HighScores.h"
#include "Game/SpriteData.h"
#include "../Theme/ThemeProvider.h"
#include <random>
#endif

namespace bombo
{

// Forward-declare so we avoid a circular include chain.
class PresetBank;

// Hidden 1992-aesthetic BBS terminal overlay. Sits as a sibling of the
// FaceplatePanel under BomboEditor, sized to getLocalBounds(), and is
// invisible by default. show() lifts it to the top of the z-order and
// requests keyboard focus; hide() drops it back. Esc dismisses.
//
// Wire-up sequence (called from BomboEditor constructor):
//   bbs_.setProgressionManager(&processorRef.progressionManager());
//   bbs_.setApvts(&processorRef.apvts);
//   bbs_.setTriggerCallback([...] { ... });
//   bbs_.setPresetBank(&processorRef.presetBank());
class BBSComponent : public juce::Component,
                     public bombo::ThemedComponent,
                     private juce::Timer
{
public:
    BBSComponent();
    ~BBSComponent() override;

    void setProgressionManager(ProgressionManager* pm) noexcept;
    void setApvts(juce::AudioProcessorValueTreeState* apvts) noexcept;
    void setTriggerCallback(std::function<void()> cb) noexcept;
    void setPresetBank(PresetBank* pb) noexcept;

    void show();
    void hide();

    std::function<void()> onShown;
    std::function<void()> onDismissed;
    std::function<void()> onPresetSaved; // fired after S-key save succeeds

    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void resized() override;

    // Expose screens for external state queries.
    BBSScreen currentScreen() const noexcept { return screens_.current(); }

    // Called by BomboEditor when bounds change — passes the chassis outline
    // in BBS-local coordinates so paint() can clip corners to the silhouette.
    void setLocalChassisClip(const juce::Path& p) { localChassisClip_ = p; repaint(); }

private:
    void timerCallback() override;

    void paintIntro       (juce::Graphics&);
    void paintBoomFeed    (juce::Graphics&);
    void paintMyDownloads (juce::Graphics&);
    void paintHeader      (juce::Graphics&, juce::Rectangle<int> area);
    void paintScrollerBar (juce::Graphics&, juce::Rectangle<int> area);

    void launchGame();
    void exitGame();

    ProgressionManager*         progression_ = nullptr;
    BBSScreens                  screens_;
    BBSLookAndFeel              lnf_;
    BoomFeed                    boomFeed_;
    BoomFeed::Mode              boomFeedMode_ = BoomFeed::Mode::Random;
    juce::Random                rng_;

    juce::AudioProcessorValueTreeState* apvts_      = nullptr;
    std::function<void()>               triggerCb_;
    PresetBank*                         presetBank_ = nullptr;

    // Intro animation
    int          introCharPos_  = 0;
    bool         introComplete_ = false;
    juce::String introText_;

    // Scroller — advances 1 char every 2 timer ticks (half the tick rate)
    int          scrollOffset_  = 0;
    int          scrollSubtick_ = 0;
    juce::String scrollerText_;

    // Chassis clip path in BBS-local coords — set by BomboEditor so paint()
    // can clip itself to the bomb silhouette (BBS rect can extend past the
    // chassis at the top-left/right corners where the egg-shape narrows).
    juce::Path   localChassisClip_;

    // SYSOP state
    int          currentSysopIdx_ = 0;
    juce::String currentMotd_;

    // Save confirmation — shown in place of MOTD for ~2 seconds after S
    juce::String saveStatusMsg_;
    juce::Time   saveStatusTime_;

    // Game
    BombImpactGame game_;
    juce::String   commandBuffer_;  // accumulates typed chars for "GAME" command
#if BOMBO_GAME_V2
    bombo::game::Game       gameV2_;
    bombo::game::Framebuffer gameV2Fb_;     // reused each frame — no alloc cost
    juce::Image             gameV2Image_;  // kFbW x kFbH ARGB, allocated in launchGame()
    int                     prePresetIdx_ = -1;  // stashed preset index restored on exit
    bool                    fireKeyWasDown_ = false;  // charge-key release edge (T/F/Space)

    // --- Discovery (Task 23): drifting BBS invader + cabinet glyph ---
    std::mt19937            discoveryRng_ { std::random_device{}() };
    bombo::game::Discovery  discovery_ { discoveryRng_ };
    // cabinetLit is persisted in the SAME store the game uses for high scores
    // (defaultHighScoresPath()), so the lit state survives restarts and lines up
    // with the firstInvaderSeenAt stamp that setCabinetLit() writes.
    bombo::game::HighScores cabinetStore_ { bombo::game::defaultHighScoresPath() };
    bool                    cabinetLit_ = false;
    // Cabinet glyph hit rect (BBS-local), set in paint() each frame when shown.
    juce::Rectangle<int>    cabinetRect_;
    // Scope-strip rect (BBS-local) the invader's logical coords map into.
    juce::Rectangle<int>    invaderStripRect_;

    void paintDiscovery(juce::Graphics&, juce::Rectangle<int> headerArea,
                        juce::Rectangle<int> stripArea);
#endif

    // Konami code detector — runs in parallel, doesn't consume individual keys
    static constexpr int kKonamiLen = 8;
    static const int kKonamiSeq[kKonamiLen]; // defined in .cpp
    int konamiPos_ = 0;

    // My Downloads selection
    int myDownloadsSelected_ = 0;

    void buildIntroText();
    void buildScrollerText();
    void refreshSysopVoice();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BBSComponent)
};

} // namespace bombo
