// NamStack — plain (JUCE-free) LV2 implementation for MOD devices.
//
// The neural model and the four impulse responses are exposed as
// patch:writable atom:Path parameters, loaded on the host-provided worker
// thread and swapped into the audio thread without blocking, following the
// pattern of neural-amp-modeler-lv2. All knobs are regular control ports so
// they can be addressed to hardware controls on MOD devices.

#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <lv2/log/log.h>
#include <lv2/log/logger.h>
#include <lv2/options/options.h>
#include <lv2/patch/patch.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include "core/Convolver.h"
#include "core/DenormalGuard.h"
#include "core/IRLoader.h"
#include "core/IRMixer.h"
#include "dsp/Spread.h"
#include "dsp/NeuralModel.h"
#include "dsp/GraphicEQ.h"
#include "dsp/ToneStack.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#define NAMSTACK_URI "urn:pilali:NamStackMOD"

#ifndef NAMSTACK_MAX_IR_SAMPLES
#define NAMSTACK_MAX_IR_SAMPLES 8192
#endif

namespace
{

constexpr unsigned int kMaxFileName = 1024;
constexpr int kNumFileSlots = 5; // 0 = model, 1..4 = IRs
constexpr int kMaxChunk = 2048;  // internal processing slice

const char* const kFileSlotUris[kNumFileSlots] = {
    NAMSTACK_URI "#model",
    NAMSTACK_URI "#ir1",
    NAMSTACK_URI "#ir2",
    NAMSTACK_URI "#ir3",
    NAMSTACK_URI "#ir4",
};

enum PortIndex
{
    kPortControl = 0,
    kPortNotify,
    kPortAudioIn,
    kPortAudioOutL,
    kPortAudioOutR,
    kPortLatency,
    kPortInGain,
    kPortAidaParam1,
    kPortAidaParam2,
    kPortTsModel,
    kPortTsPosition,
    kPortTsBass,
    kPortTsMid,
    kPortTsTreble,
    kPortIr1On,
    kPortIr1Gain,
    kPortIr1Pan,
    kPortIr2On,
    kPortIr2Gain,
    kPortIr2Pan,
    kPortIr3On,
    kPortIr3Gain,
    kPortIr3Pan,
    kPortIr4On,
    kPortIr4Gain,
    kPortIr4Pan,
    // The doubler these six indices used to carry was replaced by Spread (an
    // ADT-style image, see dsp/Spread.h). Its controls have no counterpart --
    // there is no Mix, Detune or Humanize in the new engine -- so the indices
    // are reused rather than kept as dead ports; the .ttl is the contract and
    // it changed with them.
    kPortSprOn,
    kPortSprOffset,
    kPortSprWobble,
    kPortSprWobbleOn,
    kPortSprCrossover,
    kPortSprCrossoverOn,
    kPortOutGain,
    kPortQuality,
    // Appended after kPortQuality on purpose: LV2 identifies control ports by
    // index, so inserting these in their "logical" place would silently remap
    // the controls of every pedalboard already saved with this plugin.
    kPortTsOn,
    kPortGeqOn,
    kPortGeqPosition,
    kPortGeqBand1, // 80 Hz; the five bands are contiguous, see kPortGeqBand1 + i
    kPortGeqBand2, // 240 Hz
    kPortGeqBand3, // 750 Hz
    kPortGeqBand4, // 2200 Hz
    kPortGeqBand5, // 6600 Hz
    kPortSprDiffuseOn, // seventh spread control; six fit in the old doubler block
    kPortTsComp,
    kPortCount
};

enum WorkType : uint32_t
{
    kWorkLoad,
    kWorkApply,
    kWorkFree,
    kWorkSetQuality
};

struct LoadMsg
{
    WorkType type;
    int32_t slot; // 0 = model, 1..4 = IR slot
    char path[kMaxFileName];
};

struct ApplyMsg
{
    WorkType type;
    int32_t slot;
    void* object; // NeuralModel* or Convolver*, may be null (= clear)
    char path[kMaxFileName];
};

struct FreeMsg
{
    WorkType type;
    int32_t slot;
    void* object;
};

// SetSlimmableSize() is thread-safe but not realtime-safe, so quality
// changes run on the worker. The worker executes jobs in order, so a
// quality job scheduled while `model` is current always runs before the
// kWorkFree job that would delete that model after a later swap.
struct QualityMsg
{
    WorkType type;
    nsdsp::NeuralModel* model;
    float value;
};

struct NamStackMod
{
    // ports
    const LV2_Atom_Sequence* control = nullptr;
    LV2_Atom_Sequence* notify = nullptr;
    const float* audioIn = nullptr;
    float* audioOutL = nullptr;
    float* audioOutR = nullptr;
    float* latencyPort = nullptr;
    const float* params[kPortCount] = {};

    // features
    LV2_URID_Map* map = nullptr;
    LV2_Worker_Schedule* schedule = nullptr;
    LV2_Log_Logger logger = {};

    struct URIs
    {
        LV2_URID atom_Object;
        LV2_URID atom_Int;
        LV2_URID atom_Path;
        LV2_URID atom_URID;
        LV2_URID bufSize_maxBlockLength;
        LV2_URID bufSize_nominalBlockLength;
        LV2_URID patch_Set;
        LV2_URID patch_Get;
        LV2_URID patch_property;
        LV2_URID patch_value;
        LV2_URID units_frame;
        LV2_URID fileSlot[kNumFileSlots];
    } uris = {};

    LV2_Atom_Forge forge = {};
    LV2_Atom_Forge_Frame notifyFrame;

    // dsp
    double sampleRate = 48000.0;
    int maxBlockSize = 8192;
    int partitionSize = 128;

    nsdsp::NeuralModel* model = nullptr; // owned, swapped via worker
    std::string filePaths[kNumFileSlots];

    nsdsp::ToneStack toneStack;
    nsdsp::GraphicEQ graphicEq;
    nsdsp::IRMixer irMixer;
    nsdsp::Spread spread;

    std::vector<float> mono, busL, busR;

    float inGainSmoothed = 1.0f;
    float outGainSmoothed = 1.0f;
    float gainCoeff = 0.01f;

    float appliedQuality = 1.0f;
    bool qualityDirty = false; // set when a new model is swapped in

    bool activated = false;
};

int nextPow2 (int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

void updateBlockSizes (NamStackMod* self, int maxBlock)
{
    self->maxBlockSize = std::max (16, maxBlock);
    self->partitionSize = std::clamp (nextPow2 (self->maxBlockSize), 64, 1024);
}

void prepareDsp (NamStackMod* self)
{
    self->toneStack.prepare (self->sampleRate);
    self->graphicEq.prepare (self->sampleRate);
    self->irMixer.prepare (self->sampleRate, self->partitionSize, std::max (self->maxBlockSize, kMaxChunk));
    self->spread.prepare (self->sampleRate, self->maxBlockSize);

    self->mono.assign ((size_t) kMaxChunk, 0.0f);
    self->busL.assign ((size_t) kMaxChunk, 0.0f);
    self->busR.assign ((size_t) kMaxChunk, 0.0f);

    self->gainCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * self->sampleRate)));

    if (self->model != nullptr)
        self->model->prepare (self->sampleRate, kMaxChunk);
}

// ------------------------------------------------------------------ notify

void writePathNotification (NamStackMod* self, int slot)
{
    LV2_Atom_Forge_Frame frame;

    lv2_atom_forge_frame_time (&self->forge, 0);
    lv2_atom_forge_object (&self->forge, &frame, 0, self->uris.patch_Set);

    lv2_atom_forge_key (&self->forge, self->uris.patch_property);
    lv2_atom_forge_urid (&self->forge, self->uris.fileSlot[slot]);
    lv2_atom_forge_key (&self->forge, self->uris.patch_value);
    lv2_atom_forge_path (&self->forge, self->filePaths[slot].c_str(),
                         (uint32_t) self->filePaths[slot].length() + 1);

    lv2_atom_forge_pop (&self->forge, &frame);
}

// ------------------------------------------------------------------- worker

LV2_Worker_Status work (LV2_Handle instance, LV2_Worker_Respond_Function respond,
                        LV2_Worker_Respond_Handle handle, uint32_t size, const void* data)
{
    auto* self = static_cast<NamStackMod*> (instance);
    (void) size;

    switch (*static_cast<const WorkType*> (data))
    {
        case kWorkLoad:
        {
            const auto* msg = static_cast<const LoadMsg*> (data);

            ApplyMsg response = { kWorkApply, msg->slot, nullptr, {} };

            const auto pathLen = strnlen (msg->path, kMaxFileName);

            if (pathLen > 0 && pathLen < kMaxFileName)
            {
                std::string error;

                if (msg->slot == 0)
                {
                    auto model = std::make_unique<nsdsp::NeuralModel>();
                    if (model->loadFile (msg->path, error))
                    {
                        model->prepare (self->sampleRate, kMaxChunk);
                        response.object = model.release();
                        memcpy (response.path, msg->path, pathLen);
                    }
                }
                else
                {
                    nsdsp::IRData ir;
                    if (nsdsp::loadIRFile (msg->path, self->sampleRate, NAMSTACK_MAX_IR_SAMPLES, ir, error))
                    {
                        const float* channels[2] = { ir.channels[0].data(), ir.channels[1].data() };
                        auto convolver = std::make_unique<nsdsp::Convolver>();
                        if (convolver->init (channels, ir.numChannels, ir.length, self->partitionSize))
                        {
                            response.object = convolver.release();
                            memcpy (response.path, msg->path, pathLen);
                        }
                        else
                        {
                            error = "Could not initialise convolver";
                        }
                    }
                }

                if (response.object == nullptr)
                    lv2_log_error (&self->logger, "NamStack: failed to load '%s': %s\n",
                                   msg->path, error.c_str());
            }

            respond (handle, sizeof (response), &response);
            return LV2_WORKER_SUCCESS;
        }

        case kWorkFree:
        {
            const auto* msg = static_cast<const FreeMsg*> (data);
            if (msg->slot == 0)
                delete static_cast<nsdsp::NeuralModel*> (msg->object);
            else
                delete static_cast<nsdsp::Convolver*> (msg->object);
            return LV2_WORKER_SUCCESS;
        }

        case kWorkSetQuality:
        {
            const auto* msg = static_cast<const QualityMsg*> (data);
            if (msg->model != nullptr)
                msg->model->setSlimmableSize ((double) msg->value);
            return LV2_WORKER_SUCCESS;
        }

        case kWorkApply:
            break; // must not arrive here
    }

    return LV2_WORKER_ERR_UNKNOWN;
}

// runs on the audio thread right after run(); must not block or allocate
LV2_Worker_Status workResponse (LV2_Handle instance, uint32_t size, const void* data)
{
    auto* self = static_cast<NamStackMod*> (instance);
    (void) size;

    if (*static_cast<const WorkType*> (data) != kWorkApply)
        return LV2_WORKER_ERR_UNKNOWN;

    const auto* msg = static_cast<const ApplyMsg*> (data);

    FreeMsg freeMsg = { kWorkFree, msg->slot, nullptr };

    if (msg->slot == 0)
    {
        freeMsg.object = self->model;
        self->model = static_cast<nsdsp::NeuralModel*> (msg->object);
        self->qualityDirty = true; // re-apply the quality knob to the new model
    }
    else
    {
        freeMsg.object = self->irMixer.exchangeConvolver (msg->slot - 1,
                                                          static_cast<nsdsp::Convolver*> (msg->object));
    }

    self->filePaths[msg->slot] = msg->path; // capacity preallocated, no allocation

    if (freeMsg.object != nullptr)
        self->schedule->schedule_work (self->schedule->handle, sizeof (freeMsg), &freeMsg);

    writePathNotification (self, msg->slot);

    return LV2_WORKER_SUCCESS;
}

// -------------------------------------------------------------------- state

LV2_State_Status save (LV2_Handle instance, LV2_State_Store_Function store, LV2_State_Handle handle,
                       uint32_t flags, const LV2_Feature* const* features)
{
    auto* self = static_cast<NamStackMod*> (instance);
    (void) flags;

    auto* mapPath = static_cast<LV2_State_Map_Path*> (
        lv2_features_data (features, LV2_STATE__mapPath));
    auto* freePath = static_cast<LV2_State_Free_Path*> (
        lv2_features_data (features, LV2_STATE__freePath));

    if (mapPath == nullptr)
        return LV2_STATE_ERR_NO_FEATURE;

    for (int slot = 0; slot < kNumFileSlots; ++slot)
    {
        if (self->filePaths[slot].empty())
            continue;

        char* apath = mapPath->abstract_path (mapPath->handle, self->filePaths[slot].c_str());
        if (apath == nullptr)
            continue;

        store (handle, self->uris.fileSlot[slot], apath, strlen (apath) + 1,
               self->uris.atom_Path, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

        if (freePath != nullptr)
            freePath->free_path (freePath->handle, apath);
        else
            free (apath);
    }

    return LV2_STATE_SUCCESS;
}

LV2_State_Status restore (LV2_Handle instance, LV2_State_Retrieve_Function retrieve, LV2_State_Handle handle,
                          uint32_t flags, const LV2_Feature* const* features)
{
    auto* self = static_cast<NamStackMod*> (instance);
    (void) flags;

    auto* mapPath = static_cast<LV2_State_Map_Path*> (
        lv2_features_data (features, LV2_STATE__mapPath));
    auto* freePath = static_cast<LV2_State_Free_Path*> (
        lv2_features_data (features, LV2_STATE__freePath));

    for (int slot = 0; slot < kNumFileSlots; ++slot)
    {
        size_t size = 0;
        uint32_t type = 0, valflags = 0;
        const void* value = retrieve (handle, self->uris.fileSlot[slot], &size, &type, &valflags);

        LoadMsg msg = { kWorkLoad, slot, {} };

        if (value != nullptr && type == self->uris.atom_Path)
        {
            const char* abstract = static_cast<const char*> (value);
            char* path = (mapPath != nullptr)
                ? mapPath->absolute_path (mapPath->handle, abstract)
                : nullptr;

            const char* effective = (path != nullptr) ? path : abstract;
            const auto len = strlen (effective);
            if (len < kMaxFileName)
                memcpy (msg.path, effective, len);

            if (path != nullptr)
            {
                if (freePath != nullptr)
                    freePath->free_path (freePath->handle, path);
                else
                    free (path);
            }
        }
        else if (self->filePaths[slot].empty())
        {
            continue; // nothing stored and nothing loaded: skip
        }

        self->schedule->schedule_work (self->schedule->handle, sizeof (msg), &msg);
    }

    return LV2_STATE_SUCCESS;
}

// ------------------------------------------------------------------ options

uint32_t optionsSet (NamStackMod* self, const LV2_Options_Option* options)
{
    for (int i = 0; options[i].key != 0 && options[i].type != 0; ++i)
    {
        if ((options[i].key == self->uris.bufSize_maxBlockLength
             || options[i].key == self->uris.bufSize_nominalBlockLength)
            && options[i].type == self->uris.atom_Int)
        {
            if (options[i].key == self->uris.bufSize_maxBlockLength)
                updateBlockSizes (self, *static_cast<const int32_t*> (options[i].value));
        }
    }
    return LV2_OPTIONS_SUCCESS;
}

// -------------------------------------------------------------- lv2 methods

LV2_Handle instantiate (const LV2_Descriptor* /*descriptor*/, double rate,
                        const char* /*bundlePath*/, const LV2_Feature* const* features)
{
    auto* self = new NamStackMod();
    self->sampleRate = rate;

    const LV2_Options_Option* options = nullptr;

    for (size_t i = 0; features[i] != nullptr; ++i)
    {
        const std::string uri (features[i]->URI);
        if (uri == LV2_URID__map)
            self->map = static_cast<LV2_URID_Map*> (features[i]->data);
        else if (uri == LV2_WORKER__schedule)
            self->schedule = static_cast<LV2_Worker_Schedule*> (features[i]->data);
        else if (uri == LV2_LOG__log)
            self->logger.log = static_cast<LV2_Log_Log*> (features[i]->data);
        else if (uri == LV2_OPTIONS__options)
            options = static_cast<const LV2_Options_Option*> (features[i]->data);
    }

    if (self->map == nullptr || self->schedule == nullptr)
    {
        delete self;
        return nullptr;
    }

    lv2_log_logger_set_map (&self->logger, self->map);
    lv2_atom_forge_init (&self->forge, self->map);

    auto mapUri = [self] (const char* uri) { return self->map->map (self->map->handle, uri); };

    self->uris.atom_Object = mapUri (LV2_ATOM__Object);
    self->uris.atom_Int = mapUri (LV2_ATOM__Int);
    self->uris.atom_Path = mapUri (LV2_ATOM__Path);
    self->uris.atom_URID = mapUri (LV2_ATOM__URID);
    self->uris.bufSize_maxBlockLength = mapUri (LV2_BUF_SIZE__maxBlockLength);
    self->uris.bufSize_nominalBlockLength = mapUri (LV2_BUF_SIZE__nominalBlockLength);
    self->uris.patch_Set = mapUri (LV2_PATCH__Set);
    self->uris.patch_Get = mapUri (LV2_PATCH__Get);
    self->uris.patch_property = mapUri (LV2_PATCH__property);
    self->uris.patch_value = mapUri (LV2_PATCH__value);
    self->uris.units_frame = mapUri (LV2_ATOM__frameTime);

    for (int i = 0; i < kNumFileSlots; ++i)
    {
        self->uris.fileSlot[i] = mapUri (kFileSlotUris[i]);
        self->filePaths[i].reserve (kMaxFileName + 1); // no allocation on the audio thread
    }

    updateBlockSizes (self, 512);
    if (options != nullptr)
        optionsSet (self, options);

    prepareDsp (self);

    return self;
}

void connectPort (LV2_Handle instance, uint32_t port, void* data)
{
    auto* self = static_cast<NamStackMod*> (instance);

    switch (port)
    {
        case kPortControl:  self->control = static_cast<const LV2_Atom_Sequence*> (data); break;
        case kPortNotify:   self->notify = static_cast<LV2_Atom_Sequence*> (data); break;
        case kPortAudioIn:  self->audioIn = static_cast<const float*> (data); break;
        case kPortAudioOutL: self->audioOutL = static_cast<float*> (data); break;
        case kPortAudioOutR: self->audioOutR = static_cast<float*> (data); break;
        case kPortLatency:  self->latencyPort = static_cast<float*> (data); break;
        default:
            if (port < kPortCount)
                self->params[port] = static_cast<const float*> (data);
            break;
    }
}

void activate (LV2_Handle instance)
{
    auto* self = static_cast<NamStackMod*> (instance);
    self->toneStack.reset();
    self->graphicEq.reset();
    self->irMixer.reset();
    self->spread.reset();
    self->activated = true;
}

float param (const NamStackMod* self, PortIndex port, float fallback = 0.0f)
{
    return self->params[port] != nullptr ? *self->params[port] : fallback;
}

void run (LV2_Handle instance, uint32_t nSamples)
{
    auto* self = static_cast<NamStackMod*> (instance);

    // Flush-to-zero for this callback, restored on the way out. Without it the
    // filter states walk into the subnormal range as soon as the player stops
    // and the whole chain falls off the FPU's fast path -- 25x slower on the
    // measured x86-64 case, and the JUCE build has had juce::ScopedNoDenormals
    // for this all along. See core/DenormalGuard.h.
    const nsdsp::DenormalGuard denormalGuard;

    // set up the notify port forge
    const auto notifyCapacity = self->notify->atom.size;
    lv2_atom_forge_set_buffer (&self->forge, reinterpret_cast<uint8_t*> (self->notify), notifyCapacity);
    lv2_atom_forge_sequence_head (&self->forge, &self->notifyFrame, 0);

    // -------------------------------------------------- incoming patch msgs
    LV2_ATOM_SEQUENCE_FOREACH (self->control, event)
    {
        if (event->body.type != self->uris.atom_Object)
            continue;

        const auto* obj = reinterpret_cast<const LV2_Atom_Object*> (&event->body);

        if (obj->body.otype == self->uris.patch_Get)
        {
            const LV2_Atom* property = nullptr;
            lv2_atom_object_get (obj, self->uris.patch_property, &property, 0);

            for (int slot = 0; slot < kNumFileSlots; ++slot)
            {
                if (property == nullptr
                    || (property->type == self->uris.atom_URID
                        && reinterpret_cast<const LV2_Atom_URID*> (property)->body == self->uris.fileSlot[slot]))
                    writePathNotification (self, slot);
            }
        }
        else if (obj->body.otype == self->uris.patch_Set)
        {
            const LV2_Atom* property = nullptr;
            const LV2_Atom* value = nullptr;

            lv2_atom_object_get (obj,
                                 self->uris.patch_property, &property,
                                 self->uris.patch_value, &value,
                                 0);

            // An empty path is not rejected: it is how the host asks for a slot
            // to be cleared. It travels down the same worker path as a load and
            // comes back with a null object, which kWorkApply installs -- freeing
            // whatever was loaded and emptying filePaths[slot].
            if (property == nullptr || property->type != self->uris.atom_URID
                || value == nullptr || value->type != self->uris.atom_Path
                || value->size >= kMaxFileName)
                continue;

            const auto propertyUrid = reinterpret_cast<const LV2_Atom_URID*> (property)->body;

            for (int slot = 0; slot < kNumFileSlots; ++slot)
            {
                if (propertyUrid == self->uris.fileSlot[slot])
                {
                    LoadMsg msg = { kWorkLoad, slot, {} };
                    memcpy (msg.path, value + 1, value->size);
                    self->schedule->schedule_work (self->schedule->handle, sizeof (msg), &msg);
                    break;
                }
            }
        }
    }

    if (nSamples == 0 || self->audioIn == nullptr || self->audioOutL == nullptr)
        return;

    // ------------------------------------------------------------ controls
    self->toneStack.setParams ((int) param (self, kPortTsModel),
                               param (self, kPortTsBass, 0.5f),
                               param (self, kPortTsMid, 0.5f),
                               param (self, kPortTsTreble, 0.5f),
                               param (self, kPortTsComp, 1.0f) > 0.5f);
    const bool tsIsPre = param (self, kPortTsPosition) < 0.5f;
    const bool tsOn = param (self, kPortTsOn, 1.0f) > 0.5f;

    float geqGains[nsdsp::GraphicEQ::numBands];
    for (int band = 0; band < nsdsp::GraphicEQ::numBands; ++band)
        geqGains[band] = param (self, (PortIndex) (kPortGeqBand1 + band));
    self->graphicEq.setGains (geqGains);

    const bool geqIsPre = param (self, kPortGeqPosition, 1.0f) < 0.5f;
    const bool geqOn = param (self, kPortGeqOn) > 0.5f;

    for (int slot = 0; slot < 4; ++slot)
        self->irMixer.setSlotParams (slot,
                                     param (self, (PortIndex) (kPortIr1On + slot * 3)) > 0.5f,
                                     param (self, (PortIndex) (kPortIr1Gain + slot * 3)),
                                     param (self, (PortIndex) (kPortIr1Pan + slot * 3)));

    {
        nsdsp::Spread::Params sp;
        sp.offsetMs = param (self, kPortSprOffset, 15.0f);
        sp.wobbleDepth = param (self, kPortSprWobbleOn, 1.0f) > 0.5f
                             ? param (self, kPortSprWobble, 0.25f)
                             : 0.0f;
        sp.crossoverHz = param (self, kPortSprCrossover, nsdsp::Spread::defaultCrossoverHz);
        sp.crossoverOn = param (self, kPortSprCrossoverOn, 1.0f) > 0.5f;
        sp.diffuseOn = param (self, kPortSprDiffuseOn, 1.0f) > 0.5f;
        self->spread.setParams (param (self, kPortSprOn) > 0.5f, sp);
    }

    if (self->model != nullptr)
        self->model->setConditioning (param (self, kPortAidaParam1, 0.5f),
                                      param (self, kPortAidaParam2, 0.5f));

    // Quality (slimmable A2 models): applied on the worker thread.
    const auto quality = param (self, kPortQuality, 1.0f);
    if (self->model != nullptr
        && (self->qualityDirty || std::abs (quality - self->appliedQuality) > 1e-4f))
    {
        QualityMsg msg = { kWorkSetQuality, self->model, quality };
        if (self->schedule->schedule_work (self->schedule->handle, sizeof (msg), &msg) == LV2_WORKER_SUCCESS)
        {
            self->appliedQuality = quality;
            self->qualityDirty = false;
        }
    }

    const float inGainTarget = std::pow (10.0f, param (self, kPortInGain) * 0.05f);
    const float outGainTarget = std::pow (10.0f, param (self, kPortOutGain) * 0.05f);

    // ------------------------------------------------------------- process
    uint32_t done = 0;
    while (done < nSamples)
    {
        const auto n = (int) std::min<uint32_t> (nSamples - done, kMaxChunk);
        const float* in = self->audioIn + done;
        float* outL = self->audioOutL + done;
        float* outR = self->audioOutR + done;

        auto* mono = self->mono.data();

        for (int i = 0; i < n; ++i)
        {
            self->inGainSmoothed += self->gainCoeff * (inGainTarget - self->inGainSmoothed);
            mono[i] = in[i] * self->inGainSmoothed;
        }

        // Each EQ picks its own side of the neural model. Running the tone stack
        // before the graphic EQ within both the pre and the post block is what
        // gives the required ordering: when the two land on the same side, the
        // graphic EQ follows the tone stack.
        if (tsIsPre && tsOn)
            self->toneStack.processBlock (mono, n);
        if (geqIsPre && geqOn)
            self->graphicEq.processBlock (mono, n);

        if (self->model != nullptr)
            self->model->process (mono, n);

        if (! tsIsPre && tsOn)
            self->toneStack.processBlock (mono, n);
        if (! geqIsPre && geqOn)
            self->graphicEq.processBlock (mono, n);

        self->irMixer.process (mono, self->busL.data(), self->busR.data(), n);

        if (self->spread.isRunning())
            self->spread.process (self->busL.data(), self->busR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            self->outGainSmoothed += self->gainCoeff * (outGainTarget - self->outGainSmoothed);
            outL[i] = self->busL[(size_t) i] * self->outGainSmoothed;
            outR[i] = self->busR[(size_t) i] * self->outGainSmoothed;
        }

        done += (uint32_t) n;
    }

    // ------------------------------------------------------------- latency
    if (self->latencyPort != nullptr)
    {
        const auto modelLatency = (self->model != nullptr) ? self->model->getLatencySamples() : 0;
        *self->latencyPort = (float) (modelLatency + self->irMixer.getLatencySamples());
    }

    lv2_atom_forge_pop (&self->forge, &self->notifyFrame);
}

void deactivate (LV2_Handle instance)
{
    static_cast<NamStackMod*> (instance)->activated = false;
}

void cleanup (LV2_Handle instance)
{
    auto* self = static_cast<NamStackMod*> (instance);

    delete self->model;
    for (int slot = 0; slot < 4; ++slot)
        delete self->irMixer.exchangeConvolver (slot, nullptr);

    delete self;
}

uint32_t lv2OptionsGet (LV2_Handle, LV2_Options_Option*)
{
    return LV2_OPTIONS_ERR_UNKNOWN;
}

uint32_t lv2OptionsSet (LV2_Handle instance, const LV2_Options_Option* options)
{
    return optionsSet (static_cast<NamStackMod*> (instance), options);
}

const void* extensionData (const char* uri)
{
    static const LV2_Worker_Interface workerInterface = { work, workResponse, nullptr };
    static const LV2_State_Interface stateInterface = { save, restore };
    static const LV2_Options_Interface optionsInterface = { lv2OptionsGet, lv2OptionsSet };

    if (strcmp (uri, LV2_WORKER__interface) == 0)
        return &workerInterface;
    if (strcmp (uri, LV2_STATE__interface) == 0)
        return &stateInterface;
    if (strcmp (uri, LV2_OPTIONS__interface) == 0)
        return &optionsInterface;

    return nullptr;
}

const LV2_Descriptor descriptor = {
    NAMSTACK_URI,
    instantiate,
    connectPort,
    activate,
    run,
    deactivate,
    cleanup,
    extensionData,
};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor (uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}
