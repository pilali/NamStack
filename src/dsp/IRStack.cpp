#include "IRStack.h"

namespace nsdsp
{

void IRStack::prepare (const juce::dsp::ProcessSpec& monoInputSpec)
{
    sampleRate = monoInputSpec.sampleRate;

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
    s.convolution.loadImpulseResponse (file,
                                       juce::dsp::Convolution::Stereo::yes,
                                       juce::dsp::Convolution::Trim::yes,
                                       0,
                                       juce::dsp::Convolution::Normalise::yes);
    {
        const juce::ScopedLock sl (nameLock);
        s.name = file.getFileNameWithoutExtension();
    }
    s.loaded.store (true);
}

void IRStack::clearIR (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;

    auto& s = slots[(size_t) slot];
    s.loaded.store (false);
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
        if (! (slot.enabled.load() && slot.loaded.load()))
            continue;

        anyActive = true;

        // Duplicate the mono signal on both convolution channels.
        slotBuffer.copyFrom (0, 0, monoIn, numSamples);
        slotBuffer.copyFrom (1, 0, monoIn, numSamples);

        juce::dsp::AudioBlock<float> block (slotBuffer.getArrayOfWritePointers(), 2, (size_t) numSamples);
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
        const auto* srcR = slotBuffer.getReadPointer (1);

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
