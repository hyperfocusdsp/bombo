#include "BBSComponent.h"
#include "../Colours.h"
#include "../Fonts.h"
#include "../../State/PresetBank.h"
#include "../../PluginProcessor.h"
#include <juce_core/juce_core.h>

namespace bombo
{

// Konami sequence: ↑↑↓↓←→←→
const int BBSComponent::kKonamiSeq[BBSComponent::kKonamiLen] = {
    juce::KeyPress::upKey, juce::KeyPress::upKey,
    juce::KeyPress::downKey, juce::KeyPress::downKey,
    juce::KeyPress::leftKey, juce::KeyPress::rightKey,
    juce::KeyPress::leftKey, juce::KeyPress::rightKey,
};

// Pick the best available terminal font at first call, cache for the session.
// Prefers JetBrains Mono (Nerd Font Mono variant for true fixed-width cells);
// falls back to Courier New on systems where it's not installed.
static const juce::String& bbsFontName()
{
    static const juce::String name = []() -> juce::String {
        for (const auto& n : juce::Font::findAllTypefaceNames())
            if (n.containsIgnoreCase("JetBrainsMono") || n.startsWithIgnoreCase("JetBrains Mono"))
                return n;
        return "Courier New";  // safe ASCII fallback, no recursion
    }();
    return name;
}

BBSComponent::BBSComponent()
{
    setOpaque(false);
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);
    setVisible(false);
    setLookAndFeel(&lnf_);

    screens_.onTransition = [this](BBSScreen s)
    {
        if (s == BBSScreen::BoomFeed) refreshSysopVoice();
        repaint();
    };

#if BOMBO_GAME_V2
    // Load persisted cabinet-lit state so a returning user keeps the powered-on
    // cabinet glyph. Same JSON store the game's high scores live in.
    cabinetStore_.load();
    cabinetLit_ = cabinetStore_.isCabinetLit();
#endif
}

BBSComponent::~BBSComponent()
{
    setLookAndFeel(nullptr);
}

void BBSComponent::setProgressionManager(ProgressionManager* pm) noexcept
{
    progression_ = pm;
}

void BBSComponent::setApvts(juce::AudioProcessorValueTreeState* apvts) noexcept
{
    apvts_ = apvts;
    boomFeed_.setApvts(apvts);
}

void BBSComponent::setTriggerCallback(std::function<void()> cb) noexcept
{
    triggerCb_ = cb;
    boomFeed_.setTriggerCallback(cb);
}

void BBSComponent::setPresetBank(PresetBank* pb) noexcept
{
    presetBank_ = pb;
}

void BBSComponent::show()
{
    introCharPos_        = 0;
    introComplete_       = false;
    myDownloadsSelected_ = 0;
    buildIntroText();
    buildScrollerText();
    refreshSysopVoice();
    screens_.transitionTo(BBSScreen::Intro);

    // 200 ms alpha fade-in — feels less jarring than a hard cut, especially
    // when the BBS comes up over a running kick. ComponentAnimator handles
    // setVisible(true) + alpha 0→1 internally.
    setAlpha(0.0f);
    setVisible(true);
    juce::Desktop::getInstance().getAnimator().fadeIn(this, 200);
    toFront(true);
    grabKeyboardFocus();
    startTimer(40);

    if (onShown) onShown();
}

void BBSComponent::hide()
{
    // Fade out 200 ms then drop visibility. Keep the intro timer running
    // until the fade is done so the scroll/intro animation doesn't freeze
    // mid-frame as it's disappearing.
    juce::Desktop::getInstance().getAnimator().fadeOut(this, 200);
    juce::Timer::callAfterDelay(220, [safe = juce::Component::SafePointer<BBSComponent>(this)]
    {
        if (safe == nullptr) return;
        safe->stopTimer();
        safe->setVisible(false);
        safe->setAlpha(1.0f);  // restore for next show()
        if (safe->onDismissed) safe->onDismissed();
        if (auto* p = safe->getParentComponent())
            p->grabKeyboardFocus();
    });
}

void BBSComponent::resized() {}

void BBSComponent::timerCallback()
{
    if (screens_.current() == BBSScreen::Game)
    {
#if BOMBO_GAME_V2
        // Feed host BPM from the processor into the game each tick.
        // apvts_->processor is juce::AudioProcessor&; cast to BomboProcessor
        // to reach the atomic hostBpm() accessor populated in processBlock.
        // Falls back to kBpmRef (120) if apvts_ is null (standalone / early init).
        if (apvts_ != nullptr)
        {
            auto& proc = static_cast<BomboProcessor&>(apvts_->processor);
            float reportedBpm = proc.hostBpm();
            // hostBpm() returns 0 when host doesn't provide BPM (standalone);
            // keep the previous value / default in that case.
            if (reportedBpm > 0.0f)
                gameV2_.setHostBpm(reportedBpm);
        }
        gameV2_.tick();
        if (gameV2_.wantsExit()) exitGame();
#else
        game_.tick();
        if (game_.wantsExit()) exitGame();
#endif
        repaint();
        return;
    }

    if (screens_.current() == BBSScreen::Intro && !introComplete_)
    {
        introCharPos_ += 2;
        if (introCharPos_ >= introText_.length())
        {
            introComplete_ = true;
            screens_.onIntroComplete();
        }
    }
    // Advance scroller every other tick (half speed — 80 ms per character).
    if (++scrollSubtick_ >= 2)
    {
        scrollSubtick_ = 0;
        scrollOffset_ = (scrollOffset_ + 1) % juce::jmax(1, scrollerText_.length());
    }

#if BOMBO_GAME_V2
    // Discovery: advance the invader spawn clock + drift while the BBS overlay
    // is shown and we're NOT inside the game. Timer fires every 40 ms (25 Hz).
    {
        const bool bbsVisible = isVisible() && screens_.current() != BBSScreen::Game;
        discovery_.tick(0.04f, bbsVisible);

        if (discovery_.consumePoweredOnEvent())
        {
            // First invader ever seen — power on the cabinet glyph and persist it.
            // setCabinetLit() saves immediately and stamps firstInvaderSeenAt.
            // Reload from disk first so we merge any scores the game wrote this
            // session — otherwise setCabinetLit()'s full-document save would
            // clobber them with cabinetStore_'s stale in-memory top_.
            cabinetLit_ = true;
            cabinetStore_.load();
            cabinetStore_.setCabinetLit(true);
        }
    }
#endif

    repaint();
}

bool BBSComponent::keyPressed(const juce::KeyPress& key)
{
    // Game screen intercepts all input. ESC exits game (not the whole BBS).
    if (screens_.current() == BBSScreen::Game)
    {
        if (key == juce::KeyPress::escapeKey) { exitGame(); return true; }
#if BOMBO_GAME_V2
        return gameV2_.handleKey(key.getKeyCode(), key.getModifiers());
#else
        return game_.keyPressed(key);
#endif
    }

    if (key == juce::KeyPress::escapeKey) { hide(); return true; }

    // T fires the kick from any BBS screen — Intro/BoomFeed/MyDownloads.
    // Mirrors the main editor's T shortcut so muscle memory carries over.
    {
        const auto chTop = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        if (chTop == 't')
        {
            if (triggerCb_) triggerCb_();
            return true;
        }
    }

    // Intro skip — transition to BoomFeed first so that Konami + command
    // buffer below see the correct screen on the very same keypress.
    if (screens_.current() == BBSScreen::Intro)
    {
        introComplete_ = true;
        screens_.onIntroComplete(); // transitions to BoomFeed
        // Don't return — N/P fall through to the BoomFeed block so one
        // keypress both skips the intro AND applies the navigation action.
        // Any other key is swallowed at the end of the function.
    }

    // Konami detector (up-up-down-down-left-right-left-right).
    // Runs after any intro skip so pressing up to skip intro also counts
    // as the first Konami step. Matched keys consumed to avoid side effects.
    if (screens_.current() == BBSScreen::BoomFeed)
    {
        if (key == juce::KeyPress(kKonamiSeq[konamiPos_]))
        {
            if (++konamiPos_ >= kKonamiLen)
            {
                konamiPos_ = 0;
#if !BOMBO_GAME_V2
                game_.hyperfocusModeActive = true;
#endif
                launchGame();
            }
            return true; // consume matched key
        }
        konamiPos_ = 0; // mismatch — reset, fall through
    }

    if (key == juce::KeyPress::tabKey)
    {
        if (screens_.current() == BBSScreen::BoomFeed)
            screens_.transitionTo(BBSScreen::MyDownloads);
        else if (screens_.current() == BBSScreen::MyDownloads)
            screens_.transitionTo(BBSScreen::BoomFeed);
        return true;
    }

    if (screens_.current() == BBSScreen::BoomFeed)
    {
        // Single-key shortcut: G launches the game immediately.
        // Also accumulate a buffer — typing "GAME" fully works too.
        const auto chUp = juce::CharacterFunctions::toUpperCase(key.getTextCharacter());
        if (chUp == 'G') { commandBuffer_.clear(); launchGame(); return true; }
        if (chUp >= 'A' && chUp <= 'Z')
        {
            commandBuffer_ += chUp;
            if (commandBuffer_.length() > 8)
                commandBuffer_ = commandBuffer_.substring(commandBuffer_.length() - 8);
            if (commandBuffer_.endsWith("GAME"))
            {
                commandBuffer_.clear();
                launchGame();
                return true;
            }
        }

        const auto ch = key.getTextCharacter();
        if (ch == 'n' || ch == 'N')
        {
            boomFeed_.advance(boomFeedMode_);
            repaint();
            return true;
        }
        if (ch == 'p' || ch == 'P')
        {
            boomFeed_.prev();
            repaint();
            return true;
        }
        if (ch == 'f' || ch == 'F')
        {
            // Forward through history (recover after pressing P too far).
            // Distinct from N because N always generates a fresh kick and
            // truncates the redo stack -- F just navigates.
            boomFeed_.next();
            repaint();
            return true;
        }
        // Space deliberately NOT handled here — falls through to the editor's
        // keyPressed so BBS inherits the main window's transport semantics
        // (space toggles loop in main, must toggle loop in BBS too). T still
        // fires a one-shot via the handler above. User-reported regression
        // 2026-05-24: looping kick couldn't be stopped from inside BBS.
        if (ch == 's' || ch == 'S')
        {
            if (apvts_ != nullptr && presetBank_ != nullptr)
            {
                const auto name = boomFeed_.currentFilename()
                                      .upToLastOccurrenceOf(".KCK", false, true);
                const int newIdx = presetBank_->saveAs(name, *apvts_);
                if (newIdx >= 0)
                {
                    if (progression_ != nullptr) progression_->onKickSaved();
                    if (onPresetSaved) onPresetSaved();
                    saveStatusMsg_ = ">> PRESET SAVED: " + name + " <<";
                }
                else
                {
                    // saveAs returns -1 for: empty sanitised name, name clash
                    // with an existing user preset, or any disk-write failure.
                    saveStatusMsg_ = ">> SAVE FAILED: " + name
                                   + " -- ALREADY EXISTS OR DISK ERROR <<";
                }
                saveStatusTime_ = juce::Time::getCurrentTime();
                repaint();
            }
            return true;
        }
        if (ch == 'r' || ch == 'R')
        {
            // R = generate a new purely random kick (ignores current mode setting)
            boomFeed_.advance(BoomFeed::Mode::Random);
            repaint();
            return true;
        }
        if (ch == 'm' || ch == 'M')
        {
            boomFeedMode_ = BoomFeed::Mode::Mutate;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::leftKey)
        {
            boomFeedMode_ = BoomFeed::Mode::Random;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::rightKey)
        {
            boomFeedMode_ = BoomFeed::Mode::Mutate;
            repaint();
            return true;
        }
    }

    if (screens_.current() == BBSScreen::MyDownloads && presetBank_ != nullptr)
    {
        const auto countUserPresets = [&]() {
            int n = 0;
            for (int i = 0; i < presetBank_->size(); ++i)
                if (presetBank_->at(i).source == PresetBank::Source::User) ++n;
            return n;
        };

        if (key == juce::KeyPress::upKey)
        {
            myDownloadsSelected_ = juce::jmax(0, myDownloadsSelected_ - 1);
            repaint(); return true;
        }
        if (key == juce::KeyPress::downKey)
        {
            const int n = countUserPresets();
            if (n > 0)
                myDownloadsSelected_ = juce::jmin(n - 1, myDownloadsSelected_ + 1);
            repaint(); return true;
        }
        if (key == juce::KeyPress::returnKey && apvts_ != nullptr)
        {
            int row = 0;
            for (int i = 0; i < presetBank_->size(); ++i)
            {
                if (presetBank_->at(i).source != PresetBank::Source::User) continue;
                if (row == myDownloadsSelected_)
                {
                    presetBank_->applyByIndex(i, *apvts_);
                    if (triggerCb_) triggerCb_();
                    break;
                }
                ++row;
            }
            repaint(); return true;
        }
        if (key == juce::KeyPress::deleteKey)
        {
            int row = 0;
            for (int i = 0; i < presetBank_->size(); ++i)
            {
                if (presetBank_->at(i).source != PresetBank::Source::User) continue;
                if (row == myDownloadsSelected_)
                {
                    presetBank_->deleteAt(i);
                    const int remaining = [&]() {
                        int n = 0;
                        for (int j = 0; j < presetBank_->size(); ++j)
                            if (presetBank_->at(j).source == PresetBank::Source::User) ++n;
                        return n;
                    }();
                    myDownloadsSelected_ = (remaining == 0) ? -1
                                                             : juce::jmax(0, myDownloadsSelected_ - 1);
                    break;
                }
                ++row;
            }
            repaint(); return true;
        }
    }

    return true;  // swallow all keys while BBS is open
}

void BBSComponent::paint(juce::Graphics& g)
{
    // Clip to the bomb silhouette so the BBS rect corners (which can
    // extend outside the chassis egg-shape at the top) stay hidden.
    if (! localChassisClip_.isEmpty())
        g.reduceClipRegion(localChassisClip_);

    // Fill with a rounded rect inset 1px from edges so the hard dark
    // boundary doesn't bleed into the orange nose section below.
    g.setColour(juce::Colour(0xFF0A0A0Au).withAlpha(0.94f));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.0f, 1.0f), 2.0f);

    auto b = getLocalBounds();

    switch (screens_.current())
    {
        case BBSScreen::Intro:
            paintIntro(g);
            break;

        case BBSScreen::BoomFeed:
        {
            const int headerH   = 22;
            const int scrollerH = 16;
            const int footerH   = 14;
            const auto headerArea = b.removeFromTop(headerH);
            paintHeader    (g, headerArea);
            b.removeFromBottom(footerH);
            const auto stripArea = b.removeFromBottom(scrollerH);
            paintScrollerBar(g, stripArea);
            paintBoomFeed  (g);
#if BOMBO_GAME_V2
            paintDiscovery(g, headerArea, stripArea);
#endif
            break;
        }

        case BBSScreen::MyDownloads:
        {
            const int headerH   = 22;
            const int scrollerH = 16;
            const int footerH   = 14;
            const auto headerArea = b.removeFromTop(headerH);
            paintHeader     (g, headerArea);
            b.removeFromBottom(footerH);
            const auto stripArea = b.removeFromBottom(scrollerH);
            paintScrollerBar(g, stripArea);
            paintMyDownloads(g);
#if BOMBO_GAME_V2
            paintDiscovery(g, headerArea, stripArea);
#endif
            break;
        }

        case BBSScreen::Game:
        {
            const int headerH = 22;
            paintHeader(g, b.removeFromTop(headerH));
#if BOMBO_GAME_V2
            {
                // Render the v2 160x112 framebuffer, then blit it scaled with
                // nearest-neighbour into the available game area (b), letterboxed
                // to preserve the exact 160:112 aspect ratio.
                const auto pal = bombo::game::getGamePalette(
                    bombo::ThemeProvider::get().activeName());
                gameV2Fb_.clear(0);
                gameV2_.renderInto(gameV2Fb_, pal);
                gameV2Fb_.resolveToARGB(gameV2Image_, pal);

                const float fbAspect  = static_cast<float>(bombo::game::kFbW)
                                      / static_cast<float>(bombo::game::kFbH);
                const float dstAspect = static_cast<float>(b.getWidth())
                                      / static_cast<float>(b.getHeight());
                int dstW, dstH;
                if (fbAspect >= dstAspect)
                {
                    dstW = b.getWidth();
                    dstH = juce::roundToInt(static_cast<float>(dstW) / fbAspect);
                }
                else
                {
                    dstH = b.getHeight();
                    dstW = juce::roundToInt(static_cast<float>(dstH) * fbAspect);
                }
                const int dstX = b.getX() + (b.getWidth()  - dstW) / 2;
                const int dstY = b.getY() + (b.getHeight() - dstH) / 2;

                g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
                g.drawImage(gameV2Image_,
                            dstX, dstY, dstW, dstH,
                            0, 0, bombo::game::kFbW, bombo::game::kFbH);
            }
#else
            game_.paint(g, b);
#endif
            break;
        }
    }

    // Footer key hints (painted last so it's always visible)
    const auto footerR = getLocalBounds().removeFromBottom(14);
    g.setColour(juce::Colour(0xFF111111u));
    g.fillRect(footerR);
    g.setFont(juce::Font(juce::FontOptions(bbsFontName(), 13.0f, juce::Font::plain)));
    g.setColour(juce::Colour(0xFF555555u));
    const juce::String hints = (screens_.current() == BBSScreen::BoomFeed)
        ? "[ TAB = MY DOWNLOADS ]  [ ESC = EXIT ]"
        : "[ TAB = BOOM FEED ]  [ ESC = EXIT ]";
    g.drawText(hints, footerR, juce::Justification::centred);
}

void BBSComponent::paintHeader(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xFF111111u));
    g.fillRect(area);
    g.setColour(juce::Colour(0xFF333333u));
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    g.setFont(juce::Font(juce::FontOptions(bbsFontName(), 14.0f, juce::Font::plain)));
    g.setColour(juce::Colour(0xFFFFE066u));
    g.drawText("HYPERFOCUS BBS v2.3",
               area.reduced(8, 0), juce::Justification::centredLeft);

    if (currentSysopIdx_ >= 0 && currentSysopIdx_ < kSysopCount)
    {
        const auto& sysop = kSysops[currentSysopIdx_];
        g.setColour(juce::Colour(0xFF888888u));
        g.drawText(juce::String("SYSOP: ") + sysop.name,
                   area.reduced(8, 0), juce::Justification::centredRight);
    }
}

void BBSComponent::paintScrollerBar(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xFF0D0D0Du));
    g.fillRect(area);
    g.setColour(juce::Colour(0xFF444444u));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 1);

    if (scrollerText_.isEmpty()) return;

    const juce::String visible = scrollerText_.substring(scrollOffset_)
                               + scrollerText_.substring(0, scrollOffset_);

    g.setFont(juce::Font(juce::FontOptions(bbsFontName(), 13.0f, juce::Font::plain)));
    g.setColour(juce::Colour(0xFF555555u));
    g.drawText(juce::String("> ") + visible,
               area.reduced(6, 0), juce::Justification::centredLeft, false);
}

void BBSComponent::paintIntro(juce::Graphics& g)
{
    const auto b = getLocalBounds().reduced(30, 20);
    g.setFont(juce::Font(juce::FontOptions(bbsFontName(), 15.0f, juce::Font::plain)));
    g.setColour(juce::Colour(0xFFC8FF8Cu));
    g.drawMultiLineText(introText_.substring(0, introCharPos_),
                        b.getX(), b.getY() + 16, b.getWidth());
}

void BBSComponent::paintBoomFeed(juce::Graphics& g)
{
    const auto b = getLocalBounds()
                       .withTrimmedTop(22)
                       .withTrimmedBottom(30)
                       .reduced(12, 8);

    g.setFont(juce::Font(juce::FontOptions(bbsFontName(), 14.0f, juce::Font::plain)));

    auto area = b;

    g.setColour(juce::Colour(0xFF444444u));
    g.drawText("-- KICK ROM BROWSER ------------",
               area.removeFromTop(18), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xFFC8FF8Cu));
    g.drawText("FILENAME : " + boomFeed_.currentFilename(),
               area.removeFromTop(20), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xFF888888u));
    g.drawText("SIZE     : 3.1 KB",
               area.removeFromTop(20), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xFFFFE066u));
    g.drawText("WAVEFORM : " + boomFeed_.currentWaveformAscii(),
               area.removeFromTop(20), juce::Justification::centredLeft);

    area.removeFromTop(8);

    g.setColour(juce::Colour(0xFF555555u));
    g.drawText("[ N ] NEW  [ P ] PREV  [ F ] FWD  [ SPACE ] PLAY  [ S ] SAVE",
               area.removeFromTop(18), juce::Justification::centredLeft);
    g.drawText("[ R ] RANDOM  [ M ] MUTATE  [ < > ] SWITCH",
               area.removeFromTop(18), juce::Justification::centredLeft);

    const bool isRandom = (boomFeedMode_ == BoomFeed::Mode::Random);
    g.setColour(juce::Colours::white);
    g.drawText(juce::String("MODE: ")
               + (isRandom ? "[> RANDOM] / [ MUTATE]"
                           : "[ RANDOM] / [> MUTATE]"),
               area.removeFromTop(20), juce::Justification::centredLeft);

    area.removeFromTop(8);
    g.setColour(juce::Colour(0xFF444444u));
    g.fillRect(area.removeFromTop(1));
    g.setColour(juce::Colour(0xFFC8FF8Cu));
    // Show save confirmation for 2s, then revert to MOTD.
    const bool showSave = !saveStatusMsg_.isEmpty()
        && (juce::Time::getCurrentTime() - saveStatusTime_).inSeconds() < 2.0;
    if (showSave)
    {
        g.setColour(juce::Colour(0xFF88FF88u));  // bright green ACK
        g.drawFittedText(saveStatusMsg_, area.removeFromTop(26),
                         juce::Justification::topLeft, 2);
    }
    else
    {
        if (showSave == false && !saveStatusMsg_.isEmpty())
            saveStatusMsg_ = {};
        g.drawFittedText("MOTD: " + currentMotd_,
                         area.removeFromTop(26),
                         juce::Justification::topLeft, 2);
    }
}

void BBSComponent::paintMyDownloads(juce::Graphics& g)
{
    if (presetBank_ == nullptr) return;

    const auto b = getLocalBounds()
                       .withTrimmedTop(22)
                       .withTrimmedBottom(30)
                       .reduced(12, 8);

    g.setFont(juce::Font(juce::FontOptions(bbsFontName(), 14.0f, juce::Font::plain)));

    auto area = b;

    int userCount = 0;
    for (int i = 0; i < presetBank_->size(); ++i)
        if (presetBank_->at(i).source == PresetBank::Source::User) ++userCount;

    g.setColour(juce::Colours::white);
    g.drawText("MY DOWNLOADS" +
               juce::String("                        ") +
               juce::String(userCount) + " FILES",
               area.removeFromTop(16), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xFF444444u));
    g.drawText("NAME                      SIZE     DATE       TIME",
               area.removeFromTop(14), juce::Justification::centredLeft);
    g.fillRect(area.removeFromTop(1));

    const int rowH = 15;
    int row = 0;
    for (int i = 0; i < presetBank_->size(); ++i)
    {
        const auto& p = presetBank_->at(i);
        if (p.source != PresetBank::Source::User) continue;

        const bool selected = (row == myDownloadsSelected_);
        if (selected)
        {
            g.setColour(juce::Colour(0xFF1A3A1Au));
            g.fillRect(area.getX(), area.getY(), area.getWidth(), rowH);
        }

        const juce::String prefix = selected ? "> " : "  ";
        const juce::String name   = juce::String(p.displayName).paddedRight(' ', 26);
        const juce::String size   = juce::String("3.1KB").paddedRight(' ', 9);
        const juce::String date   = p.filePath.exists()
            ? p.filePath.getLastModificationTime().toString(false, false, false, false).substring(0, 5)
            : "??-??";
        const juce::String timeStr = p.filePath.exists()
            ? p.filePath.getLastModificationTime().toString(false, true, false, true).substring(0, 5)
            : "??:??";

        g.setColour(selected ? juce::Colours::white : juce::Colour(0xFFAAAAAA));
        g.drawText(prefix + name + size + date + "    " + timeStr,
                   area.removeFromTop(rowH), juce::Justification::centredLeft);
        ++row;
    }

    if (row == 0)
    {
        g.setColour(juce::Colour(0xFF444444u));
        g.drawText("  (NO DOWNLOADS YET -- PRESS N IN BOOM FEED TO BROWSE)",
                   area.removeFromTop(rowH), juce::Justification::centredLeft);
    }
}

#if BOMBO_GAME_V2
void BBSComponent::paintDiscovery(juce::Graphics& g,
                                  juce::Rectangle<int> headerArea,
                                  juce::Rectangle<int> stripArea)
{
    const auto pal = bombo::game::getGamePalette(
        bombo::ThemeProvider::get().activeName());

    // --- Cabinet glyph: top-right of the header, dim if unlit, bright if lit ---
    // kCabinet is 12 wide x 16 tall. Scale to fit the 22px header height.
    constexpr int cbW = 12, cbH = 16;
    const int cbScale = juce::jmax(1, (headerArea.getHeight() - 2) / cbH);
    const int cbPxW = cbW * cbScale;
    const int cbPxH = cbH * cbScale;
    // Sit just left of the SYSOP text padding — anchor to the right with margin.
    const int cbX = headerArea.getRight() - cbPxW - 4;
    const int cbY = headerArea.getY() + (headerArea.getHeight() - cbPxH) / 2;
    cabinetRect_ = juce::Rectangle<int>(cbX, cbY, cbPxW, cbPxH);

    for (int r = 0; r < cbH; ++r)
        for (int c = 0; c < cbW; ++c)
        {
            const int idx = bombo::game::sprites::kCabinet[r][c];
            if (idx == 0) continue;  // transparent
            juce::Colour col(pal.byIndex(idx));
            // Unlit: heavily dimmed monochrome silhouette. Lit: full palette colour.
            if (! cabinetLit_)
                col = juce::Colour(0xFF333333u).withAlpha(0.7f);
            g.setColour(col);
            g.fillRect(cbX + c * cbScale, cbY + r * cbScale, cbScale, cbScale);
        }

    // --- Invader: drifts across the scope strip when active ---
    if (! discovery_.hasActiveInvader())
    {
        invaderStripRect_ = stripArea;  // keep current for hit-mapping consistency
        return;
    }
    invaderStripRect_ = stripArea;

    constexpr int invW = 8, invH = 8;
    const int invScale = juce::jmax(1, (stripArea.getHeight() - 2) / invH);
    const int invPxW = invW * invScale;
    const int invPxH = invH * invScale;

    // Map logical invaderX in [0, kFbW] -> across the strip width; invaderY in
    // [0, kFbH] -> across the strip height (clamped so it stays inside the band).
    const float fx = discovery_.invaderX() / static_cast<float>(bombo::game::kFbW);
    const float fy = discovery_.invaderY() / static_cast<float>(bombo::game::kFbH);
    const int sx = stripArea.getX()
                 + juce::roundToInt(fx * static_cast<float>(stripArea.getWidth()));
    const int sy = stripArea.getY()
                 + juce::jlimit(0, juce::jmax(0, stripArea.getHeight() - invPxH),
                                juce::roundToInt(fy * static_cast<float>(stripArea.getHeight())
                                                 - invPxH * 0.5f));

    for (int r = 0; r < invH; ++r)
        for (int c = 0; c < invW; ++c)
        {
            const int idx = bombo::game::sprites::kInvader[r][c];
            if (idx == 0) continue;
            g.setColour(juce::Colour(pal.byIndex(idx)));
            g.fillRect(sx + c * invScale, sy + r * invScale, invScale, invScale);
        }
}
#endif

void BBSComponent::mouseDown(const juce::MouseEvent& e)
{
#if BOMBO_GAME_V2
    // Only the discovery surface is clickable, and only on the non-game BBS
    // screens that render the header + scope strip.
    if (screens_.current() == BBSScreen::BoomFeed
        || screens_.current() == BBSScreen::MyDownloads)
    {
        // Cabinet glyph click -> launch (regardless of lit state; the glyph is
        // always drawn, just dim until discovered).
        if (cabinetLit_ && cabinetRect_.contains(e.getPosition()))
        {
            launchGame();
            return;
        }

        // Invader click: map the screen position back into the invader's logical
        // coord space (inverse of paintDiscovery's mapping), then hit-test.
        if (discovery_.hasActiveInvader() && ! invaderStripRect_.isEmpty()
            && invaderStripRect_.contains(e.getPosition()))
        {
            const float fx = static_cast<float>(e.x - invaderStripRect_.getX())
                           / static_cast<float>(juce::jmax(1, invaderStripRect_.getWidth()));
            const float fy = static_cast<float>(e.y - invaderStripRect_.getY())
                           / static_cast<float>(juce::jmax(1, invaderStripRect_.getHeight()));
            const float lx = fx * static_cast<float>(bombo::game::kFbW);
            const float ly = fy * static_cast<float>(bombo::game::kFbH);
            if (discovery_.tryHitInvader(lx, ly))
            {
                launchGame();
                return;
            }
        }
    }
#else
    juce::ignoreUnused(e);
#endif
}

void BBSComponent::launchGame()
{
    // Re-entrancy guard: never re-stash the preset while a game is already live,
    // or we'd clobber prePresetIdx_ with the in-game Pew index and lose the
    // user's real preset. (The keyboard path already can't re-enter, but Task 14
    // and any future HeaderBar launch button add new callers.)
    if (screens_.current() == BBSScreen::Game)
        return;

#if BOMBO_GAME_V2
    // Stash current preset so we can restore it on exit.
    prePresetIdx_ = (presetBank_ != nullptr) ? presetBank_->currentIndex() : -1;

    // Force preset "Pew" as the in-game shot sound, if the bank is wired and
    // the preset exists. We search by displayName since "Pew" is a user preset
    // and its position after the factory presets can vary. If not found (e.g.
    // the user hasn't saved it yet), we leave the current preset active.
    if (presetBank_ != nullptr && apvts_ != nullptr)
    {
        int pewIdx = -1;
        for (int i = 0; i < presetBank_->size(); ++i)
        {
            if (juce::String(presetBank_->at(i).displayName).equalsIgnoreCase("Pew"))
            {
                pewIdx = i;
                break;
            }
        }
        if (pewIdx >= 0)
            presetBank_->applyByIndex(pewIdx, *apvts_);
        else
            juce::Logger::writeToLog("Bombo game: 'Pew' preset not found -- keeping current preset as shot sound");
    }

    // Allocate the ARGB image once per game launch (cheap, kFbW x kFbH = 17920 bytes).
    if (! gameV2Image_.isValid()
        || gameV2Image_.getWidth()  != bombo::game::kFbW
        || gameV2Image_.getHeight() != bombo::game::kFbH)
    {
        gameV2Image_ = juce::Image(juce::Image::ARGB, bombo::game::kFbW, bombo::game::kFbH, false);
    }

    gameV2_.startNewRun(/*dailySeed=*/false);
    screens_.transitionTo(BBSScreen::Game);
    commandBuffer_.clear();
    konamiPos_ = 0;
#else
    game_.onKick = triggerCb_;
    game_.startGame();
    screens_.transitionTo(BBSScreen::Game);
    commandBuffer_.clear();
    konamiPos_ = 0;
#endif
}

void BBSComponent::exitGame()
{
#if BOMBO_GAME_V2
    gameV2_ = bombo::game::Game{};  // reset to Title state, clears wantsExit

    // Restore the preset the user had before the game launched.
    if (presetBank_ != nullptr && apvts_ != nullptr && prePresetIdx_ >= 0)
    {
        presetBank_->applyByIndex(prePresetIdx_, *apvts_);
        prePresetIdx_ = -1;
    }

    screens_.transitionTo(BBSScreen::BoomFeed);
    commandBuffer_.clear();
    konamiPos_ = 0;
#else
    game_.stopGame();
    screens_.transitionTo(BBSScreen::BoomFeed);
    commandBuffer_.clear();
    konamiPos_ = 0;
#endif
}

void BBSComponent::buildIntroText()
{
    introText_ =
        "ATDT 555-1992...\n"
        "CONNECT 2400\n"
        "\n"
        "##############\n"
        "\n"
        "  HYPERFOCUS  BBS\n"
        "  ==============\n"
        "\n"
        "  CONNECTION ESTABLISHED\n"
        "  PRESS ANY KEY TO SKIP\n";
}

void BBSComponent::buildScrollerText()
{
    if (currentSysopIdx_ >= 0 && currentSysopIdx_ < kSysopCount)
        scrollerText_ = kSysops[currentSysopIdx_].scrollerLine;
    else
        scrollerText_ = "HYPERFOCUS BBS - KICK ROM ARCHIVE -";
    scrollerText_ += "   ";
    scrollOffset_  = 0;
}

void BBSComponent::refreshSysopVoice()
{
    if (progression_ != nullptr)
        currentSysopIdx_ = progression_->currentSysopIndex();
    else
        currentSysopIdx_ = 0;

    currentMotd_ = pickMotd(currentSysopIdx_, rng_);
    buildScrollerText();
}

} // namespace bombo
