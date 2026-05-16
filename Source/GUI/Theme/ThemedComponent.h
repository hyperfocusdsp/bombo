#pragma once

#include "ThemeProvider.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace bombo
{

// Mixin: any juce::Component subclass that paints palette colours can inherit
// from this in addition to its existing Component-derived base to get
// auto-repaint on theme change.
//
// Usage:
//   class MyWidget : public juce::Component, public bombo::ThemedComponent { ... };
//   // also works for any Component-derived base (juce::Button, juce::Slider, …)
//
// Registers in ctor, unregisters in dtor, calls repaint() on broadcast.
// dynamic_cast<juce::Component*> resolves to the most-derived Component base.
//
// Thread safety: ThemeProvider broadcasts on the message thread; repaint() is
// also message-thread-only. juce::ChangeBroadcaster::add/removeChangeListener
// internally guard with a CriticalSection so the ctor/dtor calls are safe even
// if the host destroys a component off-thread.
class ThemedComponent : public juce::ChangeListener
{
public:
    ThemedComponent()           { ThemeProvider::get().addChangeListener(this); }
    ~ThemedComponent() override { ThemeProvider::get().removeChangeListener(this); }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        if (auto* self = dynamic_cast<juce::Component*>(this))
            self->repaint();
    }
};

} // namespace bombo
