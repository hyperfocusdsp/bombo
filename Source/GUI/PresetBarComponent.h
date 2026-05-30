#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../State/PresetBank.h"
#include "Theme/ThemedComponent.h"

namespace bombo
{

// Preset bar — factory + user CRUD via menu (Phase 3 MVP, 2026-05-17).
//
// Layout (post 2026-05-24 redesign):
//
//   +----+----------------------+----+
//   |  < |     Acid Tape        |  > |   <- top row: preset name (16pt)
//   |    |     3 / 30           |    |   <- bottom row: counter (16pt)
//   +----+----------------------+----+
//
// Both text rows use the same theme-accent colour and font as the BNC /
// LIM / TAIL pills, so the bar visually belongs to the same control
// language. Click anywhere on the centre opens the preset menu.
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

    // Refresh the displayed text from the bank's current preset.
    void refresh();

    // ThemedComponent broadcast hook — re-applies palette colours to the
    // child buttons / editor (which cache them at attach time) and then
    // repaints. The default ThemedComponent impl only calls repaint(),
    // which doesn't refresh child-button colours.
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

private:
    enum class EditMode { None, SaveAs, Rename };

    void showMenu();
    void importBank();
    void exportBank();
    void beginEdit(EditMode mode);
    void commitEdit();
    void cancelEdit();
    // Apply current palette to prev_/next_/menu_ + nameEditor_.
    // Called from ctor and on theme change.
    void applyPaletteToChildren();

    PresetBank&                              bank_;
    juce::AudioProcessorValueTreeState&      apvts_;

    juce::TextButton prev_   { "<" };
    juce::TextButton next_   { ">" };
    juce::TextButton menu_   { "=" };  // hidden hamburger; label kept ASCII per ascii-only rule
    juce::String     nameText_;   // top row text (preset display name or "-- N presets")
    juce::String     countText_;  // bottom row text ("3 / 30") — empty when no preset is current
    juce::TextEditor nameEditor_;

    // Held across the async bank Import/Export dialog (the FileChooser must
    // outlive launchAsync until its callback fires).
    std::unique_ptr<juce::FileChooser> fileChooser_;

    EditMode edit_ = EditMode::None;

    // CRT/VGA overlay cache (scanline + vignette), rebuilt on resize. Mirrors
    // the scope so the readout reads as the same green phosphor screen when the
    // shared VGA filter is on.
    juce::Image crtScreen_;
    int         crtScreenW_ = 0;
    int         crtScreenH_ = 0;

    // Retry pump: PopupMenu dismissal + BomboEditor::visibilityChanged +
    // host-side focus shuffles can each steal focus back from nameEditor_
    // at unpredictable delays (worse on Hyprland/Wayland). Instead of
    // gambling on one fixed callAfterDelay window, keep re-grabbing
    // until the editor has focus or we hit the attempt cap.
    class FocusGrabber : public juce::Timer
    {
    public:
        explicit FocusGrabber(juce::TextEditor& te) : target_(te) {}
        void start() { attempts_ = 0; startTimer(25); }
        void timerCallback() override;
    private:
        juce::TextEditor& target_;
        int               attempts_ = 0;
    };
    FocusGrabber focusGrabber_ { nameEditor_ };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBarComponent)
};

} // namespace bombo
