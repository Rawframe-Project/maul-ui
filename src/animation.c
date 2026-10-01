// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Timed records follow their easing from where the property was to its
// target; spring records follow the closed-form spring from their start
// offset and velocity. A record whose node is gone is freed the next time
// transitions advance.

#include "animation.h"

#include "property.h"

#include <math.h>

#define NANOSECONDS 1e9
#define TWO_PI      6.28318530717958647692

// A spring still moving after this long is put at its target.
#define LONGEST_SPRING 60.0

const muiTransitionSpec* muiFindSpec(const muiAnimationStore* store, muiTransitionId id)
{
    uint32_t slot = muiPoolResolve(&store->specPool, id.index1, id.generation);
    return slot != 0 ? &store->specs[slot - 1] : nullptr;
}

uint32_t muiFindAnimation(const muiMotion* motion, uint32_t node, muiProperty property)
{
    for (uint32_t at = motion->styles[node - 1].firstAnimation; at != 0;
         at = motion->store->records[at - 1].next)
    {
        if (motion->store->records[at - 1].property == property)
        {
            return at;
        }
    }
    return 0;
}

static void Unlink(const muiMotion* motion, uint32_t node, uint32_t record)
{
    uint32_t* link = &motion->styles[node - 1].firstAnimation;
    while (*link != record)
    {
        link = &motion->store->records[*link - 1].next;
    }
    *link = motion->store->records[record - 1].next;
}

// Frees a record; node is its live node, or 0 when the node is gone.
static void Release(const muiMotion* motion, uint32_t node, uint32_t record)
{
    if (node != 0)
    {
        Unlink(motion, node, record);
    }
    muiPoolGive(&motion->store->pool, record);
    motion->store->running--;
}

static double Seconds(uint64_t later, uint64_t earlier)
{
    return later > earlier ? (double)(later - earlier) / NANOSECONDS : 0.0;
}

// Where a timed record is at elapsed seconds into its motion, as the
// share of the way its easing gives.
static double TimedProgress(const muiAnimation* animation, double elapsed)
{
    if (animation->seconds <= 0.0 || elapsed >= animation->seconds)
    {
        return 1.0;
    }
    const muiTransitionSpec* spec = &animation->spec;
    double x = elapsed / animation->seconds;
    return spec->def.easing == mui_easingLinear ? x : muiEase(&spec->curve, x);
}

// A record's channels and velocity at nowNs; whether it has arrived.
static bool Sample(const muiAnimation* animation, uint64_t nowNs, float value[2],
                   double velocity[2])
{
    velocity[0] = 0.0;
    velocity[1] = 0.0;
    double elapsed = Seconds(nowNs, animation->startNs);
    if (animation->spec.def.kind == mui_transitionTimed)
    {
        double progress = nowNs < animation->startNs ? 0.0 : TimedProgress(animation, elapsed);
        for (uint32_t i = 0; i < animation->channels; i++)
        {
            double span = (double)animation->to[i] - (double)animation->from[i];
            value[i] = (float)((double)animation->from[i] + span * progress);
        }
        return nowNs >= animation->startNs && elapsed >= animation->seconds;
    }
    bool resting = true;
    double restSpeed = animation->restOffset * TWO_PI * (double)animation->spec.def.frequency;
    for (uint32_t i = 0; i < animation->channels; i++)
    {
        double offset = (double)animation->from[i] - (double)animation->to[i];
        if (nowNs >= animation->startNs)
        {
            muiSpringAt(&animation->springs[i], elapsed, &offset, &velocity[i]);
        }
        value[i] = (float)((double)animation->to[i] + offset);
        resting =
            resting && fabs(offset) <= animation->restOffset && fabs(velocity[i]) <= restSpeed;
    }
    return nowNs >= animation->startNs && (resting || elapsed >= LONGEST_SPRING);
}

// CSS Transitions' shortening of a reversal: the share of the way the
// old transition covered, in value, not time.
static double Shortening(const muiAnimation* old, uint64_t nowNs)
{
    double eased = nowNs < old->startNs ? 0.0 : TimedProgress(old, Seconds(nowNs, old->startNs));
    double share = eased * old->shortening + (1.0 - old->shortening);
    return fmin(fabs(share), 1.0);
}

static bool IsSame(const float a[2], const float b[2], uint32_t channels)
{
    return a[0] == b[0] && (channels < 2 || a[1] == b[1]);
}

// Takes a record for a node's property: its running one, or a new one
// linked first. 0 when none is free.
static uint32_t RecordFor(const muiMotion* motion, uint32_t node, muiProperty property)
{
    uint32_t record = muiFindAnimation(motion, node, property);
    if (record != 0)
    {
        return record;
    }
    record = muiPoolTake(&motion->store->pool);
    if (record != 0)
    {
        motion->store->running++;
        motion->store->records[record - 1] = (muiAnimation){
            .next = motion->styles[node - 1].firstAnimation,
            .property = property,
        };
        motion->styles[node - 1].firstAnimation = record;
    }
    return record;
}

bool muiStartAnimation(const muiMotion* motion, uint32_t node, muiProperty property,
                       const float target[2], const muiTransitionSpec* spec, uint64_t nowNs)
{
    float current[2] = {0.0f, 0.0f};
    uint32_t channels = muiPropertyChannels(&motion->nodes[node - 1].style, property, current);
    uint32_t running = muiFindAnimation(motion, node, property);
    double velocity[2] = {0.0, 0.0};
    float reversingStart[2] = {current[0], current[1]};
    double shortening = 1.0;
    if (running != 0)
    {
        const muiAnimation* old = &motion->store->records[running - 1];
        float sampled[2] = {0.0f, 0.0f};
        (void)Sample(old, nowNs, sampled, velocity);
        bool reversal = old->spec.def.kind == mui_transitionTimed &&
                        spec->def.kind == mui_transitionTimed &&
                        IsSame(target, old->reversingStart, channels);
        if (reversal)
        {
            shortening = Shortening(old, nowNs);
            reversingStart[0] = old->to[0];
            reversingStart[1] = old->to[1];
        }
    }
    uint32_t record = RecordFor(motion, node, property);
    if (record == 0)
    {
        return false;
    }
    muiAnimation* animation = &motion->store->records[record - 1];
    animation->node = muiTreeIdOf(motion->tree, node);
    animation->channels = channels;
    animation->spec = *spec;
    animation->changedNs = nowNs;
    animation->startNs = nowNs + spec->def.delayNs;
    animation->seconds = (double)spec->def.durationNs / NANOSECONDS * shortening;
    animation->shortening = shortening;
    // The way a spring goes: its offset, or as far as its speed alone
    // would carry it in a radian of its motion.
    double way = 0.0;
    double w0 = TWO_PI * (double)spec->def.frequency;
    for (uint32_t i = 0; i < 2; i++)
    {
        animation->from[i] = current[i];
        animation->to[i] = i < channels ? target[i] : 0.0f;
        animation->reversingStart[i] = reversingStart[i];
        double offset = (double)current[i] - (double)animation->to[i];
        way = fmax(way, fmax(fabs(offset), fabs(velocity[i]) / w0));
        animation->springs[i] = muiMakeSpring((double)spec->def.frequency,
                                              (double)spec->def.dampingRatio, offset, velocity[i]);
    }
    // At rest within a thousandth of the way.
    animation->restOffset = way * 1e-3;
    return true;
}

void muiStopAnimation(const muiMotion* motion, uint32_t node, muiProperty property)
{
    uint32_t record = muiFindAnimation(motion, node, property);
    if (record != 0)
    {
        Release(motion, node, record);
    }
}

void muiAdvanceAnimations(const muiMotion* motion, uint64_t nowNs, bool finish)
{
    muiAnimationStore* store = motion->store;
    for (uint32_t record = 1; store->running != 0 && record <= store->pool.used; record++)
    {
        if (!store->pool.slots[record - 1].live)
        {
            continue;
        }
        const muiAnimation* animation = &store->records[record - 1];
        uint32_t node = muiTreeResolve(motion->tree, animation->node);
        if (node == 0)
        {
            Release(motion, 0, record);
            continue;
        }
        float value[2] = {0.0f, 0.0f};
        double velocity[2] = {0.0, 0.0};
        bool arrived = Sample(animation, nowNs, value, velocity) || finish;
        if (arrived)
        {
            value[0] = animation->to[0];
            value[1] = animation->to[1];
        }
        muiSetPropertyChannels(&motion->nodes[node - 1].style, animation->property, value);
        muiTreeMarkLayout(motion->tree, node);
        if (arrived)
        {
            Release(motion, node, record);
        }
    }
}

bool muiIsAnimatingUnder(const muiAnimationStore* store, const muiTree* tree, uint32_t root)
{
    for (uint32_t record = 1; store->running != 0 && record <= store->pool.used; record++)
    {
        if (!store->pool.slots[record - 1].live)
        {
            continue;
        }
        uint32_t node = muiTreeResolve(tree, store->records[record - 1].node);
        if (node != 0 && muiTreeIsAncestor(tree, root, node))
        {
            return true;
        }
    }
    return false;
}
