#include "PluginEditor.h"

#include "ParamIDs.h"

#include <utility>

namespace
{
// ---------------------------------------------------------------- geometry
// Mirrors mod/modgui/stylesheet-namstack-mod.css. Vertical positions are the
// pedal's, unchanged; the panel is wider (1200 vs 1120) only because the tone
// stack's combo box needs room the pedal's knob does not.
constexpr int kWidth = 1200;
constexpr int kHeight = 624;

constexpr int kRuleInset = 28;   // .ns-section: left 28, width = panel - 56
constexpr int kRuleGap = 120;    // centre gap the section name sits in

constexpr int kCaptionH = 16;    // .ns-ctrl p
constexpr int kValueH = 16;      // .ns-value
constexpr int kWidgetTop = 2;    // .ns-knob margin-top
constexpr int kKnobH = 56;       // .ns-knob / .ns-switch
constexpr int kFaderH = 96;      // .ns-fader
constexpr int kComboH = 24;

constexpr int kCell = 88;        // .ns-ctrl
constexpr int kEqCell = 80;      // .ns-row-eq .ns-ctrl
constexpr int kStackCell = 160;  // the amp-model list, wide enough for its text
constexpr int kFaderCell = 68;   // .ns-row-eq .ns-ctrl-fader
constexpr int kSlotW = 264;      // one IR slot = 3 cells
constexpr int kFileW = 232;      // .ns-file-select
constexpr int kFileBoxH = 22;
constexpr int kClearW = 24;

// Section rules and the row under each of them (CSS tops, unchanged).
constexpr int kAmpSection = 50, kAmpRow = 70;
constexpr int kEqSection = 170, kEqRow = 190;
constexpr int kCabSection = 330, kCabRow = 392;
constexpr int kSpreadSection = 492, kSpreadRow = 512;
constexpr int kModelFileTop = 94, kIrFileTop = 350;

// Row lefts: each row is centred as a group in the panel.
//   AMP:    240 (model selector) + 20 + 5*88 = 700 -> (1200-700)/2 = 250
//   EQ:     160 + 8*80 + 5*68           = 1140     -> (1200-1140)/2 = 30
//   CAB:    4 * 264                     = 1056     -> (1200-1056)/2 = 72
//   SPREAD: 7 * 88                      = 616      -> (1200-616)/2  = 292
constexpr int kAmpModelX = 250;
constexpr int kAmpKnobsX = 510;
constexpr int kEqRowX = 30;
constexpr int kCabRowX = 72;
constexpr int kSpreadRowX = 292;

using LnF = NamStackLookAndFeel;

void placeCaption (juce::Label& caption, int x, int top, int width)
{
    caption.setBounds (x, top, width, kCaptionH);
}
} // namespace

// ------------------------------------------------------------ cell builders

void NamStackAudioProcessorEditor::addKnob (juce::Slider& slider, juce::Label& caption,
                                            const juce::String& text, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextValueSuffix (suffix);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kCell, kValueH);
    slider.setColour (juce::Slider::textBoxTextColourId, LnF::textDim);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    caption.setColour (juce::Label::textColourId, LnF::text);
    addAndMakeVisible (caption);
}

void NamStackAudioProcessorEditor::addFader (juce::Slider& slider, juce::Label& caption,
                                             const juce::String& text, const juce::String& suffix)
{
    addKnob (slider, caption, text, suffix);
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setDoubleClickReturnValue (true, 0.0); // back to flat
}

void NamStackAudioProcessorEditor::addSwitch (juce::ToggleButton& button, juce::Label& caption,
                                              const juce::String& text, const juce::String& tooltip)
{
    // The name lives in the caption above, exactly as on the pedal, so the
    // button itself carries no text.
    button.setButtonText ({});
    if (tooltip.isNotEmpty())
        button.setTooltip (tooltip);
    addAndMakeVisible (button);

    caption.setText (text, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    caption.setColour (juce::Label::textColourId, LnF::text);
    if (tooltip.isNotEmpty())
        caption.setTooltip (tooltip);
    addAndMakeVisible (caption);
}

void NamStackAudioProcessorEditor::addCombo (juce::ComboBox& box, juce::Label& caption,
                                             const juce::String& text, const juce::String& paramID)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramID)))
        box.addItemList (choice->choices, 1);
    addAndMakeVisible (box);

    caption.setText (text, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    caption.setColour (juce::Label::textColourId, LnF::text);
    addAndMakeVisible (caption);
}

void NamStackAudioProcessorEditor::addFileRow (juce::Label& caption, juce::TextButton& name,
                                               juce::TextButton& clear, const juce::String& text)
{
    // The pedal's file selector: a left-aligned caption over a dark box holding
    // the loaded file's name. Clicking the box opens the chooser; the X next to
    // it is the "-- none --" entry of the pedal's dropdown.
    caption.setText (text, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centredLeft);
    caption.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    caption.setColour (juce::Label::textColourId, LnF::text);
    addAndMakeVisible (caption);

    name.getProperties().set (LnF::alignLeftProperty, true);
    addAndMakeVisible (name);
    addAndMakeVisible (clear);
}

// ------------------------------------------------------------------- editor

NamStackAudioProcessorEditor::NamStackAudioProcessorEditor (NamStackAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    auto& apvts = processor.apvts;

    // ------------------------------------------------------------------ AMP
    addFileRow (modelFileLabel, modelNameButton, modelClearButton, "NEURAL MODEL");
    modelNameButton.onClick = [this] { chooseModelFile(); };
    modelClearButton.onClick = [this] { processor.clearModel(); };

    modelInfoLabel.setJustificationType (juce::Justification::centredLeft);
    modelInfoLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    modelInfoLabel.setColour (juce::Label::textColourId, LnF::textDim);
    addAndMakeVisible (modelInfoLabel);

    addKnob (qualitySlider, qualityLabel, "QUALITY");
    addKnob (inputGainSlider, inputGainLabel, "INPUT", " dB");
    addKnob (aidaParam1Slider, aidaParam1Label, "PARAM 1");
    addKnob (aidaParam2Slider, aidaParam2Label, "PARAM 2");
    addKnob (outputGainSlider, outputGainLabel, "OUTPUT", " dB");

    qualityAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::modelQuality, qualitySlider);
    inputGainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::inputGain, inputGainSlider);
    aidaParam1Attachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::aidaParam1, aidaParam1Slider);
    aidaParam2Attachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::aidaParam2, aidaParam2Slider);
    outputGainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::outputGain, outputGainSlider);

    // ------------------------------------------------------------------- EQ
    addCombo (toneStackBox, toneStackLabel, "STACK", ParamIDs::tsModel);
    addSwitch (toneOnButton, toneOnLabel, "STACK ON");
    addCombo (tonePositionBox, tonePositionLabel, "PRE/POST", ParamIDs::tsPosition);
    addKnob (bassSlider, bassLabel, "BASS");
    addKnob (midSlider, midLabel, "MIDDLE");
    addKnob (trebleSlider, trebleLabel, "TREBLE");
    addSwitch (toneCompButton, toneCompLabel, "LEVEL COMP",
               "Compensates the passive stack's insertion loss: noon = 0 dB on every model");
    addSwitch (geqOnButton, geqOnLabel, "EQ ON");
    addCombo (geqPositionBox, geqPositionLabel, "EQ PRE/POST", ParamIDs::geqPosition);

    toneStackAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::tsModel, toneStackBox);
    toneOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::tsOn, toneOnButton);
    tonePositionAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::tsPosition, tonePositionBox);
    bassAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::tsBass, bassSlider);
    midAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::tsMid, midSlider);
    trebleAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::tsTreble, trebleSlider);
    toneCompAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::tsComp, toneCompButton);
    geqOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::geqOn, geqOnButton);
    geqPositionAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::geqPosition, geqPositionBox);

    for (int band = 0; band < nsdsp::GraphicEQ::numBands; ++band)
    {
        const auto hz = nsdsp::GraphicEQ::getFrequencies()[(size_t) band];
        addFader (geqSliders[band], geqLabels[band], juce::String (juce::roundToInt (hz)), " dB");
        geqAttachments[band] = std::make_unique<SliderAttachment> (
            apvts, ParamIDs::geqBand (band, hz), geqSliders[band]);
    }

    // ------------------------------------------------------------------ CAB
    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        auto& slot = irSlots[i];

        addFileRow (slot.fileLabel, slot.nameButton, slot.clearButton, "IR " + juce::String (i + 1));
        slot.nameButton.onClick = [this, i] { chooseIRFile (i); };
        slot.clearButton.onClick = [this, i] { processor.clearIR (i); };

        addSwitch (slot.onButton, slot.onLabel, "ON");
        addKnob (slot.gainSlider, slot.gainLabel, "LEVEL", " dB");
        addKnob (slot.panSlider, slot.panLabel, "PAN");

        slot.onAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::irOn (i), slot.onButton);
        slot.gainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::irGain (i), slot.gainSlider);
        slot.panAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::irPan (i), slot.panSlider);
    }

    // --------------------------------------------------------------- SPREAD
    addSwitch (spreadOnButton, spreadOnLabel, "SPREAD",
               "ADT-style stereo image: one side dry, the other a wobbling late copy");
    addKnob (spreadOffsetSlider, spreadOffsetLabel, "OFFSET", " ms");
    addKnob (spreadWobbleSlider, spreadWobbleLabel, "WOBBLE");
    addSwitch (spreadWobbleOnButton, spreadWobbleOnLabel, "WOBBLE ON",
               "Random-walk drift of the delay time -- what makes the copy read as a second take");
    addKnob (spreadCrossoverSlider, spreadCrossoverLabel, "CROSSOVER", " Hz");
    addSwitch (spreadCrossoverOnButton, spreadCrossoverOnLabel, "X-OVER ON",
               "Keeps everything under the cutoff out of the delay, so the lows stay mono-safe");
    addSwitch (spreadDiffuseOnButton, spreadDiffuseOnLabel, "DIFFUSE",
               "Allpass cascade on the late side: decorrelates phase without touching magnitude");

    // The sign of Offset picks the lagged channel, so centre is the identity
    // point and deserves a detent: double-click lands exactly on 0 ms.
    spreadOffsetSlider.setDoubleClickReturnValue (true, 0.0);

    spreadOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::sprOn, spreadOnButton);
    spreadOffsetAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::sprOffset, spreadOffsetSlider);
    spreadWobbleAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::sprWobble, spreadWobbleSlider);
    spreadWobbleOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::sprWobbleOn, spreadWobbleOnButton);
    spreadCrossoverAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::sprCrossover, spreadCrossoverSlider);
    spreadCrossoverOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::sprCrossoverOn, spreadCrossoverOnButton);
    spreadDiffuseOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::sprDiffuseOn, spreadDiffuseOnButton);

    processor.fileStateChanged.addChangeListener (this);
    refreshFileLabels();

    setSize (kWidth, kHeight);
}

NamStackAudioProcessorEditor::~NamStackAudioProcessorEditor()
{
    processor.fileStateChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void NamStackAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFileLabels();
}

void NamStackAudioProcessorEditor::refreshFileLabels()
{
    const auto modelName = processor.getModelName();
    modelNameButton.setButtonText (modelName.isNotEmpty() ? modelName : "-- none --");
    modelInfoLabel.setText (processor.getModelInfo(), juce::dontSendNotification);

    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        const auto irName = processor.getIRName (i);
        irSlots[i].nameButton.setButtonText (irName.isNotEmpty() ? irName : "-- none --");
    }

    // The conditioning knobs only do something on conditioned AIDA-X models.
    const auto numConditioning = processor.getNumModelConditioningInputs();
    aidaParam1Slider.setEnabled (numConditioning >= 1);
    aidaParam2Slider.setEnabled (numConditioning >= 2);
    aidaParam1Label.setEnabled (numConditioning >= 1);
    aidaParam2Label.setEnabled (numConditioning >= 2);
}

void NamStackAudioProcessorEditor::chooseModelFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Select a NAM or AIDA-X model",
                                                       juce::File(),
                                                       "*.nam;*.aidax;*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;

                                  juce::String error;
                                  if (! processor.loadModelFile (file, error))
                                      juce::AlertWindow::showMessageBoxAsync (
                                          juce::MessageBoxIconType::WarningIcon, "Model load failed", error);
                              });
}

void NamStackAudioProcessorEditor::chooseIRFile (int slot)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Select an impulse response",
                                                       juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, slot] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file != juce::File())
                                      processor.loadIRFile (slot, file);
                              });
}

void NamStackAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Panel: the pedal's vertical gradient inside a dark edge.
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill ({ LnF::bgTop, 0.0f, 0.0f, LnF::bgBottom, 0.0f, bounds.getHeight(), false });
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (LnF::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (1.0f), 12.0f, 2.0f);

    // Header: title, feature list and brand share one text baseline.
    constexpr float baseline = 33.0f;
    const auto titleFont = juce::Font (juce::FontOptions (26.0f, juce::Font::bold));
    g.setColour (LnF::amber);
    g.setFont (titleFont);
    g.drawSingleLineText ("NamStack", 24, (int) baseline);

    g.setColour (LnF::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawSingleLineText (juce::String (juce::CharPointer_UTF8 (
                              "NAM \xc2\xb7 AIDA-X \xc2\xb7 TONE STACK \xc2\xb7 5-BAND EQ "
                              "\xc2\xb7 IR MIXER \xc2\xb7 SPREAD")),
                          24 + juce::GlyphArrangement::getStringWidthInt (titleFont, "NamStack") + 24,
                          (int) baseline);
    g.drawSingleLineText ("Pilali", getWidth() - 24, (int) baseline, juce::Justification::right);

    // Section rules: a thin line broken in the middle by the section's name.
    const auto ruleWidth = getWidth() - 2 * kRuleInset;
    const auto segment = (ruleWidth - kRuleGap) / 2;

    const auto drawSection = [&] (int top, const char* name)
    {
        const auto y = (float) top + 7.0f;
        g.setColour (LnF::boxEdge);
        g.fillRect ((float) kRuleInset, y, (float) segment, 1.0f);
        g.fillRect ((float) (kRuleInset + segment + kRuleGap), y, (float) segment, 1.0f);

        g.setColour (LnF::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        // The pedal letter-spaces these by 2px; spaced-out capitals are the
        // closest a plain drawText gets.
        g.drawText (name, kRuleInset + segment, top, kRuleGap, 14,
                    juce::Justification::centred, false);
    };

    drawSection (kAmpSection, "A M P");
    drawSection (kEqSection, "E Q");
    drawSection (kCabSection, "C A B");
    drawSection (kSpreadSection, "S P R E A D");
}

void NamStackAudioProcessorEditor::resized()
{
    const auto knobCell = [] (juce::Slider& slider, juce::Label& caption, int x, int top, int w)
    {
        placeCaption (caption, x, top, w);
        slider.setBounds (x, top + kCaptionH + kWidgetTop, w, kKnobH + kWidgetTop + kValueH);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, w, kValueH);
    };

    const auto faderCell = [] (juce::Slider& slider, juce::Label& caption, int x, int top, int w)
    {
        placeCaption (caption, x, top, w);
        slider.setBounds (x, top + kCaptionH + kWidgetTop, w, kFaderH + kWidgetTop + kValueH);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, w, kValueH);
    };

    const auto switchCell = [] (juce::ToggleButton& button, juce::Label& caption, int x, int top, int w)
    {
        placeCaption (caption, x, top, w);
        button.setBounds (x, top + kCaptionH + kWidgetTop, w, kKnobH);
    };

    const auto comboCell = [] (juce::ComboBox& box, juce::Label& caption, int x, int top, int w)
    {
        placeCaption (caption, x, top, w);
        box.setBounds (x + 4, top + kCaptionH + kWidgetTop + (kKnobH - kComboH) / 2,
                       w - 8, kComboH);
    };

    const auto fileRow = [] (juce::Label& caption, juce::TextButton& name, juce::TextButton& clear,
                             int x, int top)
    {
        caption.setBounds (x, top, kFileW, kCaptionH);
        name.setBounds (x, top + kCaptionH, kFileW - kClearW - 4, kFileBoxH);
        clear.setBounds (x + kFileW - kClearW, top + kCaptionH, kClearW, kFileBoxH);
    };

    // ------------------------------------------------------------------ AMP
    fileRow (modelFileLabel, modelNameButton, modelClearButton, kAmpModelX, kModelFileTop);
    modelInfoLabel.setBounds (kAmpModelX, kModelFileTop + kCaptionH + kFileBoxH + 4, kFileW, 18);

    {
        const std::pair<juce::Slider*, juce::Label*> ampCells[] = {
            { &qualitySlider, &qualityLabel },
            { &inputGainSlider, &inputGainLabel },
            { &aidaParam1Slider, &aidaParam1Label },
            { &aidaParam2Slider, &aidaParam2Label },
            { &outputGainSlider, &outputGainLabel },
        };

        int x = kAmpKnobsX;
        for (const auto& cell : ampCells)
        {
            knobCell (*cell.first, *cell.second, x, kAmpRow, kCell);
            x += kCell;
        }
    }

    // ------------------------------------------------------------------- EQ
    {
        int x = kEqRowX;
        comboCell (toneStackBox, toneStackLabel, x, kEqRow, kStackCell);      x += kStackCell;
        switchCell (toneOnButton, toneOnLabel, x, kEqRow, kEqCell);           x += kEqCell;
        comboCell (tonePositionBox, tonePositionLabel, x, kEqRow, kEqCell);   x += kEqCell;
        knobCell (bassSlider, bassLabel, x, kEqRow, kEqCell);                 x += kEqCell;
        knobCell (midSlider, midLabel, x, kEqRow, kEqCell);                   x += kEqCell;
        knobCell (trebleSlider, trebleLabel, x, kEqRow, kEqCell);             x += kEqCell;
        switchCell (toneCompButton, toneCompLabel, x, kEqRow, kEqCell);       x += kEqCell;
        switchCell (geqOnButton, geqOnLabel, x, kEqRow, kEqCell);             x += kEqCell;
        comboCell (geqPositionBox, geqPositionLabel, x, kEqRow, kEqCell);     x += kEqCell;

        for (int band = 0; band < nsdsp::GraphicEQ::numBands; ++band)
        {
            faderCell (geqSliders[band], geqLabels[band], x, kEqRow, kFaderCell);
            x += kFaderCell;
        }
    }

    // ------------------------------------------------------------------ CAB
    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        auto& slot = irSlots[i];
        const auto slotX = kCabRowX + i * kSlotW;

        fileRow (slot.fileLabel, slot.nameButton, slot.clearButton, slotX, kIrFileTop);
        switchCell (slot.onButton, slot.onLabel, slotX, kCabRow, kCell);
        knobCell (slot.gainSlider, slot.gainLabel, slotX + kCell, kCabRow, kCell);
        knobCell (slot.panSlider, slot.panLabel, slotX + 2 * kCell, kCabRow, kCell);
    }

    // --------------------------------------------------------------- SPREAD
    {
        int x = kSpreadRowX;
        switchCell (spreadOnButton, spreadOnLabel, x, kSpreadRow, kCell);                     x += kCell;
        knobCell (spreadOffsetSlider, spreadOffsetLabel, x, kSpreadRow, kCell);               x += kCell;
        knobCell (spreadWobbleSlider, spreadWobbleLabel, x, kSpreadRow, kCell);               x += kCell;
        switchCell (spreadWobbleOnButton, spreadWobbleOnLabel, x, kSpreadRow, kCell);         x += kCell;
        knobCell (spreadCrossoverSlider, spreadCrossoverLabel, x, kSpreadRow, kCell);         x += kCell;
        switchCell (spreadCrossoverOnButton, spreadCrossoverOnLabel, x, kSpreadRow, kCell);   x += kCell;
        switchCell (spreadDiffuseOnButton, spreadDiffuseOnLabel, x, kSpreadRow, kCell);
    }
}
