#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "NamStackLookAndFeel.h"
#include "PluginProcessor.h"

// Desktop face of the MOD pedal.
//
// Same four sections in the same order (AMP / EQ / CAB / SPREAD), same rows,
// same controls in the same cells, same palette and widget shapes: the layout
// constants in PluginEditor.cpp are the ones in
// mod/modgui/stylesheet-namstack-mod.css, and the widgets are drawn by
// NamStackLookAndFeel from the same geometry as the pedal's film strips. Edit
// one and edit the other.
//
// Three cells differ, and only in the widget, never in the position: the tone
// stack model and the two Pre/Post enumerations are combo boxes here rather
// than a knob and two switches with a text readout. The pedal makes them knobs
// and switches so they can be addressed to hardware controls; a desktop editor
// has a mouse, and a ten-way list is a list. The stack's cell is widened to fit
// the model names, which is the pedal's one departure from a uniform grid.
class NamStackAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::ChangeListener
{
public:
    explicit NamStackAudioProcessorEditor (NamStackAudioProcessor&);
    ~NamStackAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshFileLabels();

    void chooseModelFile();
    void chooseIRFile (int slot);

    // One control cell of the pedal grid: a caption on top, the widget under
    // it, and (for knobs and faders) the value readout the widget draws itself.
    // The value readouts' format is the parameter's (a SliderAttachment routes
    // the slider's text through RangedAudioParameter::getText), so it is set
    // once in createParameterLayout and not here. Only the unit suffix, which
    // the Slider appends itself, is an editor concern.
    void addKnob (juce::Slider&, juce::Label&, const juce::String& caption,
                  const juce::String& suffix = {});
    void addFader (juce::Slider&, juce::Label&, const juce::String& caption,
                   const juce::String& suffix = {});
    void addSwitch (juce::ToggleButton&, juce::Label&, const juce::String& caption,
                    const juce::String& tooltip = {});
    void addCombo (juce::ComboBox&, juce::Label&, const juce::String& caption,
                   const juce::String& paramID);
    void addFileRow (juce::Label& caption, juce::TextButton& name, juce::TextButton& clear,
                     const juce::String& captionText);

    NamStackAudioProcessor& processor;
    NamStackLookAndFeel lookAndFeel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // ---------------------------------------------------------------- AMP
    juce::Label modelFileLabel, modelInfoLabel;
    juce::TextButton modelNameButton, modelClearButton { "X" };

    juce::Slider qualitySlider, inputGainSlider, aidaParam1Slider, aidaParam2Slider, outputGainSlider;
    juce::Label qualityLabel, inputGainLabel, aidaParam1Label, aidaParam2Label, outputGainLabel;
    std::unique_ptr<SliderAttachment> qualityAttachment, inputGainAttachment, aidaParam1Attachment,
                                      aidaParam2Attachment, outputGainAttachment;

    // ----------------------------------------------------------------- EQ
    juce::ComboBox toneStackBox, tonePositionBox, geqPositionBox;
    juce::Label toneStackLabel, tonePositionLabel, geqPositionLabel;
    std::unique_ptr<ComboAttachment> toneStackAttachment, tonePositionAttachment, geqPositionAttachment;

    juce::ToggleButton toneOnButton, toneCompButton, geqOnButton;
    juce::Label toneOnLabel, toneCompLabel, geqOnLabel;
    std::unique_ptr<ButtonAttachment> toneOnAttachment, toneCompAttachment, geqOnAttachment;

    juce::Slider bassSlider, midSlider, trebleSlider;
    juce::Label bassLabel, midLabel, trebleLabel;
    std::unique_ptr<SliderAttachment> bassAttachment, midAttachment, trebleAttachment;

    juce::Slider geqSliders[nsdsp::GraphicEQ::numBands];
    juce::Label geqLabels[nsdsp::GraphicEQ::numBands];
    std::unique_ptr<SliderAttachment> geqAttachments[nsdsp::GraphicEQ::numBands];

    // ---------------------------------------------------------------- CAB
    struct IRSlot
    {
        juce::Label fileLabel, onLabel, gainLabel, panLabel;
        juce::TextButton nameButton, clearButton { "X" };
        juce::ToggleButton onButton;
        juce::Slider gainSlider, panSlider;
        std::unique_ptr<ButtonAttachment> onAttachment;
        std::unique_ptr<SliderAttachment> gainAttachment, panAttachment;
    };

    IRSlot irSlots[nsdsp::IRStack::numSlots];

    // ------------------------------------------------------------- SPREAD
    juce::ToggleButton spreadOnButton, spreadWobbleOnButton, spreadCrossoverOnButton,
                       spreadDiffuseOnButton;
    juce::Label spreadOnLabel, spreadWobbleOnLabel, spreadCrossoverOnLabel, spreadDiffuseOnLabel;
    std::unique_ptr<ButtonAttachment> spreadOnAttachment, spreadWobbleOnAttachment,
                                      spreadCrossoverOnAttachment, spreadDiffuseOnAttachment;

    juce::Slider spreadOffsetSlider, spreadWobbleSlider, spreadCrossoverSlider;
    juce::Label spreadOffsetLabel, spreadWobbleLabel, spreadCrossoverLabel;
    std::unique_ptr<SliderAttachment> spreadOffsetAttachment, spreadWobbleAttachment,
                                      spreadCrossoverAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Several switches gate a section rather than an effect (Level Comp, the
    // three Spread deck powers); without a tooltip window their setTooltip
    // calls would be dead code.
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamStackAudioProcessorEditor)
};
