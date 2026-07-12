#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace nam
{
class DSP;
}

namespace RTNeural
{
template <typename T>
class Model;
}

namespace dsp
{
template <typename T, int NCHANS, size_t A>
class ResamplingContainer;
}

namespace nsdsp
{

// Loads and runs either a NAM (.nam) model through NeuralAmpModelerCore or an
// AIDA-X / RTNeural (.aidax / .json) model through RTNeural. When the model's
// native sample rate differs from the host's, processing is wrapped in a
// Lanczos resampling container.
class NeuralModel
{
public:
    enum class Type
    {
        none,
        nam,
        rtNeural
    };

    NeuralModel();
    ~NeuralModel();

    // Call on a non-realtime thread. Returns false and fills errorMessage on
    // failure. prepare() must be called before the model is processed.
    bool loadFile (const juce::File& file, juce::String& errorMessage);

    void prepare (double sampleRate, int maxBlockSize);

    // Mono in-place processing. Only call after prepare().
    void process (float* data, int numSamples);

    Type getType() const noexcept { return type; }
    bool isLoaded() const noexcept { return type != Type::none; }
    juce::String getName() const { return name; }
    double getModelSampleRate() const noexcept { return modelSampleRate; }
    int getLatencySamples() const noexcept;

private:
    Type type = Type::none;
    juce::String name;
    double modelSampleRate = 48000.0;
    double hostSampleRate = 48000.0;

    std::unique_ptr<nam::DSP> namModel;
    std::unique_ptr<RTNeural::Model<float>> rtModel;
    int rtInputSize = 1;

    std::unique_ptr<dsp::ResamplingContainer<float, 1, 12>> resampler;
    bool needsResampling = false;

    void processAtModelRate (float** in, float** out, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuralModel)
};

} // namespace nsdsp
