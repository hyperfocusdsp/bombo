#pragma once

#include "../Theme/ThemedComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace bombo
{

// Hidden 1992-aesthetic BBS terminal overlay. Sits as a sibling of the
// FaceplatePanel under BomboEditor, sized to getLocalBounds(), and is
// invisible by default. show() lifts it to the top of the z-order and
// requests keyboard focus; hide() drops it back. Esc dismisses.
//
// Phase 1 scope (this file): just the surface — dark backdrop + ASCII
// banner + Esc-to-dismiss. The screen-state machine + idle ticker land
// in BBSScreens.h / AsciiArt.h once the scaffold is wired up.
//
// Activation gesture: the v1.0 design is "long-press the central nose
// detonator" (the Archie fuze component, Phase 2). Until that ships, a
// temporary Ctrl+Shift+B in BomboEditor::keyPressed shows the overlay
// for development.
class BBSComponent
    : public juce::Component
    , public bombo::ThemedComponent
{
public:
    BBSComponent();
    ~BBSComponent() override = default;

    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Flip visibility + focus. show() also fires the onShown / onDismissed
    // callbacks so BomboEditor can persist bbs.unlocked = true and write
    // the last-screen index back to PersistentState.
    void show();
    void hide();

    std::function<void()> onShown;
    std::function<void()> onDismissed;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BBSComponent)
};

} // namespace bombo
