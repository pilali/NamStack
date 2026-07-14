#include "PluginEditor.h"

#include "ParamIDs.h"

namespace
{
void setupRotary (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
}

void setupCaption (juce::Label& label)
{
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (13.0f));
    label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
}
} // namespace

NamStackAudioProcessorEditor::NamStackAudioProcessorEditor (NamStackAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    auto& apvts = processor.apvts;

    // ------------------------------------------------------------ amp model
    addAndMakeVisible (ampGroup);
    addAndMakeVisible (loadModelButton);
    addAndMakeVisible (clearModelButton);
    addAndMakeVisible (modelNameLabel);
    addAndMakeVisible (modelInfoLabel);

    loadModelButton.onClick = [this] { chooseModelFile(); };
    clearModelButton.onClick = [this] { processor.clearModel(); };
    modelNameLabel.setJustificationType (juce::Justification::centredLeft);
    modelInfoLabel.setJustificationType (juce::Justification::centredLeft);
    modelInfoLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

    setupRotary (inputGainSlider);
    setupRotary (outputGainSlider);
    setupCaption (inputGainLabel);
    setupCaption (outputGainLabel);
    addAndMakeVisible (inputGainSlider);
    addAndMakeVisible (outputGainSlider);
    addAndMakeVisible (inputGainLabel);
    addAndMakeVisible (outputGainLabel);
    inputGainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::inputGain, inputGainSlider);
    outputGainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::outputGain, outputGainSlider);

    setupRotary (aidaParam1Slider);
    setupRotary (aidaParam2Slider);
    setupCaption (aidaParam1Label);
    setupCaption (aidaParam2Label);
    addAndMakeVisible (aidaParam1Slider);
    addAndMakeVisible (aidaParam2Slider);
    addAndMakeVisible (aidaParam1Label);
    addAndMakeVisible (aidaParam2Label);
    aidaParam1Attachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::aidaParam1, aidaParam1Slider);
    aidaParam2Attachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::aidaParam2, aidaParam2Slider);

    setupRotary (qualitySlider);
    setupCaption (qualityLabel);
    addAndMakeVisible (qualitySlider);
    addAndMakeVisible (qualityLabel);
    qualityAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::modelQuality, qualitySlider);

    // -------------------------------------------- EQ: tone stack + graphic
    addAndMakeVisible (eqGroup);
    addAndMakeVisible (toneOnButton);
    addAndMakeVisible (toneStackBox);
    addAndMakeVisible (tonePositionBox);

    toneOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::tsOn, toneOnButton);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::tsModel)))
        toneStackBox.addItemList (choice->choices, 1);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::tsPosition)))
        tonePositionBox.addItemList (choice->choices, 1);

    toneStackAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::tsModel, toneStackBox);
    tonePositionAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::tsPosition, tonePositionBox);

    for (auto* slider : { &bassSlider, &midSlider, &trebleSlider })
    {
        setupRotary (*slider);
        addAndMakeVisible (*slider);
    }
    for (auto* label : { &bassLabel, &midLabel, &trebleLabel })
    {
        setupCaption (*label);
        addAndMakeVisible (*label);
    }
    bassAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::tsBass, bassSlider);
    midAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::tsMid, midSlider);
    trebleAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::tsTreble, trebleSlider);

    addAndMakeVisible (geqOnButton);
    addAndMakeVisible (geqPositionBox);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::geqPosition)))
        geqPositionBox.addItemList (choice->choices, 1);

    geqOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::geqOn, geqOnButton);
    geqPositionAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::geqPosition, geqPositionBox);

    for (int band = 0; band < nsdsp::GraphicEQ::numBands; ++band)
    {
        const auto hz = nsdsp::GraphicEQ::getFrequencies()[(size_t) band];

        // Vertical faders, like the real thing.
        auto& slider = geqSliders[band];
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
        slider.setDoubleClickReturnValue (true, 0.0); // back to flat
        addAndMakeVisible (slider);

        geqLabels[band].setText (juce::String (juce::roundToInt (hz)) + " Hz", juce::dontSendNotification);
        setupCaption (geqLabels[band]);
        addAndMakeVisible (geqLabels[band]);

        geqAttachments[band] = std::make_unique<SliderAttachment> (
            apvts, ParamIDs::geqBand (band, hz), slider);
    }

    // -------------------------------------------------------------- IR slots
    addAndMakeVisible (irGroup);

    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        auto& row = irRows[i];

        addAndMakeVisible (row.onButton);
        addAndMakeVisible (row.loadButton);
        addAndMakeVisible (row.clearButton);
        addAndMakeVisible (row.nameLabel);
        addAndMakeVisible (row.gainSlider);
        addAndMakeVisible (row.panSlider);

        row.loadButton.onClick = [this, i] { chooseIRFile (i); };
        row.clearButton.onClick = [this, i] { processor.clearIR (i); };
        row.nameLabel.setJustificationType (juce::Justification::centredLeft);

        // Rotary level and pan under the slot's file line, as on the MOD pedal.
        setupRotary (row.gainSlider);
        row.gainSlider.setTextValueSuffix (" dB");
        setupRotary (row.panSlider);
        setupCaption (row.gainLabel);
        setupCaption (row.panLabel);
        addAndMakeVisible (row.gainLabel);
        addAndMakeVisible (row.panLabel);

        row.onAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::irOn (i), row.onButton);
        row.gainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::irGain (i), row.gainSlider);
        row.panAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::irPan (i), row.panSlider);
    }

    // --------------------------------------------------------------- doubler
    addAndMakeVisible (doublerGroup);
    addAndMakeVisible (doublerOnButton);
    doublerOnAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::dblOn, doublerOnButton);

    struct DoublerControl
    {
        juce::Slider* slider;
        juce::Label* label;
        const char* paramID;
        std::unique_ptr<SliderAttachment>* attachment;
    };

    DoublerControl doublerControls[] = {
        { &doublerMixSlider, &doublerMixLabel, ParamIDs::dblMix, &doublerMixAttachment },
        { &doublerTimeSlider, &doublerTimeLabel, ParamIDs::dblTime, &doublerTimeAttachment },
        { &doublerDetuneSlider, &doublerDetuneLabel, ParamIDs::dblDetune, &doublerDetuneAttachment },
        { &doublerHumanizeSlider, &doublerHumanizeLabel, ParamIDs::dblHumanize, &doublerHumanizeAttachment },
        { &doublerWidthSlider, &doublerWidthLabel, ParamIDs::dblWidth, &doublerWidthAttachment },
    };

    for (auto& control : doublerControls)
    {
        setupRotary (*control.slider);
        setupCaption (*control.label);
        addAndMakeVisible (*control.slider);
        addAndMakeVisible (*control.label);
        *control.attachment = std::make_unique<SliderAttachment> (apvts, control.paramID, *control.slider);
    }

    processor.fileStateChanged.addChangeListener (this);
    refreshFileLabels();

    setSize (1140, 812); // four bands, laid out like the MOD pedal
}

NamStackAudioProcessorEditor::~NamStackAudioProcessorEditor()
{
    processor.fileStateChanged.removeChangeListener (this);
}

void NamStackAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFileLabels();
}

void NamStackAudioProcessorEditor::refreshFileLabels()
{
    const auto modelName = processor.getModelName();
    modelNameLabel.setText (modelName.isNotEmpty() ? modelName : "<no model>",
                            juce::dontSendNotification);
    modelInfoLabel.setText (processor.getModelInfo(), juce::dontSendNotification);

    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        const auto irName = processor.getIRName (i);
        irRows[i].nameLabel.setText (irName.isNotEmpty() ? irName : "<empty>",
                                     juce::dontSendNotification);
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
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // Title, feature list and brand share one text baseline, as on the MOD
    // pedal's header.
    const float baseline = 32.0f;

    auto titleFont = juce::Font (juce::FontOptions (24.0f, juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.setFont (titleFont);
    g.drawSingleLineText ("NamStack", 20, (int) baseline);

    auto subFont = juce::Font (juce::FontOptions (13.0f));
    g.setColour (juce::Colours::grey);
    g.setFont (subFont);
    g.drawSingleLineText (juce::String (juce::CharPointer_UTF8 (
                              "NAM \xc2\xb7 AIDA-X \xc2\xb7 TONE STACK \xc2\xb7 5-BAND EQ "
                              "\xc2\xb7 IR MIXER \xc2\xb7 DOUBLER")),
                          20 + juce::GlyphArrangement::getStringWidthInt (titleFont, "NamStack") + 24,
                          (int) baseline);
    g.drawSingleLineText ("Pilali", getWidth() - 20, (int) baseline,
                          juce::Justification::right);
}

void NamStackAudioProcessorEditor::resized()
{
    // Four bands in the MOD pedal's order and shape: AMP (model + quality and
    // gain staging), EQ (tone stack then graphic EQ on one line), CAB (one
    // column per IR slot: file line above on / level / pan), DOUBLER.
    auto bounds = getLocalBounds().reduced (12);
    bounds.removeFromTop (34); // header

    const int knobWidth = 96;

    auto placeKnob = [] (juce::Rectangle<int>& area, int width,
                         juce::Slider& slider, juce::Label& label)
    {
        auto cell = area.removeFromLeft (width);
        label.setBounds (cell.removeFromTop (16));
        slider.setBounds (cell);
    };

    // ------------------------------------------------------------------ AMP
    auto ampArea = bounds.removeFromTop (140);
    ampGroup.setBounds (ampArea);
    auto ampInner = ampArea.reduced (14, 22);

    // model column on the left, as the pedal's file selector
    auto modelColumn = ampInner.removeFromLeft (330);
    auto modelButtons = modelColumn.removeFromTop (26);
    loadModelButton.setBounds (modelButtons.removeFromLeft (130));
    modelButtons.removeFromLeft (8);
    clearModelButton.setBounds (modelButtons.removeFromLeft (70));
    modelColumn.removeFromTop (8);
    modelNameLabel.setBounds (modelColumn.removeFromTop (24));
    modelInfoLabel.setBounds (modelColumn.removeFromTop (20));

    ampInner.removeFromLeft (24);

    // pedal order: quality, input, param 1, param 2, output
    placeKnob (ampInner, knobWidth, qualitySlider, qualityLabel);
    placeKnob (ampInner, knobWidth, inputGainSlider, inputGainLabel);
    placeKnob (ampInner, knobWidth, aidaParam1Slider, aidaParam1Label);
    placeKnob (ampInner, knobWidth, aidaParam2Slider, aidaParam2Label);
    placeKnob (ampInner, knobWidth, outputGainSlider, outputGainLabel);

    bounds.removeFromTop (8);

    // ------------------------------------------------------------------- EQ
    // tone stack then graphic EQ on a single line, as on the pedal
    auto eqArea = bounds.removeFromTop (190);
    eqGroup.setBounds (eqArea);
    auto eqInner = eqArea.reduced (14, 22);

    auto tsColumn = eqInner.removeFromLeft (230);
    toneOnButton.setBounds (tsColumn.removeFromTop (24));
    tsColumn.removeFromTop (8);
    toneStackBox.setBounds (tsColumn.removeFromTop (26));
    tsColumn.removeFromTop (8);
    tonePositionBox.setBounds (tsColumn.removeFromTop (26));

    eqInner.removeFromLeft (16);
    placeKnob (eqInner, knobWidth, bassSlider, bassLabel);
    placeKnob (eqInner, knobWidth, midSlider, midLabel);
    placeKnob (eqInner, knobWidth, trebleSlider, trebleLabel);

    eqInner.removeFromLeft (16);
    auto geqColumn = eqInner.removeFromLeft (170);
    geqOnButton.setBounds (geqColumn.removeFromTop (24));
    geqColumn.removeFromTop (8);
    geqPositionBox.setBounds (geqColumn.removeFromTop (26));

    eqInner.removeFromLeft (16);
    const auto faderWidth = eqInner.getWidth() / nsdsp::GraphicEQ::numBands;

    for (int band = 0; band < nsdsp::GraphicEQ::numBands; ++band)
    {
        auto area = eqInner.removeFromLeft (faderWidth);
        geqLabels[band].setBounds (area.removeFromTop (16));
        geqSliders[band].setBounds (area);
    }

    bounds.removeFromTop (8);

    // ------------------------------------------------------------------ CAB
    // one column per IR slot: file line above on / level / pan
    auto irArea = bounds.removeFromTop (216);
    irGroup.setBounds (irArea);
    auto irInner = irArea.reduced (14, 22);

    const auto slotWidth = irInner.getWidth() / nsdsp::IRStack::numSlots;

    for (auto& row : irRows)
    {
        auto slot = irInner.removeFromLeft (slotWidth).reduced (6, 0);

        auto fileLine = slot.removeFromTop (24);
        row.loadButton.setBounds (fileLine.removeFromLeft (86));
        fileLine.removeFromLeft (6);
        row.clearButton.setBounds (fileLine.removeFromLeft (26));
        slot.removeFromTop (4);
        row.nameLabel.setBounds (slot.removeFromTop (20));
        slot.removeFromTop (4);

        row.onButton.setBounds (slot.removeFromLeft (52)
                                    .withHeight (26)
                                    .translated (0, slot.getHeight() / 2 - 13));
        placeKnob (slot, (slot.getWidth()) / 2, row.gainSlider, row.gainLabel);
        placeKnob (slot, slot.getWidth(), row.panSlider, row.panLabel);
    }

    bounds.removeFromTop (8);

    // -------------------------------------------------------------- DOUBLER
    auto doublerArea = bounds.removeFromTop (150);
    doublerGroup.setBounds (doublerArea);
    auto doublerInner = doublerArea.reduced (14, 22);

    // centred like the pedal's doubler row
    const int doublerWidth = 60 + 5 * knobWidth;
    doublerInner.removeFromLeft ((doublerInner.getWidth() - doublerWidth) / 2);

    doublerOnButton.setBounds (doublerInner.removeFromLeft (60)
                                   .withHeight (26)
                                   .translated (0, doublerInner.getHeight() / 2 - 13));

    placeKnob (doublerInner, knobWidth, doublerMixSlider, doublerMixLabel);
    placeKnob (doublerInner, knobWidth, doublerTimeSlider, doublerTimeLabel);
    placeKnob (doublerInner, knobWidth, doublerDetuneSlider, doublerDetuneLabel);
    placeKnob (doublerInner, knobWidth, doublerHumanizeSlider, doublerHumanizeLabel);
    placeKnob (doublerInner, knobWidth, doublerWidthSlider, doublerWidthLabel);
}
