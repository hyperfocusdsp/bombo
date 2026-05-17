#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../State/PresetBank.h"
#include "Theme/ThemedComponent.h"

namespace bombo
{

// Preset bar — factory + user CRUD via menu (Phase 3 MVP, 2026-05-17).
//
// Layout:  [ < ]   N / TOTAL · NAME   [ > ]   [ ≡ ]
//
// Chevrons step through the combined list. The center label shows the
// current preset; clicking it opens the same menu as the ≡ button.
//
// Menu contents:
//   • SAVE          — overwrite the current user preset (disabled for factory)
//   • SAVE AS       — prompts a name inline, writes a new user preset
//   • RENAME        — inline rename of the current user preset (disabled for factory)
//   • DELETE        — delete the current user preset (disabled for factory)
//   • INIT          — reset every sound param to its declared default
//   • ──────
//   • <preset list> — flat list of every factory + user preset for direct load
class PresetBarComponent : public juce::Component,
                           public bombo::ThemedComponent
{
public:
    PresetBarComponent(PresetBank& bank,
                       juce::AudioProcessorValueTreeState& apvts);
    ~PresetBarComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    // Refresh the centre label from the bank's current preset.
    void refresh();

private:
    enum class EditMode { None, SaveAs, Rename };

    void showMenu();
    void beginEdit(EditMode mode);
    void commitEdit();
    void cancelEdit();

    PresetBank&                              bank_;
    juce::AudioProcessorValueTreeState&      apvts_;

    juce::TextButton prev_   { "<" };
    juce::TextButton next_   { ">" };
    juce::TextButton menu_   { juce::CharPointer_UTF8("\xe2\x89\xa1") };  // ≡
    juce::Label      name_;
    juce::TextEditor nameEditor_;

    EditMode edit_ = EditMode::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBarComponent)
};

} // namespace bombo
