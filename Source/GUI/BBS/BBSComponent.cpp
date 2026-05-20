#include "BBSComponent.h"
#include "../Colours.h"
#include "../Fonts.h"
#include "../../State/PresetBank.h"
#include <juce_core/juce_core.h>

namespace bombo
{

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

    setVisible(true);
    toFront(true);
    grabKeyboardFocus();
    startTimer(40);

    if (onShown) onShown();
}

void BBSComponent::hide()
{
    stopTimer();
    setVisible(false);
    if (onDismissed) onDismissed();
    if (auto* p = getParentComponent())
        p->grabKeyboardFocus();
}

void BBSComponent::resized() {}

void BBSComponent::timerCallback()
{
    if (screens_.current() == BBSScreen::Intro && !introComplete_)
    {
        introCharPos_ += 2;
        if (introCharPos_ >= introText_.length())
        {
            introComplete_ = true;
            screens_.onIntroComplete();
        }
    }
    scrollOffset_ = (scrollOffset_ + 1) % juce::jmax(1, scrollerText_.length());
    repaint();
}

bool BBSComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { hide(); return true; }

    if (screens_.current() == BBSScreen::Intro)
    {
        introComplete_ = true;
        screens_.onIntroComplete();
        // Don't return — N/P fall through to the BoomFeed block so one
        // keypress both skips the intro AND applies the navigation action.
        // Any other key is swallowed at the end of the function.
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
        if (key == juce::KeyPress::spaceKey)
        {
            if (triggerCb_) triggerCb_();
            return true;
        }
        if (ch == 't' || ch == 'T')
        {
            if (triggerCb_) triggerCb_();
            return true;
        }
        if (ch == 's' || ch == 'S')
        {
            if (apvts_ != nullptr && presetBank_ != nullptr)
            {
                const auto name = boomFeed_.currentFilename()
                                      .upToLastOccurrenceOf(".KCK", false, true);
                presetBank_->saveAs(name, *apvts_);
                if (progression_ != nullptr) progression_->onKickSaved();
                saveStatusMsg_ = ">> PRESET SAVED: " + name + " <<";
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
            paintHeader    (g, b.removeFromTop(headerH));
            b.removeFromBottom(footerH);
            paintScrollerBar(g, b.removeFromBottom(scrollerH));
            paintBoomFeed  (g);
            break;
        }

        case BBSScreen::MyDownloads:
        {
            const int headerH   = 22;
            const int scrollerH = 16;
            const int footerH   = 14;
            paintHeader     (g, b.removeFromTop(headerH));
            b.removeFromBottom(footerH);
            paintScrollerBar(g, b.removeFromBottom(scrollerH));
            paintMyDownloads(g);
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
