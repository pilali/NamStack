#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

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

    NamStackAudioProcessor& processor;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // ------------------------------------------------------------ amp model
    juce::GroupComponent ampGroup { {}, "AMP MODEL  (NAM / AIDA-X)" };
    juce::TextButton loadModelButton { "Load model..." };
    juce::TextButton clearModelButton { "Clear" };
    juce::Label modelNameLabel, modelInfoLabel;

    juce::Slider inputGainSlider, outputGainSlider;
    juce::Label inputGainLabel { {}, "Input" }, outputGainLabel { {}, "Output" };
    std::unique_ptr<SliderAttachment> inputGainAttachment, outputGainAttachment;

    juce::Slider aidaParam1Slider, aidaParam2Slider;
    juce::Label aidaParam1Label { {}, "Param 1" }, aidaParam2Label { {}, "Param 2" };
    std::unique_ptr<SliderAttachment> aidaParam1Attachment, aidaParam2Attachment;

    // ------------------------------------------------------------ tone stack
    juce::GroupComponent toneGroup { {}, "TONE STACK" };
    juce::ComboBox toneStackBox, tonePositionBox;
    juce::Slider bassSlider, midSlider, trebleSlider;
    juce::Label bassLabel { {}, "Bass" }, midLabel { {}, "Middle" }, trebleLabel { {}, "Treble" };
    std::unique_ptr<ComboAttachment> toneStackAttachment, tonePositionAttachment;
    std::unique_ptr<SliderAttachment> bassAttachment, midAttachment, trebleAttachment;

    // -------------------------------------------------------------- IR slots
    juce::GroupComponent irGroup { {}, "CABINET IMPULSE RESPONSES" };

    struct IRRow
    {
        juce::ToggleButton onButton;
        juce::TextButton loadButton { "Load IR..." };
        juce::TextButton clearButton { "X" };
        juce::Label nameLabel;
        juce::Slider gainSlider, panSlider;
        std::unique_ptr<ButtonAttachment> onAttachment;
        std::unique_ptr<SliderAttachment> gainAttachment, panAttachment;
    };

    IRRow irRows[nsdsp::IRStack::numSlots];

    // --------------------------------------------------------------- doubler
    juce::GroupComponent doublerGroup { {}, "DOUBLER" };
    juce::ToggleButton doublerOnButton { "On" };
    juce::Slider doublerMixSlider, doublerTimeSlider, doublerDetuneSlider,
                 doublerHumanizeSlider, doublerWidthSlider;
    juce::Label doublerMixLabel { {}, "Mix" }, doublerTimeLabel { {}, "Time" },
                doublerDetuneLabel { {}, "Detune" }, doublerHumanizeLabel { {}, "Humanize" },
                doublerWidthLabel { {}, "Width" };
    std::unique_ptr<ButtonAttachment> doublerOnAttachment;
    std::unique_ptr<SliderAttachment> doublerMixAttachment, doublerTimeAttachment,
                                      doublerDetuneAttachment, doublerHumanizeAttachment,
                                      doublerWidthAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamStackAudioProcessorEditor)
};
