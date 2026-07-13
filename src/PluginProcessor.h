#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Doubler.h"
#include "dsp/IRStack.h"
#include "dsp/NeuralModel.h"
#include "dsp/ToneStack.h"

class NamStackAudioProcessor : public juce::AudioProcessor,
                               private juce::AsyncUpdater,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    NamStackAudioProcessor();
    ~NamStackAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ------------------------------------------------------------------ API
    // used by the editor (message thread only)
    bool loadModelFile (const juce::File& file, juce::String& errorMessage);
    void clearModel();
    juce::String getModelName() const;
    juce::String getModelInfo() const;
    int getNumModelConditioningInputs() const;

    void loadIRFile (int slot, const juce::File& file);
    void clearIR (int slot);
    juce::String getIRName (int slot) const { return irStack.getIRName (slot); }
    bool isIRLoaded (int slot) const { return irStack.isLoaded (slot); }

    juce::AudioProcessorValueTreeState apvts;

    // Fired whenever a model or IR was (un)loaded, so the editor can refresh.
    juce::ChangeBroadcaster fileStateChanged;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleAsyncUpdate() override; // restores model / IR files after setStateInformation
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void updateLatency();
    void applyModelQuality(); // message thread only

    // setSlimmableSize() is not realtime-safe: bounce quality changes to the
    // message thread (parameterChanged may fire from the audio thread).
    struct QualityApplier : juce::AsyncUpdater
    {
        explicit QualityApplier (NamStackAudioProcessor& p) : processor (p) {}
        void handleAsyncUpdate() override { processor.applyModelQuality(); }
        NamStackAudioProcessor& processor;
    };

    QualityApplier qualityApplier { *this };

    // ------------------------------------------------------------------ DSP
    nsdsp::ToneStack toneStack;
    nsdsp::IRStack irStack;
    nsdsp::Doubler doubler;

    juce::SpinLock modelLock;
    std::unique_ptr<nsdsp::NeuralModel> model; // guarded by modelLock in processBlock

    juce::AudioBuffer<float> monoBuffer;
    juce::AudioBuffer<float> stereoBuffer;
    juce::SmoothedValue<float> inputGain, outputGain;

    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;
    bool prepared = false;

    // --------------------------------------------------------- cached params
    std::atomic<float>* pInGain = nullptr;
    std::atomic<float>* pOutGain = nullptr;
    std::atomic<float>* pAidaParam1 = nullptr;
    std::atomic<float>* pAidaParam2 = nullptr;
    std::atomic<float>* pTsModel = nullptr;
    std::atomic<float>* pTsPosition = nullptr;
    std::atomic<float>* pTsBass = nullptr;
    std::atomic<float>* pTsMid = nullptr;
    std::atomic<float>* pTsTreble = nullptr;
    std::atomic<float>* pIrOn[nsdsp::IRStack::numSlots] = {};
    std::atomic<float>* pIrGain[nsdsp::IRStack::numSlots] = {};
    std::atomic<float>* pIrPan[nsdsp::IRStack::numSlots] = {};
    std::atomic<float>* pDblOn = nullptr;
    std::atomic<float>* pDblMix = nullptr;
    std::atomic<float>* pDblTime = nullptr;
    std::atomic<float>* pDblDetune = nullptr;
    std::atomic<float>* pDblHumanize = nullptr;
    std::atomic<float>* pDblWidth = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamStackAudioProcessor)
};
