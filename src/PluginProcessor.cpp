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
    pAidaParam1 = apvts.getRawParameterValue (ParamIDs::aidaParam1);
    pAidaParam2 = apvts.getRawParameterValue (ParamIDs::aidaParam2);
    pTsOn = apvts.getRawParameterValue (ParamIDs::tsOn);
    pTsModel = apvts.getRawParameterValue (ParamIDs::tsModel);
    pTsPosition = apvts.getRawParameterValue (ParamIDs::tsPosition);
    pGeqOn = apvts.getRawParameterValue (ParamIDs::geqOn);
    pGeqPosition = apvts.getRawParameterValue (ParamIDs::geqPosition);
    for (int band = 0; band < nsdsp::GraphicEQ::numBands; ++band)
        pGeqBand[band] = apvts.getRawParameterValue (
            ParamIDs::geqBand (band, nsdsp::GraphicEQ::getFrequencies()[(size_t) band]));
    pTsBass = apvts.getRawParameterValue (ParamIDs::tsBass);
    pTsMid = apvts.getRawParameterValue (ParamIDs::tsMid);
    pTsTreble = apvts.getRawParameterValue (ParamIDs::tsTreble);
    pTsComp = apvts.getRawParameterValue (ParamIDs::tsComp);

    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        pIrOn[i] = apvts.getRawParameterValue (ParamIDs::irOn (i));
        pIrGain[i] = apvts.getRawParameterValue (ParamIDs::irGain (i));
        pIrPan[i] = apvts.getRawParameterValue (ParamIDs::irPan (i));
    }

    apvts.addParameterListener (ParamIDs::modelQuality, this);

    pSprOn = apvts.getRawParameterValue (ParamIDs::sprOn);
    pSprOffset = apvts.getRawParameterValue (ParamIDs::sprOffset);
    pSprWobble = apvts.getRawParameterValue (ParamIDs::sprWobble);
    pSprWobbleOn = apvts.getRawParameterValue (ParamIDs::sprWobbleOn);
    pSprCrossover = apvts.getRawParameterValue (ParamIDs::sprCrossover);
    pSprCrossoverOn = apvts.getRawParameterValue (ParamIDs::sprCrossoverOn);
    pSprDiffuseOn = apvts.getRawParameterValue (ParamIDs::sprDiffuseOn);
}

NamStackAudioProcessor::~NamStackAudioProcessor()
{
    apvts.removeParameterListener (ParamIDs::modelQuality, this);
    qualityApplier.cancelPendingUpdate();
    cancelPendingUpdate();
}

void NamStackAudioProcessor::parameterChanged (const juce::String&, float)
{
    qualityApplier.triggerAsyncUpdate();
}

void NamStackAudioProcessor::applyModelQuality()
{
    // The model pointer is only mutated on the message thread (which we are
    // on), and setSlimmableSize() is safe to call while the audio thread is
    // processing, so no lock is needed here.
    if (model != nullptr && model->isLoaded())
        model->setSlimmableSize ((double) apvts.getRawParameterValue (ParamIDs::modelQuality)->load());
}

juce::AudioProcessorValueTreeState::ParameterLayout NamStackAudioProcessor::createParameterLayout()
{
    using FloatParam = juce::AudioParameterFloat;
    using BoolParam = juce::AudioParameterBool;
    using ChoiceParam = juce::AudioParameterChoice;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto id = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };

    // Value readouts. JUCE derives the decimal count from the range's interval
    // and falls back to seven digits when a range has none, which is every bare
    // 0-1 knob here. Spelling the format out on the parameter fixes the plugin's
    // own editor, the MOD readouts and the host's generic UI at once -- and
    // without coarsening the range, which is what adding an interval would do.
    auto readout = [] (int decimals, const char* label = nullptr)
    {
        auto attributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [decimals] (float v, int) { return juce::String (v, decimals); });
        return label != nullptr ? attributes.withLabel (label) : attributes;
    };

    params.push_back (std::make_unique<FloatParam> (
        id (ParamIDs::inputGain), "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, readout (1, "dB")));
    params.push_back (std::make_unique<FloatParam> (
        id (ParamIDs::outputGain), "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, readout (1, "dB")));

    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::aidaParam1), "Model Param 1",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                    readout (2)));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::aidaParam2), "Model Param 2",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                    readout (2)));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::modelQuality), "Model Quality",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f,
                                                    readout (2)));

    juce::StringArray toneStackNames;
    for (const auto& m : nsdsp::ToneStack::getModels())
        toneStackNames.add (m.name);

    // Short enough for the 80px Pre/Post cell of both UIs; the caption above
    // it already says what the choice selects.
    const juce::StringArray positionNames { "Pre", "Post" };

    params.push_back (std::make_unique<BoolParam> (id (ParamIDs::tsOn), "Tone Stack On", true));
    params.push_back (std::make_unique<ChoiceParam> (id (ParamIDs::tsModel), "Tone Stack", toneStackNames, 0));
    params.push_back (std::make_unique<ChoiceParam> (id (ParamIDs::tsPosition), "Tone Stack Position",
                                                     positionNames, 1));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::tsBass), "Bass",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                    readout (2)));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::tsMid), "Middle",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                    readout (2)));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::tsTreble), "Treble",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                    readout (2)));
    // On by default: the passive circuit loses 4.6 to 13.1 dB at noon depending
    // on the model, which in a real amp the next gain stage makes up.
    params.push_back (std::make_unique<BoolParam> (id (ParamIDs::tsComp), "Tone Stack Level Comp", true));

    // 5-band graphic EQ (Mesa/Boogie Mark voicing). Its position is independent
    // of the tone stack's; when both land on the same side of the neural model,
    // the graphic EQ runs after the tone stack (see processBlock).
    params.push_back (std::make_unique<BoolParam> (id (ParamIDs::geqOn), "Graphic EQ On", false));
    params.push_back (std::make_unique<ChoiceParam> (id (ParamIDs::geqPosition), "Graphic EQ Position",
                                                     positionNames, 1));

    const auto maxGain = nsdsp::GraphicEQ::maxGainDb;
    for (int band = 0; band < nsdsp::GraphicEQ::numBands; ++band)
    {
        const auto hz = nsdsp::GraphicEQ::getFrequencies()[(size_t) band];
        params.push_back (std::make_unique<FloatParam> (
            id (ParamIDs::geqBand (band, hz)),
            "EQ " + juce::String (juce::roundToInt (hz)) + " Hz",
            juce::NormalisableRange<float> (-maxGain, maxGain, 0.1f), 0.0f, readout (1, "dB")));
    }

    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
    {
        const auto n = juce::String (i + 1);
        params.push_back (std::make_unique<BoolParam> (id (ParamIDs::irOn (i)), "IR " + n + " On", false));
        params.push_back (std::make_unique<FloatParam> (
            id (ParamIDs::irGain (i)), "IR " + n + " Level",
            juce::NormalisableRange<float> (-40.0f, 12.0f, 0.1f), 0.0f, readout (1, "dB")));
        params.push_back (std::make_unique<FloatParam> (id (ParamIDs::irPan (i)), "IR " + n + " Pan",
                                                        juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                                                        readout (2)));
    }

    // Spread: one musical control (the signed Offset), plus the deck sections.
    // Defaults land a tight classic ADT (+15 ms on the right, 25 % wobble) so
    // switching it on is audible immediately, and every deck switch defaults on
    // because the deck *is* the sound -- the switches only expose its sections.
    params.push_back (std::make_unique<BoolParam> (id (ParamIDs::sprOn), "Spread On", false));
    params.push_back (std::make_unique<FloatParam> (
        id (ParamIDs::sprOffset), "Spread Offset",
        juce::NormalisableRange<float> (-nsdsp::Spread::maxOffsetMs, nsdsp::Spread::maxOffsetMs, 0.01f),
        15.0f, readout (1, "ms")));
    params.push_back (std::make_unique<FloatParam> (id (ParamIDs::sprWobble), "Spread Wobble",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f,
                                                    readout (2)));
    params.push_back (std::make_unique<BoolParam> (id (ParamIDs::sprWobbleOn), "Spread Wobble On", true));
    // Log-ish map skewed so the 130 Hz default lands exactly at the knob's
    // centre (32.5 * 16^0.5 = 130: 4x per half turn).
    auto crossoverRange = juce::NormalisableRange<float> (nsdsp::Spread::minCrossoverHz,
                                                          nsdsp::Spread::maxCrossoverHz, 0.1f);
    crossoverRange.setSkewForCentre (nsdsp::Spread::defaultCrossoverHz);
    params.push_back (std::make_unique<FloatParam> (
        id (ParamIDs::sprCrossover), "Spread Crossover", crossoverRange,
        nsdsp::Spread::defaultCrossoverHz, readout (0, "Hz")));
    params.push_back (std::make_unique<BoolParam> (id (ParamIDs::sprCrossoverOn), "Spread Crossover On", true));
    params.push_back (std::make_unique<BoolParam> (id (ParamIDs::sprDiffuseOn), "Spread Diffuse On", true));

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
    graphicEq.prepare (sampleRate);
    irStack.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 1 });
    spread.prepare (sampleRate, samplesPerBlock);

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
    spread.reset();
    toneStack.reset();
    graphicEq.reset();
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

    // ----------------------------------------------------- equalisers (pre)
    // Each EQ picks its own side of the neural model. Running the tone stack
    // before the graphic EQ within both the pre and the post block is what gives
    // the required ordering: when the two land on the same side, the graphic EQ
    // follows the tone stack.
    const auto tsModelIndex = (int) pTsModel->load();
    const bool tsIsPre = ((int) pTsPosition->load()) == 0;
    const bool tsOn = pTsOn->load() > 0.5f;
    toneStack.setParams (tsModelIndex, pTsBass->load(), pTsMid->load(), pTsTreble->load(),
                         pTsComp->load() > 0.5f);

    float geqGains[nsdsp::GraphicEQ::numBands];
    for (int band = 0; band < nsdsp::GraphicEQ::numBands; ++band)
        geqGains[band] = pGeqBand[band]->load();
    graphicEq.setGains (geqGains);

    const bool geqIsPre = ((int) pGeqPosition->load()) == 0;
    const bool geqOn = pGeqOn->load() > 0.5f;

    if (tsIsPre && tsOn)
        toneStack.processBlock (mono, numSamples);
    if (geqIsPre && geqOn)
        graphicEq.processBlock (mono, numSamples);

    // ---------------------------------------------------------- amp model
    {
        const juce::SpinLock::ScopedTryLockType tl (modelLock);
        if (tl.isLocked() && model != nullptr && model->isLoaded())
        {
            model->setConditioning (pAidaParam1->load(), pAidaParam2->load());
            model->process (mono, numSamples);
        }
    }

    // ---------------------------------------------------- equalisers (post)
    if (! tsIsPre && tsOn)
        toneStack.processBlock (mono, numSamples);
    if (! geqIsPre && geqOn)
        graphicEq.processBlock (mono, numSamples);

    // ------------------------------------------------------------- IR mix
    for (int i = 0; i < nsdsp::IRStack::numSlots; ++i)
        irStack.setSlotParams (i, pIrOn[i]->load() > 0.5f, pIrGain[i]->load(), pIrPan[i]->load());

    irStack.process (mono, stereoBuffer, numSamples);

    // -------------------------------------------------------------- spread
    // Only meaningful with two output channels; on a mono bus the engine is
    // held idle so the output stays the plain chain rather than a half-heard
    // double summed back on itself. The parameter keeps its value, so a preset
    // saved with spread on comes back alive on a stereo bus.
    {
        nsdsp::Spread::Params sp;
        sp.offsetMs = pSprOffset->load();
        sp.wobbleDepth = pSprWobbleOn->load() > 0.5f ? pSprWobble->load() : 0.0f;
        sp.crossoverHz = pSprCrossover->load();
        sp.crossoverOn = pSprCrossoverOn->load() > 0.5f;
        sp.diffuseOn = pSprDiffuseOn->load() > 0.5f;

        spread.setParams (numOut >= 2 && pSprOn->load() > 0.5f, sp);
        if (spread.isRunning())
            spread.process (stereoBuffer.getWritePointer (0), stereoBuffer.getWritePointer (1),
                            numSamples);
    }

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

    std::string error;
    if (! newModel->loadFile (file.getFullPathName().toStdString(), error))
    {
        errorMessage = error;
        return false;
    }

    if (prepared)
        newModel->prepare (currentSampleRate, currentBlockSize);

    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        std::swap (model, newModel);
    }
    newModel.reset(); // destroy the previous model outside the lock

    apvts.state.setProperty (modelPathProperty, file.getFullPathName(), nullptr);
    applyModelQuality();
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
    return model != nullptr ? juce::String (model->getName()) : juce::String();
}

juce::String NamStackAudioProcessor::getModelInfo() const
{
    const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (modelLock));

    if (model == nullptr || ! model->isLoaded())
        return "No model loaded";

    juce::String info = model->getType() == nsdsp::NeuralModel::Type::nam ? "NAM" : "AIDA-X / RTNeural";
    info << " @ " << juce::String (model->getModelSampleRate() / 1000.0, 1) << " kHz";

    if (const auto numConditioning = model->getNumConditioningInputs(); numConditioning > 0)
        info << "  -  " << numConditioning << (numConditioning > 1 ? " conditioning params" : " conditioning param");

    return info;
}

int NamStackAudioProcessor::getNumModelConditioningInputs() const
{
    const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (modelLock));
    return model != nullptr ? model->getNumConditioningInputs() : 0;
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
