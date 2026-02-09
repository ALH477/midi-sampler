/**
 * @file envelope.c
 * @brief ADSR envelope generator implementation
 */

#include "internal/internal.h"
#include <math.h>
#include <string.h>

void envelope_init(envelope_generator_t *env, float sample_rate, const ms_envelope_t *params) {
    if (!env || !params) return;
    
    memset(env, 0, sizeof(*env));
    env->sample_rate = sample_rate;
    env->params = *params;
    env->stage = ENV_IDLE;
    env->current_level = 0.0f;
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

float envelope_process(envelope_generator_t *env) {
    if (!env) return 0.0f;
    
    float output = env->current_level;
    
    switch (env->stage) {
        case ENV_IDLE:
            env->current_level = 0.0f;
            break;
            
        case ENV_ATTACK:
            if (env->samples_processed < env->stage_samples) {
                /* Linear attack */
                env->current_level = (float)env->samples_processed / (float)env->stage_samples;
                env->samples_processed++;
            } else {
                /* Move to decay stage */
                env->stage = ENV_DECAY;
                env->stage_samples = (uint32_t)(env->params.decay_time * env->sample_rate);
                env->samples_processed = 0;
                env->current_level = 1.0f;
                
                if (env->stage_samples == 0) {
                    env->stage_samples = 1;
                }
            }
            break;
            
        case ENV_DECAY:
            if (env->samples_processed < env->stage_samples) {
                /* Linear decay from 1.0 to sustain level */
                float t = (float)env->samples_processed / (float)env->stage_samples;
                env->current_level = 1.0f - t * (1.0f - env->params.sustain_level);
                env->samples_processed++;
            } else {
                /* Move to sustain stage */
                env->stage = ENV_SUSTAIN;
                env->current_level = env->params.sustain_level;
            }
            break;
            
        case ENV_SUSTAIN:
            env->current_level = env->params.sustain_level;
            break;
            
        case ENV_RELEASE:
            if (env->samples_processed < env->stage_samples) {
                /* Linear release to zero */
                float start_level = env->params.sustain_level;
                float t = (float)env->samples_processed / (float)env->stage_samples;
                env->current_level = start_level * (1.0f - t);
                env->samples_processed++;
            } else {
                /* Move to idle */
                env->stage = ENV_IDLE;
                env->current_level = 0.0f;
            }
            break;
    }
    
    /* Clamp to valid range */
    if (env->current_level < 0.0f) env->current_level = 0.0f;
    if (env->current_level > 1.0f) env->current_level = 1.0f;
    
    return output;
}

bool envelope_is_active(const envelope_generator_t *env) {
    if (!env) return false;
    return env->stage != ENV_IDLE;
}
