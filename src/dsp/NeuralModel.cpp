#include "NeuralModel.h"

#include <NAM/dsp.h>
#include <NAM/get_dsp.h>
#include <NAM/slimmable.h>
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

// The `using LanczosResampler = LanczosResampler<...>` alias inside
// ResamplingContainer.h changes the meaning of the name within class scope.
// GCC 11+ makes this a hard error governed by -fpermissive; the pragma below
// only silences it on GCC 14+, where -Wchanges-meaning exists. For GCC 11-13
// the CMakeLists compiles this TU with -fpermissive (downgrades it to a
// warning). See CMakeLists.txt (set_source_files_properties on NeuralModel.cpp).
#if defined(__GNUC__) && ! defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wpragmas" // older GCC: unknown warning below
  #pragma GCC diagnostic ignored "-Wchanges-meaning"
#endif

#include <dsp/ResamplingContainer/ResamplingContainer.h>

#if defined(__GNUC__) && ! defined(__clang__)
  #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace nsdsp
{

// Defined in NamArchitectures.cpp; calling it from here (a translation unit
// that is always linked) keeps the NAM architecture registrars alive.
const void* getNamArchitectureAnchor (int index);

namespace
{
std::string toLower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}
} // namespace

NeuralModel::NeuralModel()
{
    (void) getNamArchitectureAnchor (0);
}

NeuralModel::~NeuralModel() = default;

bool NeuralModel::loadFile (const std::string& path, std::string& errorMessage)
{
    const std::filesystem::path fsPath (path);

    std::error_code ec;
    if (! std::filesystem::is_regular_file (fsPath, ec))
    {
        errorMessage = "File not found: " + path;
        return false;
    }

    const auto extension = toLower (fsPath.extension().string());

    if (extension == ".nam")
    {
        try
        {
            namModel = nam::get_dsp (fsPath);
        }
        catch (const std::exception& e)
        {
            errorMessage = std::string ("Failed to load NAM model: ") + e.what();
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
            std::ifstream jsonStream (fsPath, std::ifstream::binary);
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
            errorMessage = std::string ("Failed to load RTNeural/AIDA-X model: ") + e.what();
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
            errorMessage = "Unsupported model input size: " + std::to_string (rtInputSize);
            rtModel.reset();
            return false;
        }

        type = Type::rtNeural;
    }

    name = fsPath.stem().string();
    return true;
}

void NeuralModel::prepare (double sampleRate, int maxBlockSize)
{
    hostSampleRate = sampleRate;
    needsResampling = std::abs (hostSampleRate - modelSampleRate) > 1.0;

    const auto modelBlockSize = needsResampling
        ? std::max (16, (int) std::ceil ((double) maxBlockSize * modelSampleRate / hostSampleRate) + 64)
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

bool NeuralModel::isSlimmable() const noexcept
{
    return dynamic_cast<const nam::SlimmableModel*> (namModel.get()) != nullptr;
}

void NeuralModel::setSlimmableSize (double size01)
{
    // clamp<double> explicitly: -fsingle-precision-constant (used by MOD
    // device builds) turns plain literals into floats.
    if (auto* slimmable = dynamic_cast<nam::SlimmableModel*> (namModel.get()))
        slimmable->SetSlimmableSize (std::clamp<double> (size01, 0.0, 1.0));
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
            // Conditioned models: audio on the first input, then the
            // Param 1 / Param 2 knobs. Any further inputs stay at zero.
            float inVec[8] = {};
            inVec[1] = conditioning[0].load (std::memory_order_relaxed);
            if (rtInputSize >= 3)
                inVec[2] = conditioning[1].load (std::memory_order_relaxed);

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
