#include "PresetBarComponent.h"

#include "Colours.h"
#include "Fonts.h"

namespace bombo
{

PresetBarComponent::PresetBarComponent(PresetBank& bank,
                                       juce::AudioProcessorValueTreeState& apvts)
    : bank_(bank), apvts_(apvts)
{
    setFocusContainerType(juce::Component::FocusContainerType::focusContainer);

    addAndMakeVisible(prev_);
    addAndMakeVisible(next_);
    addAndMakeVisible(menu_);

    prev_.onClick = [this] { bank_.prev(apvts_); refresh(); };
    next_.onClick = [this] { bank_.next(apvts_); refresh(); };
    menu_.onClick = [this] { showMenu(); };

    addChildComponent(nameEditor_);
    nameEditor_.setJustification(juce::Justification::centred);
    nameEditor_.onReturnKey = [this] { commitEdit(); };
    nameEditor_.onEscapeKey = [this] { cancelEdit(); };
    nameEditor_.onFocusLost = [this] { commitEdit(); };

    applyPaletteToChildren();
    refresh();
}

PresetBarComponent::~PresetBarComponent() = default;

void PresetBarComponent::refresh()
{
    const int n   = bank_.size();
    const int idx = bank_.currentIndex();
    if (idx >= 0 && n > 0)
    {
        // currentDisplayName() returns std::string with UTF-8 bytes —
        // wrap with CharPointer_UTF8 so any accented chars in preset
        // names round-trip cleanly (same pattern as the popup menu).
        nameText_  = juce::String(juce::CharPointer_UTF8(
                         bank_.currentDisplayName().c_str()));
        countText_ = juce::String(idx + 1) + " / " + juce::String(n);
    }
    else
    {
        // No preset is current — show the bank size on the top row, the
        // bottom row stays blank rather than showing a meaningless "0/N".
        nameText_  = juce::String(n) + " presets";
        countText_ = {};
    }
    repaint();
}

void PresetBarComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Default ThemedComponent::changeListenerCallback only repaints — but
    // child buttons + the TextEditor cache their palette colours at attach
    // time, so a bare repaint() would leave them rendered in the previous
    // theme's accent. Re-apply, then repaint.
    applyPaletteToChildren();
    repaint();
}

void PresetBarComponent::applyPaletteToChildren()
{
    auto styleButton = [](juce::TextButton& b)
    {
        b.setColour(juce::TextButton::buttonColourId,
                    col::graphiteHi().withAlpha(0.85f));
        b.setColour(juce::TextButton::textColourOffId, col::accentAmber());
        b.setColour(juce::TextButton::textColourOnId,  col::accentAmber());
        b.setColour(juce::ComboBox::outlineColourId,   juce::Colours::transparentBlack);
    };
    styleButton(prev_);
    styleButton(next_);
    styleButton(menu_);

    nameEditor_.setColour(juce::TextEditor::backgroundColourId,
                          col::graphite().withAlpha(0.95f));
    nameEditor_.setColour(juce::TextEditor::textColourId,           col::accentAmber());
    nameEditor_.setColour(juce::TextEditor::outlineColourId,        col::accentAmber().withAlpha(0.60f));
    nameEditor_.setColour(juce::TextEditor::focusedOutlineColourId, col::accentAmber());
    nameEditor_.setColour(juce::TextEditor::highlightColourId,      col::accentAmber().withAlpha(0.35f));
    nameEditor_.applyFontToAllText(bombo::fonts::value(16.0f));
}

void PresetBarComponent::paint(juce::Graphics& g)
{
    // Pill background. Neon-aware (matches the LIM/TAIL/LOOP treatment):
    // on neon themes the accent IS the bright neon and an amber-tinted
    // fill becomes a coloured-on-coloured wash with no contrast against
    // the text. Dark fill + bright accent text is the consistent rule.
    const auto r = getLocalBounds().toFloat().reduced(1.5f);
    if (col::isNeon())
    {
        g.setColour(col::graphite().withAlpha(0.92f));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(col::accentAmber().withAlpha(0.75f));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    }
    else
    {
        g.setColour(col::accentAmber().withAlpha(0.22f));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(col::accentAmber().withAlpha(0.60f));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    }

    // Two-row text: top = preset name, bottom = "X / N". Same font + colour
    // as the BNC/LIM/TAIL pill text (fonts::value(16.0f), accentAmber) so
    // the bar reads as part of the same control family.
    //
    // When the rename TextEditor is visible it overlays the top half —
    // skip drawing the top row text to avoid double-render under the
    // editor's slightly-translucent background.
    const auto area = getLocalBounds().reduced(3, 2);
    constexpr int kChevronW = 18;
    constexpr int kGap      = 3;
    auto textArea = area;
    textArea.removeFromLeft (kChevronW + kGap);
    textArea.removeFromRight(kChevronW + kGap);

    const int halfH    = textArea.getHeight() / 2;
    const auto topRow  = textArea.removeFromTop(halfH);
    const auto botRow  = textArea;  // remainder

    g.setFont(bombo::fonts::value(16.0f));
    g.setColour(col::accentAmber());

    if (! nameEditor_.isVisible())
        g.drawText(nameText_, topRow, juce::Justification::centred, false);

    if (countText_.isNotEmpty())
        g.drawText(countText_, botRow, juce::Justification::centred, false);
}

void PresetBarComponent::mouseDown(const juce::MouseEvent& e)
{
    if (edit_ != EditMode::None) return;
    // Click anywhere off the chevrons opens the menu. menu_ is hidden so
    // its bounds are empty — skip it.
    if (! prev_.getBounds().contains(e.x, e.y)
        && ! next_.getBounds().contains(e.x, e.y))
    {
        showMenu();
    }
}

void PresetBarComponent::resized()
{
    auto area = getLocalBounds().reduced(3, 2);
    if (area.isEmpty()) return;

    // Layout: chevrons on the cartouche edges, text rows occupy the
    // remaining middle band. The hamburger menu button is hidden — its
    // menu opens by clicking the centre name area (mouseDown routes
    // through showMenu). Keeps the strip visually clean inside the
    // narrow cartouche footprint.
    constexpr int kChevronW = 18;
    constexpr int kGap      = 3;

    prev_.setBounds(area.removeFromLeft(kChevronW));
    area.removeFromLeft(kGap);

    next_.setBounds(area.removeFromRight(kChevronW));
    area.removeFromRight(kGap);

    menu_.setVisible(false);

    // Rename / save-as editor overlays the TOP row only — the X/N counter
    // stays visible below so the user can still see which slot they're
    // renaming into.
    const int halfH = area.getHeight() / 2;
    nameEditor_.setBounds(area.removeFromTop(halfH));
}

// ── Menu + inline edit ───────────────────────────────────────────────

void PresetBarComponent::showMenu()
{
    if (edit_ != EditMode::None) return;

    juce::PopupMenu m;
    const bool onUser = bank_.isCurrentUserPreset();
    m.addItem(1, "Save",       onUser);
    m.addItem(2, "Save As...");
    m.addItem(3, "Rename...", onUser);
    m.addItem(4, "Delete",      onUser);
    m.addSeparator();
    m.addItem(5, "Init (reset to defaults)");
    m.addSeparator();

    // Flat preset list — factory first, then user, with a separator at
    // the boundary. Item IDs 100+ map to preset indices.
    const int n = bank_.size();
    bool addedSep = false;
    for (int i = 0; i < n; ++i)
    {
        const auto& p = bank_.at(i);
        if (! addedSep && p.source == PresetBank::Source::User)
        {
            m.addSeparator();
            addedSep = true;
        }
        const bool ticked = (i == bank_.currentIndex());
        m.addItem(100 + i,
                  juce::String(p.displayName)
                      + (p.source == PresetBank::Source::User ? "  *" : ""),
                  true, ticked);
    }

    auto opts = juce::PopupMenu::Options()
                    .withTargetComponent(this)
                    .withMinimumWidth(juce::jmax(180, getWidth()));

    m.showMenuAsync(opts,
        [this](int result)
        {
            if (result == 0) return;
            switch (result)
            {
                case 1:
                    bank_.overwriteCurrent(apvts_);
                    refresh();
                    break;
                case 2:
                    beginEdit(EditMode::SaveAs);
                    break;
                case 3:
                    beginEdit(EditMode::Rename);
                    break;
                case 4:
                    if (bank_.isCurrentUserPreset())
                    {
                        bank_.deleteAt(bank_.currentIndex());
                        // Re-apply whatever the bank fell back to so the
                        // current sound matches the name shown.
                        if (bank_.currentIndex() >= 0)
                            bank_.applyByIndex(bank_.currentIndex(), apvts_);
                        refresh();
                    }
                    break;
                case 5:
                    bank_.applyDefaults(apvts_);  // instance method now (clears current_ + fires onPresetApplied)
                    refresh();
                    break;
                default:
                    if (result >= 100)
                    {
                        bank_.applyByIndex(result - 100, apvts_);
                        refresh();
                    }
                    break;
            }
        });
}

void PresetBarComponent::FocusGrabber::timerCallback()
{
    if (target_.hasKeyboardFocus(true) || attempts_ >= 40)
    {
        stopTimer();
        return;
    }
    target_.grabKeyboardFocus();
    ++attempts_;
}

void PresetBarComponent::beginEdit(EditMode mode)
{
    edit_ = mode;
    nameEditor_.setVisible(true);
    nameEditor_.setText(mode == EditMode::Rename
                        ? juce::String(bank_.currentDisplayName())
                        : juce::String(),
                        juce::dontSendNotification);
    nameEditor_.selectAll();
    repaint();  // hide the cached nameText_ behind the editor
    // Focus-grab must survive (a) PopupMenu's own focus-return async,
    // (b) BomboEditor::visibilityChanged grabbing focus on the editor, and
    // (c) any other late focus shuffling. Fixed callAfterDelay windows
    // (60 / 150 ms) were unreliable on Hyprland/Wayland — the OS-level
    // focus restore after popup dismissal can land outside the window the
    // timers fire in, so both grabs no-op and the user has to click.
    // FocusGrabber polls every 25 ms (≤ 1 s total) and stops the moment
    // the editor actually has focus; idempotent and self-cancelling.
    focusGrabber_.start();
}

void PresetBarComponent::commitEdit()
{
    if (edit_ == EditMode::None) return;
    const auto mode = edit_;
    edit_ = EditMode::None;
    const auto entered = nameEditor_.getText().trim();
    nameEditor_.setVisible(false);

    if (entered.isEmpty()) { refresh(); return; }

    switch (mode)
    {
        case EditMode::SaveAs: bank_.saveAs(entered, apvts_);              break;
        case EditMode::Rename: bank_.renameAt(bank_.currentIndex(), entered); break;
        case EditMode::None: break;
    }
    refresh();
}

void PresetBarComponent::cancelEdit()
{
    if (edit_ == EditMode::None) return;
    edit_ = EditMode::None;
    nameEditor_.setVisible(false);
    refresh();
}

} // namespace bombo
