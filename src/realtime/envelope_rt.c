/**
 * @file envelope_rt.c
 * @brief Real-time optimized ADSR envelope generator
 * 
 * Optimizations:
 * - Pre-calculated coefficients to avoid division in RT path
 * - Inlined processing (see internal_rt.h)
 * - Minimal branching
 */

#include "internal/internal_rt.h"
#include <math.h>
#include <string.h>

void envelope_init(envelope_generator_t *env, float sample_rate, const ms_envelope_t *params) {
    if (!env || !params) return;
    
    memset(env, 0, sizeof(*env));
    env->sample_rate = sample_rate;
    env->params = *params;
    env->stage = ENV_IDLE;
    env->current_level = 0.0f;
    
    /* Pre-calculate coefficients for linear interpolation */
    /* These avoid division in the hot processing loop */
    
    uint32_t attack_samples = (uint32_t)(params->attack_time * sample_rate);
    if (attack_samples > 0) {
        env->attack_coeff = 1.0f / (float)attack_samples;
    } else {
        env->attack_coeff = 1.0f;
    }
    
    uint32_t decay_samples = (uint32_t)(params->decay_time * sample_rate);
    if (decay_samples > 0) {
        env->decay_coeff = (1.0f - params->sustain_level) / (float)decay_samples;
    } else {
        env->decay_coeff = 0.0f;
    }
    
    uint32_t release_samples = (uint32_t)(params->release_time * sample_rate);
    if (release_samples > 0) {
        env->release_coeff = params->sustain_level / (float)release_samples;
    } else {
        env->release_coeff = params->sustain_level;
    }
}

void envelope_trigger(envelope_generator_t *env) {
    if (!env) return;
    
    env->stage = ENV_ATTACK;
    env->stage_samples = (uint32_t)(env->params.attack_time * env->sample_rate);
    env->samples_processed = 0;
    
    /* Prevent division by zero */
    if (env->stage_samples == 0) {
        env->stage_samples = 1;
    }
}

void envelope_release(envelope_generator_t *env) {
    if (!env) return;
    
    env->stage = ENV_RELEASE;
    env->stage_samples = (uint32_t)(env->params.release_time * env->sample_rate);
    env->samples_processed = 0;
    
    if (env->stage_samples == 0) {
        env->stage_samples = 1;
    }
}

/* Note: envelope_process() is inlined in internal_rt.h for maximum performance */
