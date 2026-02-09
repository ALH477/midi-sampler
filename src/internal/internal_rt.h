/**
 * @file internal_rt.h
 * @brief Real-time optimized internal structures
 * @internal
 * 
 * Optimizations for BORE scheduler and RT Linux:
 * - Lock-free ring buffers for event passing
 * - Cache-aligned structures
 * - Compiler hints for hot paths
 * - RT thread priority management
 */

#ifndef MIDI_SAMPLER_INTERNAL_RT_H
#define _GNU_SOURCE
#define MIDI_SAMPLER_INTERNAL_RT_H

#include "midi_sampler.h"
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#include <sys/mman.h>

/* ============================================================================
 * Real-time Configuration
 * ========================================================================== */

#define MS_RT_PRIORITY 80              /**< Default RT priority */
#define MS_CACHE_LINE_SIZE 64          /**< CPU cache line size */
#define MS_MAX_SAMPLES_PER_INSTRUMENT 128
#define MS_MAX_VOICES 64
#define MS_VERSION "1.0.0-rt"

/* Alignment macros */
#define CACHE_ALIGNED __attribute__((aligned(MS_CACHE_LINE_SIZE)))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define PREFETCH_READ(addr) __builtin_prefetch(addr, 0, 3)
#define PREFETCH_WRITE(addr) __builtin_prefetch(addr, 1, 3)

/* Force inline for hot paths */
#define FORCE_INLINE __attribute__((always_inline)) inline

/* ============================================================================
 * Lock-free Ring Buffer for RT Event Passing
 * ========================================================================== */

#define RT_EVENT_QUEUE_SIZE 256

typedef struct {
    uint8_t note;
    uint8_t velocity;
    uint8_t event_type;  /* 0=note_on, 1=note_off */
    uint8_t padding;
    void *instrument;
} rt_event_t;

typedef struct {
    CACHE_ALIGNED atomic_uint_fast32_t write_idx;
    CACHE_ALIGNED atomic_uint_fast32_t read_idx;
    CACHE_ALIGNED rt_event_t events[RT_EVENT_QUEUE_SIZE];
} rt_event_queue_t;

/* Lock-free queue operations */
static FORCE_INLINE bool rt_queue_push(rt_event_queue_t *q, const rt_event_t *event) {
    uint32_t write_idx = atomic_load_explicit(&q->write_idx, memory_order_relaxed);
    uint32_t next_write = (write_idx + 1) % RT_EVENT_QUEUE_SIZE;
    uint32_t read_idx = atomic_load_explicit(&q->read_idx, memory_order_acquire);
    
    if (UNLIKELY(next_write == read_idx)) {
        return false;  /* Queue full */
    }
    
    q->events[write_idx] = *event;
    atomic_store_explicit(&q->write_idx, next_write, memory_order_release);
    return true;
}

static FORCE_INLINE bool rt_queue_pop(rt_event_queue_t *q, rt_event_t *event) {
    uint32_t read_idx = atomic_load_explicit(&q->read_idx, memory_order_relaxed);
    uint32_t write_idx = atomic_load_explicit(&q->write_idx, memory_order_acquire);
    
    if (UNLIKELY(read_idx == write_idx)) {
        return false;  /* Queue empty */
    }
    
    *event = q->events[read_idx];
    atomic_store_explicit(&q->read_idx, (read_idx + 1) % RT_EVENT_QUEUE_SIZE, 
                         memory_order_release);
    return true;
}

/* ============================================================================
 * Sample Structure (Cache-aligned)
 * ========================================================================== */

typedef struct {
    float *data CACHE_ALIGNED;      /**< PCM data (cache-aligned) */
    size_t num_frames;              /**< Number of audio frames */
    uint16_t channels;              /**< Number of channels */
    ms_sample_metadata_t meta;      /**< Sample metadata */
} ms_sample_data_t;

/* ============================================================================
 * Envelope Generator (Optimized)
 * ========================================================================== */

typedef enum {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} envelope_stage_t;

typedef struct CACHE_ALIGNED {
    envelope_stage_t stage;
    float current_level;
    float sample_rate;
    ms_envelope_t params;
    
    /* Pre-calculated coefficients for fast processing */
    float attack_coeff;
    float decay_coeff;
    float release_coeff;
    
    uint32_t stage_samples;
    uint32_t samples_processed;
} envelope_generator_t;

void envelope_init(envelope_generator_t *env, float sample_rate, const ms_envelope_t *params);
void envelope_trigger(envelope_generator_t *env);
void envelope_release(envelope_generator_t *env);

/* Optimized hot-path envelope processing */
static FORCE_INLINE float envelope_process(envelope_generator_t *env) {
    float output = env->current_level;
    
    switch (env->stage) {
        case ENV_IDLE:
            return 0.0f;
            
        case ENV_ATTACK:
            if (LIKELY(env->samples_processed < env->stage_samples)) {
                env->current_level += env->attack_coeff;
                env->samples_processed++;
            } else {
                env->stage = ENV_DECAY;
                env->stage_samples = (uint32_t)(env->params.decay_time * env->sample_rate);
                env->samples_processed = 0;
                env->current_level = 1.0f;
            }
            break;
            
        case ENV_DECAY:
            if (LIKELY(env->samples_processed < env->stage_samples)) {
                env->current_level -= env->decay_coeff;
                env->samples_processed++;
            } else {
                env->stage = ENV_SUSTAIN;
                env->current_level = env->params.sustain_level;
            }
            break;
            
        case ENV_SUSTAIN:
            env->current_level = env->params.sustain_level;
            break;
            
        case ENV_RELEASE:
            if (LIKELY(env->samples_processed < env->stage_samples)) {
                env->current_level -= env->release_coeff;
                env->samples_processed++;
            } else {
                env->stage = ENV_IDLE;
                env->current_level = 0.0f;
            }
            break;
    }
    
    /* Clamp */
    if (UNLIKELY(env->current_level < 0.0f)) env->current_level = 0.0f;
    if (UNLIKELY(env->current_level > 1.0f)) env->current_level = 1.0f;
    
    return output;
}

static FORCE_INLINE bool envelope_is_active(const envelope_generator_t *env) {
    return env->stage != ENV_IDLE;
}

/* ============================================================================
 * Voice (Cache-aligned for performance)
 * ========================================================================== */

typedef struct CACHE_ALIGNED {
    bool active;
    uint32_t voice_id;
    uint8_t note;
    uint8_t velocity;
    
    ms_sample_data_t *sample;
    double playback_position;
    double playback_speed;
    
    envelope_generator_t envelope;
    float pitch_bend_multiplier;
    float velocity_gain;  /* Pre-calculated */
    
    struct ms_instrument_t *instrument;
    
    /* Padding to cache line */
    uint8_t padding[MS_CACHE_LINE_SIZE - 
                   (sizeof(bool) + sizeof(uint32_t) + 2 * sizeof(uint8_t) +
                    sizeof(void*) + 2 * sizeof(double) + sizeof(envelope_generator_t) +
                    2 * sizeof(float) + sizeof(void*)) % MS_CACHE_LINE_SIZE];
} voice_t;

void voice_init(voice_t *voice, uint32_t voice_id, float sample_rate);
void voice_trigger(voice_t *voice, ms_sample_data_t *sample, uint8_t note, 
                   uint8_t velocity, const ms_envelope_t *envelope);
void voice_release(voice_t *voice);
void voice_process(voice_t *voice, float *output, size_t num_frames, uint16_t channels);
bool voice_is_active(const voice_t *voice);

/* ============================================================================
 * Instrument
 * ========================================================================== */

struct ms_instrument_t {
    char name[64];
    ms_sample_data_t *samples[MS_MAX_SAMPLES_PER_INSTRUMENT];
    size_t num_samples;
    ms_envelope_t envelope;
    float pitch_bend_range;
    int16_t current_pitch_bend;
    struct ms_sampler_t *sampler;
};

ms_sample_data_t* instrument_find_sample(ms_instrument_t *instrument, 
                                         uint8_t note, uint8_t velocity);

/* ============================================================================
 * MIDI Event
 * ========================================================================== */

typedef enum {
    MIDI_NOTE_ON,
    MIDI_NOTE_OFF,
    MIDI_PITCH_BEND,
    MIDI_CONTROL_CHANGE
} midi_event_type_t;

typedef struct {
    uint32_t timestamp;
    midi_event_type_t type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
} midi_event_t;

typedef struct {
    midi_event_t *events;
    size_t num_events;
    size_t capacity;
    uint32_t ticks_per_beat;
    uint32_t tempo;
} midi_track_t;

ms_error_t midi_parse_file(const char *filepath, midi_track_t *track);
void midi_track_destroy(midi_track_t *track);

/* ============================================================================
 * Sampler (RT-optimized)
 * ========================================================================== */

struct ms_sampler_t {
    ms_audio_config_t config;
    
    /* Voice pool (cache-aligned) */
    voice_t voices[MS_MAX_VOICES] CACHE_ALIGNED;
    uint32_t next_voice_id;
    
    /* Lock-free event queue for RT safety */
    rt_event_queue_t event_queue CACHE_ALIGNED;
    
    /* MIDI playback state */
    midi_track_t *current_track;
    size_t playback_event_index;
    uint64_t playback_sample_count;
    atomic_bool is_playing;
    
    /* RT thread info */
    pthread_t audio_thread;
    int rt_priority;
    bool rt_enabled;
    
    /* Statistics (for monitoring, not in hot path) */
    CACHE_ALIGNED atomic_uint_fast64_t frames_processed;
    CACHE_ALIGNED atomic_uint_fast32_t xruns;
    
    /* Mutex only for non-RT operations */
    pthread_mutex_t control_lock;
};

/* ============================================================================
 * RT Thread Management
 * ========================================================================== */

/**
 * @brief Set current thread to real-time priority
 * 
 * @param priority RT priority (1-99, higher = more important)
 * @return MS_SUCCESS on success, error code otherwise
 */
static inline ms_error_t ms_set_realtime_priority(int priority) {
    struct sched_param param;
    param.sched_priority = priority;
    
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        return MS_ERROR_UNKNOWN;
    }
    
    return MS_SUCCESS;
}

/**
 * @brief Lock memory to prevent paging (critical for RT)
 * 
 * @return MS_SUCCESS on success, error code otherwise
 */
static inline ms_error_t ms_lock_memory(void) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        return MS_ERROR_UNKNOWN;
    }
    return MS_SUCCESS;
}

#endif /* MIDI_SAMPLER_INTERNAL_RT_H */
