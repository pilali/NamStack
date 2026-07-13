// NeuralAmpModelerCore registers its model architectures (WaveNet, LSTM,
// ConvNet, Linear, SlimmableContainer) through static ConfigParserHelper
// instances that live in the same translation units as the create_config
// functions referenced below. When nam_core is linked as a static library,
// the linker would drop those objects — nothing references them directly —
// and loading any .nam file would then fail with "No config parser
// registered". Taking the address of one symbol from each of those
// translation units forces the linker to keep them, static registrars
// included. NeuralModel's constructor calls this function so that this
// translation unit is itself always retained.

#include <NAM/container.h>
#include <NAM/convnet.h>
#include <NAM/linear.h>
#include <NAM/lstm.h>
#include <NAM/wavenet/model.h>
#include <NAM/wavenet/slimmable.h>

namespace nsdsp
{

const void* getNamArchitectureAnchor (int index)
{
    static const void* const anchors[] = {
        reinterpret_cast<const void*> (&nam::linear::create_config),
        reinterpret_cast<const void*> (&nam::lstm::create_config),
        reinterpret_cast<const void*> (&nam::convnet::create_config),
        reinterpret_cast<const void*> (&nam::wavenet::create_config),
        reinterpret_cast<const void*> (&nam::container::create_config),
        reinterpret_cast<const void*> (&nam::slimmable_wavenet::create_config),
    };

    return anchors[index % 6];
}

} // namespace nsdsp
