#include "IRStack.h"

namespace nsdsp
{

void IRStack::prepare (const juce::dsp::ProcessSpec& monoInputSpec)
{
    sampleRate = monoInputSpec.sampleRate;

    if (formatManager.getNumKnownFormats() == 0)
        formatManager.registerBasicFormats();

    juce::dsp::ProcessSpec stereoSpec { monoInputSpec.sampleRate, monoInputSpec.maximumBlockSize, 2 };

    for (auto& slot : slots)
    {
        slot.convolution.prepare (stereoSpec);
        slot.gainLeft.reset (sampleRate, 0.02);
        slot.gainRight.reset (sampleRate, 0.02);
    }

    slotBuffer.setSize (2, (int) monoInputSpec.maximumBlockSize);
}

void IRStack::reset()
{
    for (auto& slot : slots)
        slot.convolution.reset();
}

void IRStack::loadIR (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;

    auto& s = slots[(size_t) slot];

    // The channel count decides whether this slot can convolve once instead of
    // twice, and Convolution never reports it, so read the header here. A file
    // that will not open counts as stereo: that is the path which is correct
    // whatever the IR turns out to be.
    int channels = 2;
    if (const std::unique_ptr<juce::AudioFormatReader> reader { formatManager.createReaderFor (file) })
        channels = (int) reader->numChannels;

    s.convolution.loadImpulseResponse (file,
                                       juce::dsp::Convolution::Stereo::yes,
                                       juce::dsp::Convolution::Trim::yes,
                                       0,
                                       juce::dsp::Convolution::Normalise::yes);
    {
        const juce::ScopedLock sl (nameLock);
        s.name = file.getFileNameWithoutExtension();
    }

    // The load above is asynchronous, so the outgoing IR is still the live one
    // for now: the audio thread keeps acting on the previous decision until
    // the settling window runs out (see settleSeconds).
    s.irIsMonoPending.store (channels == 1);
    s.monoSettle.store ((int) (settleSeconds * sampleRate));
    s.loaded.store (true);
}

void IRStack::clearIR (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;

    auto& s = slots[(size_t) slot];
    s.loaded.store (false);
    s.irIsMonoPending.store (false);
    s.monoSettle.store ((int) (settleSeconds * sampleRate));
    {
        const juce::ScopedLock sl (nameLock);
        s.name.clear();
    }
    s.convolution.reset();
}

juce::String IRStack::getIRName (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return {};

    const juce::ScopedLock sl (nameLock);
    return slots[(size_t) slot].name;
}

void IRStack::setSlotParams (int slot, bool enabled, float gainDb, float pan)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;

    auto& s = slots[(size_t) slot];
    s.enabled.store (enabled);
    s.targetGainDb.store (gainDb);
    s.targetPan.store (pan);
}

void IRStack::process (const float* monoIn, juce::AudioBuffer<float>& stereoOut, int numSamples)
{
    stereoOut.clear (0, 0, numSamples);
    stereoOut.clear (1, 0, numSamples);

    bool anyActive = false;

    for (auto& slot : slots)
    {
        // The settling window runs whether or not the slot is being heard, so
        // a slot loaded while bypassed is ready the moment it is switched on.
        if (const auto settle = slot.monoSettle.load(); settle > 0)
        {
            const auto remaining = juce::jmax (0, settle - numSamples);
            slot.monoSettle.store (remaining);

            // The requested IR is now the live one; act on what it is.
            if (remaining == 0)
                slot.irIsMonoLive = slot.irIsMonoPending.load();
        }

        if (! (slot.enabled.load() && slot.loaded.load()))
            continue;

        anyActive = true;

        // A mono IR gives both engines the same IR, and both are fed the same
        // mono signal, so their outputs are identical: convolve one channel
        // and duplicate. JUCE only skips the second engine when the block it
        // is handed is itself mono (MultichannelEngine::processSamples takes
        // jmin of the head count and the block's channels), which is why the
        // block is built narrow rather than the buffer left wide.
        const auto monoIr = slot.irIsMonoLive;
        const auto convChannels = (size_t) (monoIr ? 1 : 2);

        slotBuffer.copyFrom (0, 0, monoIn, numSamples);
        if (! monoIr)
            slotBuffer.copyFrom (1, 0, monoIn, numSamples);

        juce::dsp::AudioBlock<float> block (slotBuffer.getArrayOfWritePointers(),
                                            convChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        slot.convolution.process (context);

        // Constant-power pan combined with the slot level.
        const auto gain = juce::Decibels::decibelsToGain (slot.targetGainDb.load());
        const auto pan = juce::jlimit (-1.0f, 1.0f, slot.targetPan.load());
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        slot.gainLeft.setTargetValue (gain * std::cos (angle) * juce::MathConstants<float>::sqrt2);
        slot.gainRight.setTargetValue (gain * std::sin (angle) * juce::MathConstants<float>::sqrt2);

        auto* left = stereoOut.getWritePointer (0);
        auto* right = stereoOut.getWritePointer (1);
        const auto* srcL = slotBuffer.getReadPointer (0);
        const auto* srcR = slotBuffer.getReadPointer (monoIr ? 0 : 1);

        for (int i = 0; i < numSamples; ++i)
        {
            left[i] += srcL[i] * slot.gainLeft.getNextValue();
            right[i] += srcR[i] * slot.gainRight.getNextValue();
        }
    }

    if (! anyActive)
    {
        stereoOut.copyFrom (0, 0, monoIn, numSamples);
        stereoOut.copyFrom (1, 0, monoIn, numSamples);
    }
}

} // namespace nsdsp
