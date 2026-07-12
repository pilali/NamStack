#include "PluginProcessor.h"

#include "ParamIDs.h"
#include "PluginEditor.h"

namespace
{
constexpr auto modelPathProperty = "modelPath";

juce::String irPathProperty (int slot)
{
    return "irPath" + juce::String (slot + 1);
}
} // namespace

NamStackAudioProcessor::NamStackAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    pInGain = apvts.getRawParameterValue (ParamIDs::inputGain);
    pOutGain = apvts.getRawParameterValue (ParamIDs::outputGain);
    pTsModel = apvts.getRawParameterValue (ParamIDs::tsModel);
    pTsPosition = apvts.getRawParameterValue (ParamIDs::tsPosition);
    pTsBass = apvts.getRawParameterValue (ParamIDs::tsBass);
    pTsMid = apvts.getRawParameterValue (ParamIDs::tsMid);
    pTsTreble = apvts.getRawParameterValue (ParamIDs::tsTreble);

    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        pIrOn[i] = apvts.getRawParameterValue (ParamIDs::irOn (i));
        pIrGain[i] = apvts.getRawParameterValue (ParamIDs::irGain (i));
        pIrPan[i] = apvts.getRawParameterValue (ParamIDs::irPan (i));
    }

    pDblOn = apvts.getRawParameterValue (ParamIDs::dblOn);
    pDblMix = apvts.getRawParameterValue (ParamIDs::dblMix);
    pDblTime = apvts.getRawParameterValue (ParamIDs::dblTime);
    pDblDetune = apvts.getRawParameterValue (ParamIDs::dblDetune);
    pDblHumanize = apvts.getRawParameterValue (ParamIDs::dblHumanize);
    pDblWidth = apvts.getRawParameterValue (ParamIDs::dblWidth);
}

NamStackAudioProcessor::~NamStackAudioProcessor()
{
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout NamStackAudioProcessor::createParameterLayout()
{
    using FloatParam = juce::AudioParameterFloat;
    using BoolParam = juce::AudioParameterBool;
    using ChoiceParam = juce::AudioParameterChoice;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto id = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };

    params.push_back (std::make_unique<FloatParam> (
        id (ParamIDs::inputGain), "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<FloatParam> (
        id (ParamIDs::outputGain), "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    juce::StringArray toneStackNames;
    for (const auto& m : nsdsp::ToneStack::getModels())
        toneStackNames.add (m.name);

    params.push_back (std::make_unique<ChoiceParam> (id (ParamIDs::tsModel), "Tone Stack", toneStackNames, 0));
    params.push_back (std::make_unique<ChoiceParam> (id (ParamIDs::tsPosition), "Tone Stack Position",
                                                     juce::StringArray { "Pre (before amp)", "Post (after amp)" }, 1));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::tsBass), "Bass",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::tsMid), "Middle",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::tsTreble), "Treble",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        const auto n = juce::String (i + 1);
        params.push_back (std::make_unique<BoolParam> (id (ParamIDs::irOn (i)), "IR " + n + " On", false));
        params.push_back (std::make_unique<FloatParam> (
            id (ParamIDs::irGain (i)), "IR " + n + " Level",
            juce::NormalisableRange<float> (-40.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
        params.push_back (std::make_unique<FloatParam> (id (ParamIDs::irPan (i)), "IR " + n + " Pan",
                                                        juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    }

    params.push_back (std::make_unique<BoolParam> (id (ParamIDs::dblOn), "Doubler On", false));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::dblMix), "Doubler Mix",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<FloatParam> (
        id (ParamIDs::dblTime), "Doubler Time",
        juce::NormalisableRange<float> (5.0f, 50.0f, 0.1f), 18.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    params.push_back (std::make_unique<FloatParam> (
        id (ParamIDs::dblDetune), "Doubler Detune",
        juce::NormalisableRange<float> (0.0f, 25.0f, 0.1f), 9.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ct")));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::dblHumanize), "Doubler Humanize",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::dblWidth), "Doubler Width",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    return { params.begin(), params.end() };
}

bool NamStackAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void NamStackAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    monoBuffer.setSize (1, samplesPerBlock);
    stereoBuffer.setSize (2, samplesPerBlock);

    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);

    toneStack.prepare (sampleRate);
    irStack.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 1 });
    doubler.prepare (sampleRate, samplesPerBlock);

    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        if (model != nullptr && model->isLoaded())
            model->prepare (sampleRate, samplesPerBlock);
    }

    prepared = true;
    updateLatency();
}

void NamStackAudioProcessor::releaseResources()
{
    irStack.reset();
    doubler.reset();
    toneStack.reset();
}

void NamStackAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numIn = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    if (numSamples == 0 || numOut == 0)
        return;

    // Defensive: some hosts exceed the block size announced in prepareToPlay.
    if (numSamples > monoBuffer.getNumSamples())
    {
        monoBuffer.setSize (1, numSamples, false, false, true);
        stereoBuffer.setSize (2, numSamples, false, false, true);
    }

    // ---------------------------------------------------------- mono source
    auto* mono = monoBuffer.getWritePointer (0);

    if (numIn >= 2)
    {
        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (l[i] + r[i]);
    }
    else if (numIn == 1)
    {
        juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);
    }
    else
    {
        juce::FloatVectorOperations::clear (mono, numSamples);
    }

    // ----------------------------------------------------------- input gain
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (pInGain->load()));
    inputGain.applyGain (mono, numSamples);

    // ------------------------------------------------- tone stack (pre) ---
    const auto tsModelIndex = (int) pTsModel->load();
    const bool tsIsPre = ((int) pTsPosition->load()) == 0;
    toneStack.setParams (tsModelIndex, pTsBass->load(), pTsMid->load(), pTsTreble->load());

    if (tsIsPre)
        toneStack.processBlock (mono, numSamples);

    // ---------------------------------------------------------- amp model
    {
        const juce::SpinLock::ScopedTryLockType tl (modelLock);
        if (tl.isLocked() && model != nullptr && model->isLoaded())
            model->process (mono, numSamples);
    }

    // ------------------------------------------------- tone stack (post) --
    if (! tsIsPre)
        toneStack.processBlock (mono, numSamples);

    // ------------------------------------------------------------- IR mix
    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
        irStack.setSlotParams (i, pIrOn[i]->load() > 0.5f, pIrGain[i]->load(), pIrPan[i]->load());

    irStack.process (mono, stereoBuffer, numSamples);

    // ------------------------------------------------------------- doubler
    doubler.setParams (pDblOn->load() > 0.5f,
                       pDblTime->load(),
                       pDblDetune->load(),
                       pDblHumanize->load(),
                       pDblWidth->load(),
                       pDblMix->load());
    doubler.process (stereoBuffer, numSamples);

    // --------------------------------------------------------- output gain
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (pOutGain->load()));
    {
        auto* l = stereoBuffer.getWritePointer (0);
        auto* r = stereoBuffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto g = outputGain.getNextValue();
            l[i] *= g;
            r[i] *= g;
        }
    }

    if (numOut >= 2)
    {
        buffer.copyFrom (0, 0, stereoBuffer, 0, 0, numSamples);
        buffer.copyFrom (1, 0, stereoBuffer, 1, 0, numSamples);
        for (int ch = 2; ch < numOut; ++ch)
            buffer.clear (ch, 0, numSamples);
    }
    else
    {
        auto* out = buffer.getWritePointer (0);
        const auto* l = stereoBuffer.getReadPointer (0);
        const auto* r = stereoBuffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
            out[i] = 0.5f * (l[i] + r[i]);
    }
}

// --------------------------------------------------------------- file loading

bool NamStackAudioProcessor::loadModelFile (const juce::File& file, juce::String& errorMessage)
{
    auto newModel = std::make_unique<nsdsp::NeuralModel>();

    if (! newModel->loadFile (file, errorMessage))
        return false;

    if (prepared)
        newModel->prepare (currentSampleRate, currentBlockSize);

    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        std::swap (model, newModel);
    }
    newModel.reset(); // destroy the previous model outside the lock

    apvts.state.setProperty (modelPathProperty, file.getFullPathName(), nullptr);
    updateLatency();
    fileStateChanged.sendChangeMessage();
    return true;
}

void NamStackAudioProcessor::clearModel()
{
    std::unique_ptr<nsdsp::NeuralModel> oldModel;
    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        std::swap (model, oldModel);
    }
    oldModel.reset();

    apvts.state.removeProperty (modelPathProperty, nullptr);
    updateLatency();
    fileStateChanged.sendChangeMessage();
}

juce::String NamStackAudioProcessor::getModelName() const
{
    const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (modelLock));
    return model != nullptr ? model->getName() : juce::String();
}

juce::String NamStackAudioProcessor::getModelInfo() const
{
    const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (modelLock));

    if (model == nullptr || ! model->isLoaded())
        return "No model loaded";

    juce::String info = model->getType() == nsdsp::NeuralModel::Type::nam ? "NAM" : "AIDA-X / RTNeural";
    info << " @ " << juce::String (model->getModelSampleRate() / 1000.0, 1) << " kHz";
    return info;
}

void NamStackAudioProcessor::loadIRFile (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, nsdsp::IRStack::numSlots))
        return;

    irStack.loadIR (slot, file);
    apvts.state.setProperty (irPathProperty (slot), file.getFullPathName(), nullptr);

    // Loading an IR switches its slot on, which is almost always what the
    // user wants.
    if (auto* onParam = apvts.getParameter (ParamIDs::irOn (slot)))
        if (onParam->getValue() < 0.5f)
        {
            onParam->beginChangeGesture();
            onParam->setValueNotifyingHost (1.0f);
            onParam->endChangeGesture();
        }

    fileStateChanged.sendChangeMessage();
}

void NamStackAudioProcessor::clearIR (int slot)
{
    if (! juce::isPositiveAndBelow (slot, nsdsp::IRStack::numSlots))
        return;

    irStack.clearIR (slot);
    apvts.state.removeProperty (irPathProperty (slot), nullptr);
    fileStateChanged.sendChangeMessage();
}

void NamStackAudioProcessor::updateLatency()
{
    int latency = 0;
    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        if (model != nullptr && model->isLoaded())
            latency = model->getLatencySamples();
    }
    setLatencySamples (latency);
}

// ---------------------------------------------------------------------- state

void NamStackAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NamStackAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            triggerAsyncUpdate(); // reload model / IR files on the message thread
        }
}

void NamStackAudioProcessor::handleAsyncUpdate()
{
    const auto modelPath = apvts.state.getProperty (modelPathProperty).toString();
    if (modelPath.isNotEmpty())
    {
        juce::String error;
        if (! loadModelFile (juce::File (modelPath), error))
            clearModel();
    }
    else
    {
        clearModel();
    }

    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        const auto irPath = apvts.state.getProperty (irPathProperty (i)).toString();
        if (irPath.isNotEmpty() && juce::File (irPath).existsAsFile())
            irStack.loadIR (i, juce::File (irPath));
        else
            irStack.clearIR (i);
    }

    fileStateChanged.sendChangeMessage();
}

juce::AudioProcessorEditor* NamStackAudioProcessor::createEditor()
{
    return new NamStackAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NamStackAudioProcessor();
}
