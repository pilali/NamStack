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

    // Group titles mirror the MOD pedal's section rules: AMP / EQ / CAB /
    // SPREAD, in the same order and with the same row contents.

    // ------------------------------------------------------------ amp model
    juce::GroupComponent ampGroup { {}, "AMP" };
    juce::TextButton loadModelButton { "Load model..." };
    juce::TextButton clearModelButton { "Clear" };
    juce::Label modelNameLabel, modelInfoLabel;

    juce::Slider inputGainSlider, outputGainSlider;
    juce::Label inputGainLabel { {}, "Input" }, outputGainLabel { {}, "Output" };
    std::unique_ptr<SliderAttachment> inputGainAttachment, outputGainAttachment;

    juce::Slider aidaParam1Slider, aidaParam2Slider;
    juce::Label aidaParam1Label { {}, "Param 1" }, aidaParam2Label { {}, "Param 2" };
    std::unique_ptr<SliderAttachment> aidaParam1Attachment, aidaParam2Attachment;

    juce::Slider qualitySlider;
    juce::Label qualityLabel { {}, "Quality" };
    std::unique_ptr<SliderAttachment> qualityAttachment;

    // -------------------------------------------- EQ: tone stack + graphic
    juce::GroupComponent eqGroup { {}, "EQ" };
    juce::ToggleButton toneOnButton { "Stack On" };
    juce::ToggleButton toneCompButton { "Level Comp" };
    juce::ComboBox toneStackBox, tonePositionBox;
    juce::Slider bassSlider, midSlider, trebleSlider;
    juce::Label bassLabel { {}, "Bass" }, midLabel { {}, "Middle" }, trebleLabel { {}, "Treble" };
    std::unique_ptr<ButtonAttachment> toneOnAttachment, toneCompAttachment;
    std::unique_ptr<ComboAttachment> toneStackAttachment, tonePositionAttachment;
    std::unique_ptr<SliderAttachment> bassAttachment, midAttachment, trebleAttachment;

    // Mesa/Boogie-style 5-band, with its own on/off and its own pre/post,
    // sharing the EQ row with the tone stack as on the MOD pedal.
    juce::ToggleButton geqOnButton { "EQ On" };
    juce::ComboBox geqPositionBox;
    juce::Slider geqSliders[nsdsp::GraphicEQ::numBands];
    juce::Label geqLabels[nsdsp::GraphicEQ::numBands];
    std::unique_ptr<ButtonAttachment> geqOnAttachment;
    std::unique_ptr<ComboAttachment> geqPositionAttachment;
    std::unique_ptr<SliderAttachment> geqAttachments[nsdsp::GraphicEQ::numBands];

    // -------------------------------------------------------------- IR slots
    juce::GroupComponent irGroup { {}, "CAB" };

    struct IRRow
    {
        juce::ToggleButton onButton { "On" };
        juce::TextButton loadButton { "Load IR..." };
        juce::TextButton clearButton { "X" };
        juce::Label nameLabel;
        juce::Slider gainSlider, panSlider;
        juce::Label gainLabel { {}, "Level" }, panLabel { {}, "Pan" };
        std::unique_ptr<ButtonAttachment> onAttachment;
        std::unique_ptr<SliderAttachment> gainAttachment, panAttachment;
    };

    IRRow irRows[nsdsp::IRStack::numSlots];

    // ---------------------------------------------------------------- spread
    // One musical knob (the signed Offset) plus the deck: Wobble depth and its
    // power, the crossover cutoff and its power, and the diffuser's power.
    juce::GroupComponent spreadGroup { {}, "SPREAD" };
    juce::ToggleButton spreadOnButton { "On" };
    juce::ToggleButton spreadWobbleOnButton { "Wobble" };
    juce::ToggleButton spreadCrossoverOnButton { "X-over" };
    juce::ToggleButton spreadDiffuseOnButton { "Diffuse" };
    juce::Slider spreadOffsetSlider, spreadWobbleSlider, spreadCrossoverSlider;
    juce::Label spreadOffsetLabel { {}, "Offset" }, spreadWobbleLabel { {}, "Wobble" },
                spreadCrossoverLabel { {}, "Crossover" };
    std::unique_ptr<ButtonAttachment> spreadOnAttachment, spreadWobbleOnAttachment,
                                      spreadCrossoverOnAttachment, spreadDiffuseOnAttachment;
    std::unique_ptr<SliderAttachment> spreadOffsetAttachment, spreadWobbleAttachment,
                                      spreadCrossoverAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Several switches here gate a section rather than an effect (Level Comp,
    // the three Spread deck powers); without a tooltip window their setTooltip
    // calls would be dead code.
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamStackAudioProcessorEditor)
};
