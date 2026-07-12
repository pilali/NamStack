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

    // ------------------------------------------------------------ tone stack
    addAndMakeVisible (toneGroup);
    addAndMakeVisible (toneStackBox);
    addAndMakeVisible (tonePositionBox);

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

        row.gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        row.gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
        row.gainSlider.setTextValueSuffix (" dB");

        row.panSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        row.panSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 16);

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

    setSize (920, 700);
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

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("NamStack", 20, 8, 300, 30, juce::Justification::centredLeft);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("NAM / AIDA-X amp modeler  -  tone stack  -  IR mixer  -  doubler",
                200, 8, getWidth() - 220, 30, juce::Justification::centredRight);
}

void NamStackAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (12);
    bounds.removeFromTop (34); // header

    // ------------------------------------------------------------ amp model
    auto ampArea = bounds.removeFromTop (140);
    ampGroup.setBounds (ampArea);
    auto ampInner = ampArea.reduced (14, 22);

    auto knobsArea = ampInner.removeFromRight (440);

    auto placeAmpKnob = [&knobsArea] (juce::Slider& slider, juce::Label& label)
    {
        auto area = knobsArea.removeFromLeft (110);
        label.setBounds (area.removeFromBottom (16));
        slider.setBounds (area);
    };

    placeAmpKnob (aidaParam1Slider, aidaParam1Label);
    placeAmpKnob (aidaParam2Slider, aidaParam2Label);
    placeAmpKnob (inputGainSlider, inputGainLabel);
    placeAmpKnob (outputGainSlider, outputGainLabel);

    auto modelButtons = ampInner.removeFromTop (28);
    loadModelButton.setBounds (modelButtons.removeFromLeft (130));
    modelButtons.removeFromLeft (8);
    clearModelButton.setBounds (modelButtons.removeFromLeft (70));

    ampInner.removeFromTop (8);
    modelNameLabel.setBounds (ampInner.removeFromTop (24));
    modelInfoLabel.setBounds (ampInner.removeFromTop (20));

    bounds.removeFromTop (8);

    // ------------------------------------------------------------ tone stack
    auto toneArea = bounds.removeFromTop (150);
    toneGroup.setBounds (toneArea);
    auto toneInner = toneArea.reduced (14, 22);

    auto comboColumn = toneInner.removeFromLeft (260);
    toneStackBox.setBounds (comboColumn.removeFromTop (26));
    comboColumn.removeFromTop (10);
    tonePositionBox.setBounds (comboColumn.removeFromTop (26));

    toneInner.removeFromLeft (20);
    const auto knobWidth = juce::jmin (120, toneInner.getWidth() / 3);

    auto placeKnob = [&toneInner, knobWidth] (juce::Slider& slider, juce::Label& label)
    {
        auto area = toneInner.removeFromLeft (knobWidth);
        label.setBounds (area.removeFromBottom (16));
        slider.setBounds (area);
    };

    placeKnob (bassSlider, bassLabel);
    placeKnob (midSlider, midLabel);
    placeKnob (trebleSlider, trebleLabel);

    bounds.removeFromTop (8);

    // -------------------------------------------------------------- IR slots
    auto irArea = bounds.removeFromTop (172);
    irGroup.setBounds (irArea);
    auto irInner = irArea.reduced (14, 22);

    const auto rowHeight = irInner.getHeight() / nsdsp::IRStack::numSlots;

    for (auto& row : irRows)
    {
        auto rowArea = irInner.removeFromTop (rowHeight).reduced (0, 3);

        row.onButton.setBounds (rowArea.removeFromLeft (28));
        row.loadButton.setBounds (rowArea.removeFromLeft (86));
        rowArea.removeFromLeft (6);
        row.clearButton.setBounds (rowArea.removeFromLeft (26));
        rowArea.removeFromLeft (6);
        row.nameLabel.setBounds (rowArea.removeFromLeft (230));
        rowArea.removeFromLeft (8);
        row.gainSlider.setBounds (rowArea.removeFromLeft (230));
        rowArea.removeFromLeft (8);
        row.panSlider.setBounds (rowArea);
    }

    bounds.removeFromTop (8);

    // --------------------------------------------------------------- doubler
    auto doublerArea = bounds.removeFromTop (150);
    doublerGroup.setBounds (doublerArea);
    auto doublerInner = doublerArea.reduced (14, 22);

    doublerOnButton.setBounds (doublerInner.removeFromLeft (60).withHeight (26).translated (0, doublerInner.getHeight() / 2 - 13));

    const auto doublerKnobWidth = juce::jmin (130, doublerInner.getWidth() / 5);

    auto placeDoublerKnob = [&doublerInner, doublerKnobWidth] (juce::Slider& slider, juce::Label& label)
    {
        auto area = doublerInner.removeFromLeft (doublerKnobWidth);
        label.setBounds (area.removeFromBottom (16));
        slider.setBounds (area);
    };

    placeDoublerKnob (doublerMixSlider, doublerMixLabel);
    placeDoublerKnob (doublerTimeSlider, doublerTimeLabel);
    placeDoublerKnob (doublerDetuneSlider, doublerDetuneLabel);
    placeDoublerKnob (doublerHumanizeSlider, doublerHumanizeLabel);
    placeDoublerKnob (doublerWidthSlider, doublerWidthLabel);
}
