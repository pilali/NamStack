#include "NeuralModel.h"

#include <NAM/dsp.h>
#include <NAM/get_dsp.h>
#include <RTNeural/RTNeural.h>

// AudioDSPTools' ResamplingContainer comes from iPlug2 and expects these two
// symbols from that framework.
namespace iplug
{
inline constexpr double PI = 3.14159265358979323846;
}
#ifndef DEFAULT_BLOCK_SIZE
#define DEFAULT_BLOCK_SIZE 512
#endif

#include <dsp/ResamplingContainer/ResamplingContainer.h>

#include <fstream>

namespace nsdsp
{

NeuralModel::NeuralModel() = default;
NeuralModel::~NeuralModel() = default;

bool NeuralModel::loadFile (const juce::File& file, juce::String& errorMessage)
{
    if (! file.existsAsFile())
    {
        errorMessage = "File not found: " + file.getFullPathName();
        return false;
    }

    const auto extension = file.getFileExtension().toLowerCase();

    if (extension == ".nam")
    {
        try
        {
            namModel = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
        }
        catch (const std::exception& e)
        {
            errorMessage = "Failed to load NAM model: " + juce::String (e.what());
            return false;
        }

        if (namModel == nullptr)
        {
            errorMessage = "Failed to load NAM model.";
            return false;
        }

        modelSampleRate = namModel->GetExpectedSampleRate();
        if (modelSampleRate <= 0.0)
            modelSampleRate = 48000.0;

        type = Type::nam;
    }
    else // .aidax / .json -> RTNeural
    {
        try
        {
            std::ifstream jsonStream (file.getFullPathName().toStdString(), std::ifstream::binary);
            nlohmann::json modelJson;
            jsonStream >> modelJson;

            rtModel = RTNeural::json_parser::parseJson<float> (modelJson);

            // AIDA-X files sometimes carry their training sample rate.
            modelSampleRate = 48000.0;
            if (modelJson.contains ("samplerate") && modelJson["samplerate"].is_number())
                modelSampleRate = modelJson["samplerate"].get<double>();
            else if (modelJson.contains ("metadata") && modelJson["metadata"].is_object()
                     && modelJson["metadata"].contains ("samplerate")
                     && modelJson["metadata"]["samplerate"].is_number())
                modelSampleRate = modelJson["metadata"]["samplerate"].get<double>();
        }
        catch (const std::exception& e)
        {
            errorMessage = "Failed to load RTNeural/AIDA-X model: " + juce::String (e.what());
            return false;
        }

        if (rtModel == nullptr)
        {
            errorMessage = "Failed to parse RTNeural/AIDA-X model.";
            return false;
        }

        rtInputSize = rtModel->getInSize();
        if (rtInputSize < 1 || rtInputSize > 8)
        {
            errorMessage = "Unsupported model input size: " + juce::String (rtInputSize);
            rtModel.reset();
            return false;
        }

        type = Type::rtNeural;
    }

    name = file.getFileNameWithoutExtension();
    return true;
}

void NeuralModel::prepare (double sampleRate, int maxBlockSize)
{
    hostSampleRate = sampleRate;
    needsResampling = std::abs (hostSampleRate - modelSampleRate) > 1.0;

    const auto modelBlockSize = needsResampling
        ? juce::jmax (16, (int) std::ceil ((double) maxBlockSize * modelSampleRate / hostSampleRate) + 64)
        : maxBlockSize;

    if (type == Type::nam && namModel != nullptr)
        namModel->Reset (modelSampleRate, modelBlockSize);
    else if (type == Type::rtNeural && rtModel != nullptr)
        rtModel->reset();

    resampler.reset();
    if (needsResampling)
    {
        resampler = std::make_unique<dsp::ResamplingContainer<float, 1, 12>> (modelSampleRate);
        resampler->Reset (hostSampleRate, maxBlockSize);
    }
}

int NeuralModel::getLatencySamples() const noexcept
{
    return resampler != nullptr ? resampler->GetLatency() : 0;
}

void NeuralModel::processAtModelRate (float** in, float** out, int numSamples)
{
    if (type == Type::nam && namModel != nullptr)
    {
        namModel->process (in, out, numSamples);
    }
    else if (type == Type::rtNeural && rtModel != nullptr)
    {
        const float* input = in[0];
        float* output = out[0];

        if (rtInputSize == 1)
        {
            for (int i = 0; i < numSamples; ++i)
                output[i] = rtModel->forward (&input[i]);
        }
        else
        {
            // Conditioned models: audio on the first input, remaining
            // conditioning inputs held at zero.
            float inVec[8] = {};
            for (int i = 0; i < numSamples; ++i)
            {
                inVec[0] = input[i];
                output[i] = rtModel->forward (inVec);
            }
        }
    }
    else if (in[0] != out[0])
    {
        std::copy_n (in[0], numSamples, out[0]);
    }
}

void NeuralModel::process (float* data, int numSamples)
{
    float* channel[1] = { data };

    if (resampler != nullptr)
    {
        resampler->ProcessBlock (channel, channel, numSamples,
                                 [this] (float** in, float** out, int n) { processAtModelRate (in, out, n); });
    }
    else
    {
        processAtModelRate (channel, channel, numSamples);
    }
}

} // namespace nsdsp
