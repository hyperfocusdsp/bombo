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
    auto setupButton = [this](juce::TextButton& b)
    {
        addAndMakeVisible(b);
        b.setColour(juce::TextButton::buttonColourId,
                    col::graphiteHi().withAlpha(0.85f));
        b.setColour(juce::TextButton::textColourOffId, col::bone());
        b.setColour(juce::TextButton::textColourOnId,  col::bone());
        b.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    };
    setupButton(prev_);
    setupButton(next_);
    setupButton(menu_);

    prev_.onClick = [this] { bank_.prev(apvts_); refresh(); };
    next_.onClick = [this] { bank_.next(apvts_); refresh(); };
    menu_.onClick = [this] { showMenu(); };

    addAndMakeVisible(name_);
    name_.setJustificationType(juce::Justification::centred);
    name_.setColour(juce::Label::textColourId,        col::accentAmber());
    name_.setColour(juce::Label::backgroundColourId,  juce::Colours::transparentBlack);
    name_.setFont(bombo::fonts::value(11.5f).boldened());
    name_.setInterceptsMouseClicks(false, false);  // bar handles its own clicks

    addChildComponent(nameEditor_);
    nameEditor_.setJustification(juce::Justification::centred);
    nameEditor_.setColour(juce::TextEditor::backgroundColourId,
                          col::graphiteHi().withAlpha(0.95f));
    nameEditor_.setColour(juce::TextEditor::textColourId,    col::accentAmber());
    nameEditor_.setColour(juce::TextEditor::outlineColourId, col::accentAmber().withAlpha(0.6f));
    nameEditor_.setColour(juce::TextEditor::focusedOutlineColourId, col::accentAmber());
    nameEditor_.onReturnKey = [this] { commitEdit(); };
    nameEditor_.onEscapeKey = [this] { cancelEdit(); };
    nameEditor_.onFocusLost = [this] { commitEdit(); };

    refresh();
}

PresetBarComponent::~PresetBarComponent() = default;

void PresetBarComponent::refresh()
{
    const int n = bank_.size();
    const int idx = bank_.currentIndex();
    juce::String text;
    if (idx >= 0 && n > 0)
    {
        // currentDisplayName() returns std::string with UTF-8 bytes —
        // wrap with CharPointer_UTF8 so any accented chars in preset
        // names round-trip cleanly (same pattern as the popup menu).
        text = juce::String(idx + 1) + " / " + juce::String(n)
             + "   "
             + juce::String(juce::CharPointer_UTF8(bank_.currentDisplayName().c_str()));
    }
    else
    {
        text = juce::String("-- ") + juce::String(n) + " presets";
    }
    name_.setText(text, juce::dontSendNotification);
}

void PresetBarComponent::paint(juce::Graphics& g)
{
    // Preset bar lives in the footprint where the yellow cartouche band
    // used to sit (since 2026-05-17). The amber top-and-bottom hairlines
    // are an echo of the cartouche so the strip reads as a designed
    // control surface rather than an empty stripe of chassis.
    const auto outer = getLocalBounds().toFloat();
    const auto inner = outer.reduced(2.0f, 2.0f);

    g.setColour(col::graphite().withAlpha(0.92f));
    g.fillRoundedRectangle(inner, 3.0f);

    g.setColour(col::ink().withAlpha(0.60f));
    g.drawRoundedRectangle(inner, 3.0f, 0.8f);

    // Amber hairlines top + bottom, full bar width.
    g.setColour(col::accentAmber().withAlpha(0.55f));
    g.fillRect(outer.getX(), outer.getY(),                     outer.getWidth(), 1.0f);
    g.fillRect(outer.getX(), outer.getBottom() - 1.0f,         outer.getWidth(), 1.0f);
}

void PresetBarComponent::mouseDown(const juce::MouseEvent& e)
{
    if (edit_ != EditMode::None) return;
    // Click on the centre name (anything not a button) opens the menu.
    if (! prev_.getBounds().contains(e.x, e.y)
        && ! next_.getBounds().contains(e.x, e.y)
        && ! menu_.getBounds().contains(e.x, e.y))
    {
        showMenu();
    }
}

void PresetBarComponent::resized()
{
    auto area = getLocalBounds().reduced(3, 2);
    if (area.isEmpty()) return;

    // Layout: < on the far LEFT edge of the cartouche, > on the far
    // RIGHT edge, name display fills the middle. The ≡ menu button is
    // hidden — its menu opens by clicking anywhere on the name strip
    // (mouseDown already routes through showMenu). Keeping the strip
    // visually clean inside the narrow cartouche footprint.
    constexpr int kChevronW = 18;
    constexpr int kGap      = 3;

    prev_.setBounds(area.removeFromLeft(kChevronW));
    area.removeFromLeft(kGap);

    next_.setBounds(area.removeFromRight(kChevronW));
    area.removeFromRight(kGap);

    menu_.setVisible(false);

    name_.setBounds(area);
    nameEditor_.setBounds(area);
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
                    PresetBank::applyDefaults(apvts_);
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

void PresetBarComponent::beginEdit(EditMode mode)
{
    edit_ = mode;
    name_.setVisible(false);
    nameEditor_.setVisible(true);
    nameEditor_.setText(mode == EditMode::Rename
                        ? juce::String(bank_.currentDisplayName())
                        : juce::String(),
                        juce::dontSendNotification);
    nameEditor_.selectAll();
    // Double-defer: PopupMenu dismissal posts its own focus-return async,
    // so a single callAsync fires before that and gets stolen. Two ticks
    // ensures we run after the popup has fully returned focus to BomboEditor.
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<juce::TextEditor>(&nameEditor_)]
        {
            juce::MessageManager::callAsync([safe]
            {
                if (safe != nullptr) safe->grabKeyboardFocus();
            });
        });
}

void PresetBarComponent::commitEdit()
{
    if (edit_ == EditMode::None) return;
    const auto mode = edit_;
    edit_ = EditMode::None;
    const auto entered = nameEditor_.getText().trim();
    nameEditor_.setVisible(false);
    name_.setVisible(true);

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
    name_.setVisible(true);
    refresh();
}

} // namespace bombo
