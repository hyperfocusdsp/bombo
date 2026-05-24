#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"
#include "Fonts.h"
#include "../ParameterIds.h"

namespace bombo
{

// Drop-in binding that turns a juce::Slider into a "routed DEC knob".
//
// Why this exists: VOICE B's DEC knob originally affected Voice A's
// ampDecay (legacy wiring); it was migrated to midDecay so it would
// actually control Voice B's audible envelope. That left Voice A's amp
// decay with NO direct UI knob — only the global DECAY macro could
// touch it. RoutedDecayBinding restores access via a sibling 3-state
// pill (A/B/AB) that re-targets the same DEC knob without duplicating
// the control.
//
//   routing = "A"  → slider writes to pid::ampDecay only.
//   routing = "B"  → slider writes to pid::midDecay only.   (default)
//   routing = "AB" → slider writes the SAME plain-ms value to both,
//                    keeping them sync'd as long as the knob is used.
//
// We deliberately do NOT use juce::SliderAttachment here — that would
// hard-bind the slider to one param. Instead we read/write via
// apvts.getParameter(...) and listen for external param edits (macros,
// host automation, presets) so the slider position reflects whichever
// target the routing currently points at.
class RoutedDecayBinding : public juce::Slider::Listener,
                           public juce::AudioProcessorValueTreeState::Listener
{
public:
    RoutedDecayBinding(juce::AudioProcessorValueTreeState& apvts,
                       juce::Slider& slider)
        : apvts_(apvts), slider_(slider)
    {
        // Slider range mirrors midDecay's param range — pull from APVTS
        // so a future range bump propagates without manual sync. Voice A's
        // ampDecay has a different range (50-5000 vs 10-5000) but we use
        // a single slider for both — pick the wider of the two so the
        // slider can express any reachable value of either param.
        const auto* mid = apvts_.getParameter(pid::midDecay);
        const auto* amp = apvts_.getParameter(pid::ampDecay);
        if (mid && amp)
        {
            const auto& rMid = static_cast<const juce::RangedAudioParameter*>(mid)->getNormalisableRange();
            const auto& rAmp = static_cast<const juce::RangedAudioParameter*>(amp)->getNormalisableRange();
            const float lo   = juce::jmin(rMid.start, rAmp.start);
            const float hi   = juce::jmax(rMid.end,   rAmp.end);
            const float skew = rMid.skew;
            slider_.setNormalisableRange({ lo, hi, 0.0, skew });
        }

        // Initialise slider from whichever target the current routing
        // points at so the knob position reads truthfully on first show.
        syncSliderFromRoutedTarget();

        slider_.addListener(this);
        apvts_.addParameterListener(pid::ampDecay,   this);
        apvts_.addParameterListener(pid::midDecay,   this);
        apvts_.addParameterListener(pid::decRouting, this);
    }

    ~RoutedDecayBinding() override
    {
        slider_.removeListener(this);
        apvts_.removeParameterListener(pid::ampDecay,   this);
        apvts_.removeParameterListener(pid::midDecay,   this);
        apvts_.removeParameterListener(pid::decRouting, this);
    }

    void sliderValueChanged(juce::Slider* s) override
    {
        if (s != &slider_ || suppressWrites_) return;
        const float plain = (float) slider_.getValue();
        const int routing = currentRouting();
        const juce::ScopedValueSetter<bool> guard(suppressReads_, true);
        if (routing == kA || routing == kAB) writePlain(pid::ampDecay, plain);
        if (routing == kB || routing == kAB) writePlain(pid::midDecay, plain);
    }

    void parameterChanged(const juce::String& paramId, float /*newValue*/) override
    {
        if (paramId == pid::decRouting)
        {
            // Routing flipped — slider should now show the new target's
            // value. AB mode keeps showing B (its current target read).
            syncSliderFromRoutedTarget();
            return;
        }
        // External param change (macro, automation, preset). Only react
        // if it's the param the knob currently displays — avoids
        // bouncing when the OTHER routed target moves independently.
        if (suppressReads_) return;
        const int routing = currentRouting();
        const bool relevant = (paramId == pid::ampDecay && routing == kA)
                           || (paramId == pid::midDecay && (routing == kB || routing == kAB));
        if (relevant) syncSliderFromRoutedTarget();
    }

private:
    static constexpr int kA  = 0;
    static constexpr int kB  = 1;
    static constexpr int kAB = 2;

    int currentRouting() const noexcept
    {
        if (auto* p = apvts_.getRawParameterValue(pid::decRouting))
            return juce::jlimit(0, 2, (int) std::round(p->load()));
        return kB;
    }

    void writePlain(const juce::String& paramId, float plain) noexcept
    {
        auto* p = apvts_.getParameter(paramId);
        if (p == nullptr) return;
        const float norm = p->convertTo0to1(plain);
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
    }

    void syncSliderFromRoutedTarget() noexcept
    {
        const int routing = currentRouting();
        const juce::String tgt = (routing == kA) ? juce::String(pid::ampDecay)
                                                  : juce::String(pid::midDecay);
        auto* p = apvts_.getParameter(tgt);
        if (p == nullptr) return;
        const float plain = p->convertFrom0to1(p->getValue());
        const juce::ScopedValueSetter<bool> guard(suppressWrites_, true);
        slider_.setValue(plain, juce::sendNotificationSync);
        // Also retarget the double-click / Ctrl-click reset value to the
        // newly-routed param's default — so a reset gesture lands on the
        // default of whatever DEC is currently controlling.
        if (auto* rp = static_cast<juce::RangedAudioParameter*>(p))
        {
            const float plainDefault =
                rp->getNormalisableRange().convertFrom0to1(rp->getDefaultValue());
            slider_.setDoubleClickReturnValue(true, plainDefault);
        }
    }

    juce::AudioProcessorValueTreeState& apvts_;
    juce::Slider& slider_;
    bool suppressWrites_ = false;  // ignore slider events while we set its value programmatically
    bool suppressReads_  = false;  // ignore APVTS callbacks while we are writing
};

// Small 1-char button that cycles the dec_routing param A → B → AB → A.
// Lives next to the DEC knob in the VOICE B section. Reads + writes the
// pid::decRouting Choice param so host automation, presets, undo all work.
class DecRoutingPill : public juce::Button
{
public:
    explicit DecRoutingPill(juce::AudioProcessorValueTreeState& apvts)
        : juce::Button({}), apvts_(apvts),
          attachment_(*apvts.getParameter(pid::decRouting),
                      [this](float) { repaint(); })
    {
        setClickingTogglesState(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTooltip("DEC routing - A: Voice A amp env. B: Voice B mid + sample env. "
                   "AB: both. Click to cycle.");
        attachment_.sendInitialUpdate();
    }

    void clicked() override
    {
        auto* p = apvts_.getParameter(pid::decRouting);
        if (p == nullptr) return;
        // Three choice options → normalised 0.0 / 0.5 / 1.0. Advance to
        // next, wrap. Round-trip through convertFrom0to1 to land on the
        // exact step value JUCE expects for a Choice param.
        const int   cur  = juce::jlimit(0, 2,
                                (int) std::round(p->convertFrom0to1(p->getValue())));
        const int   next = (cur + 1) % 3;
        const float norm = p->convertTo0to1((float) next);
        p->beginChangeGesture();
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
        p->endChangeGesture();
    }

    void paintButton(juce::Graphics& g,
                     bool shouldDrawButtonAsHighlighted,
                     bool /*shouldDrawButtonAsDown*/) override
    {
        const auto r = getLocalBounds().toFloat();
        const bool hover = shouldDrawButtonAsHighlighted;
        const bool neon  = col::isNeon();

        // Pill chrome — same dark-bg + accent-border pattern as the
        // SynthToggle / LoopButton family on neon themes; amber-fill on
        // classic themes. Always treated as "on" because the routing
        // pill is informational rather than binary on/off.
        if (neon)
        {
            g.setColour(col::graphite().withAlpha(0.92f));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(col::accentAmber().withAlpha(hover ? 1.0f : 0.85f));
            g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
        }
        else
        {
            g.setColour(col::accentAmber().withAlpha(hover ? 0.35f : 0.28f));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(col::accentAmber().withAlpha(0.70f));
            g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
        }

        // Single-char glyph. We pull it live from the param so it stays
        // correct even if a host automation lane writes the value while
        // the user isn't interacting.
        const char* label = "B";
        if (auto* p = apvts_.getParameter(pid::decRouting))
        {
            const int idx = juce::jlimit(0, 2,
                                (int) std::round(p->convertFrom0to1(p->getValue())));
            label = (idx == 0) ? "A" : (idx == 1) ? "B" : "+";
        }
        g.setColour(neon ? col::accentAmber() : col::bone());
        g.setFont(fonts::value(11.0f));
        g.drawText(label, r, juce::Justification::centred, false);
    }

private:
    juce::AudioProcessorValueTreeState& apvts_;
    juce::ParameterAttachment attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecRoutingPill)
};

} // namespace bombo
