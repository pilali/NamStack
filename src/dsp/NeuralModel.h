#pragma once

#include <atomic>
#include <memory>
#include <string>

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
//
// This class is deliberately JUCE-free so that it can be shared between the
// JUCE plugin and the plain LV2 (MOD) build.
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

    NeuralModel (const NeuralModel&) = delete;
    NeuralModel& operator= (const NeuralModel&) = delete;

    // Call on a non-realtime thread. Returns false and fills errorMessage on
    // failure. prepare() must be called before the model is processed.
    bool loadFile (const std::string& path, std::string& errorMessage);

    void prepare (double sampleRate, int maxBlockSize);

    // Mono in-place processing. Only call after prepare().
    void process (float* data, int numSamples);

    Type getType() const noexcept { return type; }
    bool isLoaded() const noexcept { return type != Type::none; }
    const std::string& getName() const noexcept { return name; }
    double getModelSampleRate() const noexcept { return modelSampleRate; }
    int getLatencySamples() const noexcept;

    // Number of extra conditioning inputs of an AIDA-X/RTNeural model
    // (0 for plain models and for NAM).
    int getNumConditioningInputs() const noexcept
    {
        return type == Type::rtNeural ? rtInputSize - 1 : 0;
    }

    // Values fed to the conditioning inputs (usually gain/master style
    // knobs baked into the training, in [0, 1]). Realtime-safe.
    void setConditioning (float param1, float param2) noexcept
    {
        conditioning[0].store (param1);
        conditioning[1].store (param2);
    }

    // Slimmable NAM models (e.g. A2 SlimmableContainer files) can trade
    // quality for CPU. setSlimmableSize() is thread-safe but NOT
    // realtime-safe: call it from a worker/background thread only.
    bool isSlimmable() const noexcept;
    void setSlimmableSize (double size01);

private:
    Type type = Type::none;
    std::string name;
    double modelSampleRate = 48000.0;
    double hostSampleRate = 48000.0;

    std::unique_ptr<nam::DSP> namModel;
    std::unique_ptr<RTNeural::Model<float>> rtModel;
    int rtInputSize = 1;
    std::atomic<float> conditioning[2] { 0.5f, 0.5f };

    std::unique_ptr<dsp::ResamplingContainer<float, 1, 12>> resampler;
    bool needsResampling = false;

    void processAtModelRate (float** in, float** out, int numSamples);
};

} // namespace nsdsp
