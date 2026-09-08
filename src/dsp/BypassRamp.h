#pragma once

#include <algorithm>

namespace nsdsp
{

// Engage / bypass as a short crossfade rather than a hard switch.
//
// Switching a filter back in by simply calling it again is a step: its state
// froze on whatever passed through it last, and its output at that instant has
// nothing to do with the signal now going by. Measured on the JCM800 tone
// stack, resuming after three seconds off stepped 19x further than the settled
// signal does between samples, and peaked above the input; the graphic EQ at
// +/-12 dB stepped 8x. Clearing the state first brings the tone stack to 1.4x
// but leaves the graphic EQ at 5.2x, because switching a 12 dB curve in is
// inherently a jump. Fading in over 25 ms from a cleared state settles both
// (1.3x and 3.6x), and what remains is a 25 ms ramp rather than a step -- the
// signal genuinely changing, which is what a bypass switch should sound like.
//
// The same ramp covers a change of identity: moving the filter to the other
// side of the neural model, or picking a different tone stack. Its state
// belongs to the point it was reading and to the circuit it was modelling, so
// both clear it and fade in again from dry.
//
// Costs nothing once settled: the caller keeps its plain in-place loop while
// the gain sits at 1, skips the filter entirely at 0, and only pays for the
// blend during the 25 ms itself.
class BypassRamp
{
public:
    void prepare (double sampleRate, double fadeSeconds = 0.025)
    {
        length = std::max (1, (int) (sampleRate * fadeSeconds));
        increment = 1.0f / (float) length;
        current = target = 0.0f;
        identity = -1;
        pendingClear = false;
    }

    // Per block, before processing. `identity` is anything the caller wants to
    // treat as "this is now a different filter" -- see the class comment.
    //
    // A change of identity is a two-stage swap, not a jump: the outgoing
    // setting fades out first, and only once it is silent is the new one
    // adopted and faded in. Cutting straight to dry to fade the new one in
    // was the step this class exists to remove -- measured at 21x the settled
    // signal's own step for a pre/post move, 43x for a change of amplifier.
    void setEngaged (bool engaged, int newIdentity)
    {
        wanted = newIdentity;
        wantedEngaged = engaged;

        if (identity != wanted)
        {
            if (current > 0.0f)
            {
                target = 0.0f;   // fade the outgoing setting out first
                return;
            }

            identity = wanted;   // silent now: adopt it and clear the state
            pendingClear = true;
        }

        target = wantedEngaged ? 1.0f : 0.0f;
    }

    // True while an identity change is still fading the outgoing setting out.
    // Callers whose coefficients depend on the identity hold their update
    // until this clears, so the fade-out runs through the setting that is
    // actually being heard.
    bool isSwapping() const { return identity != wanted; }

    // Bypass without declaring a new identity, so the caller fades out through
    // whatever it was last set to instead of cutting to dry. Used when the
    // filter is being switched off rather than reconfigured.
    void bypassKeepingIdentity() { target = 0.0f; }

    // False = fully bypassed and settled; the caller can skip the filter.
    bool isRunning() const { return current > 0.0f || target > 0.0f; }

    // True while the gain is moving, i.e. while the caller must blend rather
    // than write in place.
    bool isFading() const { return current != target; }

    // Consumes the flag: the caller clears its filter state when this returns
    // true, before processing the block.
    bool takeClearRequest()
    {
        const auto clear = pendingClear;
        pendingClear = false;
        return clear;
    }

    float next()
    {
        if (current < target)      current = std::min (target, current + increment);
        else if (current > target) current = std::max (target, current - increment);
        return current;
    }

    float value() const { return current; }

private:
    int length = 1;
    int identity = -1, wanted = -1;
    bool wantedEngaged = false;
    float increment = 1.0f;
    float current = 0.0f, target = 0.0f;
    bool pendingClear = false;
};

} // namespace nsdsp
