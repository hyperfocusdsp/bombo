#pragma once
#include "BBSScreens.h"
#include "BBSLookAndFeel.h"
#include "ProgressionManager.h"
#include "BoomFeed.h"
#include "SysopContent.h"
#include "../Theme/ThemedComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

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

    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void resized() override;

    // Expose screens for external state queries.
    BBSScreen currentScreen() const noexcept { return screens_.current(); }

private:
    void timerCallback() override;

    void paintIntro       (juce::Graphics&);
    void paintBoomFeed    (juce::Graphics&);
    void paintMyDownloads (juce::Graphics&);
    void paintHeader      (juce::Graphics&, juce::Rectangle<int> area);
    void paintScrollerBar (juce::Graphics&, juce::Rectangle<int> area);

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

    // Scroller
    int          scrollOffset_ = 0;
    juce::String scrollerText_;

    // SYSOP state
    int          currentSysopIdx_ = 0;
    juce::String currentMotd_;

    // Save confirmation — shown in place of MOTD for ~2 seconds after S
    juce::String saveStatusMsg_;
    juce::Time   saveStatusTime_;

    // My Downloads selection
    int myDownloadsSelected_ = 0;

    void buildIntroText();
    void buildScrollerText();
    void refreshSysopVoice();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BBSComponent)
};

} // namespace bombo
