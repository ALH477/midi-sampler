/**
 * @file sampler_rt.c
 * @brief Real-time optimized sampler implementation
 * 
 * Key RT optimizations:
 * - Lock-free event queue for note on/off
 * - No allocations in audio thread
 * - Minimal locking (only for control operations)
 * - Cache-aligned data structures
 * - RT thread priority support
 */

#include "internal/internal_rt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <sys/mman.h>

/* Forward declarations */
extern ms_error_t load_wav_file(const char *filepath, ms_sample_data_t *sample);
extern void sample_data_destroy(ms_sample_data_t *sample);

/* ============================================================================
 * Sampler Lifecycle
 * ========================================================================== */

ms_error_t ms_sampler_create(const ms_audio_config_t *config, ms_sampler_t **sampler) {
    if (!config || !sampler) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    /* Allocate cache-aligned memory */
    ms_sampler_t *s = (ms_sampler_t*)aligned_alloc(MS_CACHE_LINE_SIZE, sizeof(ms_sampler_t));
    if (!s) {
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    memset(s, 0, sizeof(*s));
    s->config = *config;
    s->next_voice_id = 1;
    s->rt_priority = MS_RT_PRIORITY;
    s->rt_enabled = false;
    
    /* Initialize lock-free event queue */
    atomic_init(&s->event_queue.write_idx, 0);
    atomic_init(&s->event_queue.read_idx, 0);
    
    /* Initialize voices */
    for (size_t i = 0; i < config->max_polyphony && i < MS_MAX_VOICES; i++) {
        voice_init(&s->voices[i], s->next_voice_id++, config->sample_rate);
    }
    
    pthread_mutex_init(&s->control_lock, NULL);
    atomic_init(&s->is_playing, false);
    atomic_init(&s->frames_processed, 0);
    atomic_init(&s->xruns, 0);
    
    *sampler = s;
    return MS_SUCCESS;
}

void ms_sampler_destroy(ms_sampler_t *sampler) {
    if (!sampler) return;
    
    pthread_mutex_lock(&sampler->control_lock);
    
    if (sampler->current_track) {
        midi_track_destroy(sampler->current_track);
        free(sampler->current_track);
    }
    
    pthread_mutex_unlock(&sampler->control_lock);
    pthread_mutex_destroy(&sampler->control_lock);
    
    free(sampler);
}

/**
 * @brief Enable real-time mode with thread priority and memory locking
 */
ms_error_t ms_sampler_enable_rt(ms_sampler_t *sampler, int priority) {
    if (!sampler) return MS_ERROR_INVALID_PARAM;
    
    sampler->rt_priority = priority;
    
    /* Lock all current and future pages in memory */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "Warning: Could not lock memory (may need CAP_IPC_LOCK)\n");
        /* Don't fail - RT will still work, just with potential page faults */
    }
    
    /* Set RT scheduling for current thread */
    struct sched_param param;
    param.sched_priority = priority;
    
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        fprintf(stderr, "Warning: Could not set RT priority (may need CAP_SYS_NICE)\n");
        /* Continue anyway */
    } else {
        sampler->rt_enabled = true;
        fprintf(stderr, "RT mode enabled: priority=%d\n", priority);
    }
    
    return MS_SUCCESS;
}

/* ============================================================================
 * Instrument Management
 * ========================================================================== */

ms_error_t ms_instrument_create(ms_sampler_t *sampler, const char *name, 
                                ms_instrument_t **instrument) {
    if (!sampler || !instrument) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    ms_instrument_t *inst = (ms_instrument_t*)calloc(1, sizeof(ms_instrument_t));
    if (!inst) {
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    if (name) {
        strncpy(inst->name, name, sizeof(inst->name) - 1);
    }
    
    /* Default envelope (optimized values for RT) */
    inst->envelope.attack_time = 0.005f;   /* 5ms */
    inst->envelope.decay_time = 0.05f;     /* 50ms */
    inst->envelope.sustain_level = 0.7f;
    inst->envelope.release_time = 0.1f;    /* 100ms */
    
    inst->pitch_bend_range = 2.0f;
    inst->sampler = sampler;
    
    *instrument = inst;
    return MS_SUCCESS;
}

ms_error_t ms_instrument_load_sample(ms_instrument_t *instrument, 
                                     const char *filepath,
                                     const ms_sample_metadata_t *metadata) {
    if (!instrument || !filepath || !metadata) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    if (instrument->num_samples >= MS_MAX_SAMPLES_PER_INSTRUMENT) {
        return MS_ERROR_BUFFER_OVERFLOW;
    }
    
    /* Allocate cache-aligned sample structure */
    ms_sample_data_t *sample = (ms_sample_data_t*)aligned_alloc(
        MS_CACHE_LINE_SIZE, sizeof(ms_sample_data_t)
    );
    if (!sample) {
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    memset(sample, 0, sizeof(*sample));
    
    ms_error_t err = load_wav_file(filepath, sample);
    if (err != MS_SUCCESS) {
        free(sample);
        return err;
    }
    
    /* Reallocate sample data cache-aligned for better performance */
    size_t data_size = sample->num_frames * sample->channels * sizeof(float);
    float *aligned_data = (float*)aligned_alloc(MS_CACHE_LINE_SIZE, data_size);
    if (aligned_data) {
        memcpy(aligned_data, sample->data, data_size);
        free(sample->data);
        sample->data = aligned_data;
    }
    
    sample->meta = *metadata;
    instrument->samples[instrument->num_samples++] = sample;
    
    return MS_SUCCESS;
}

ms_error_t ms_instrument_load_sample_memory(ms_instrument_t *instrument,
                                           const float *data, size_t num_frames,
                                           uint16_t channels,
                                           const ms_sample_metadata_t *metadata) {
    if (!instrument || !data || !metadata) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    if (instrument->num_samples >= MS_MAX_SAMPLES_PER_INSTRUMENT) {
        return MS_ERROR_BUFFER_OVERFLOW;
    }
    
    ms_sample_data_t *sample = (ms_sample_data_t*)aligned_alloc(
        MS_CACHE_LINE_SIZE, sizeof(ms_sample_data_t)
    );
    if (!sample) {
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    memset(sample, 0, sizeof(*sample));
    
    /* Allocate cache-aligned sample data */
    size_t data_size = num_frames * channels * sizeof(float);
    sample->data = (float*)aligned_alloc(MS_CACHE_LINE_SIZE, data_size);
    if (!sample->data) {
        free(sample);
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    memcpy(sample->data, data, data_size);
    sample->num_frames = num_frames;
    sample->channels = channels;
    sample->meta = *metadata;
    
    instrument->samples[instrument->num_samples++] = sample;
    
    return MS_SUCCESS;
}

ms_error_t ms_instrument_set_envelope(ms_instrument_t *instrument, 
                                      const ms_envelope_t *envelope) {
    if (!instrument || !envelope) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    instrument->envelope = *envelope;
    return MS_SUCCESS;
}

void ms_instrument_destroy(ms_instrument_t *instrument) {
    if (!instrument) return;
    
    for (size_t i = 0; i < instrument->num_samples; i++) {
        if (instrument->samples[i]) {
            sample_data_destroy(instrument->samples[i]);
            free(instrument->samples[i]);
        }
    }
    
    free(instrument);
}

ms_sample_data_t* instrument_find_sample(ms_instrument_t *instrument, 
                                         uint8_t note, uint8_t velocity) {
    if (!instrument) return NULL;
    
    ms_sample_data_t *best_match = NULL;
    int min_note_distance = 128;
    
    /* Prefetch first sample for better cache performance */
    if (instrument->num_samples > 0) {
        PREFETCH_READ(instrument->samples[0]);
    }
    
    for (size_t i = 0; i < instrument->num_samples; i++) {
        ms_sample_data_t *sample = instrument->samples[i];
        
        if (velocity >= sample->meta.velocity_low && 
            velocity <= sample->meta.velocity_high) {
            
            int note_distance = abs(note - sample->meta.root_note);
            if (note_distance < min_note_distance) {
                min_note_distance = note_distance;
                best_match = sample;
            }
        }
    }
    
    /* Fallback to closest note */
    if (!best_match && instrument->num_samples > 0) {
        for (size_t i = 0; i < instrument->num_samples; i++) {
            ms_sample_data_t *sample = instrument->samples[i];
            int note_distance = abs(note - sample->meta.root_note);
            if (note_distance < min_note_distance) {
                min_note_distance = note_distance;
                best_match = sample;
            }
        }
    }
    
    return best_match;
}

/* ============================================================================
 * Playback Control (Lock-free RT-safe API)
 * ========================================================================== */

ms_error_t ms_note_on(ms_instrument_t *instrument, uint8_t note, 
                      uint8_t velocity, uint32_t *voice_id) {
    if (!instrument || !instrument->sampler) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    /* Push event to lock-free queue for processing in RT thread */
    rt_event_t event = {
        .note = note,
        .velocity = velocity,
        .event_type = 0,  /* note_on */
        .instrument = instrument
    };
    
    if (!rt_queue_push(&instrument->sampler->event_queue, &event)) {
        return MS_ERROR_BUFFER_OVERFLOW;  /* Queue full */
    }
    
    return MS_SUCCESS;
}

ms_error_t ms_note_off(ms_instrument_t *instrument, uint8_t note) {
    if (!instrument || !instrument->sampler) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    /* Push event to lock-free queue */
    rt_event_t event = {
        .note = note,
        .velocity = 0,
        .event_type = 1,  /* note_off */
        .instrument = instrument
    };
    
    if (!rt_queue_push(&instrument->sampler->event_queue, &event)) {
        return MS_ERROR_BUFFER_OVERFLOW;
    }
    
    return MS_SUCCESS;
}

void ms_all_notes_off(ms_sampler_t *sampler) {
    if (!sampler) return;
    
    /* This is a non-RT operation, safe to use direct access */
    for (size_t i = 0; i < sampler->config.max_polyphony && i < MS_MAX_VOICES; i++) {
        sampler->voices[i].active = false;
    }
}

ms_error_t ms_pitch_bend(ms_instrument_t *instrument, int16_t value) {
    if (!instrument) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    instrument->current_pitch_bend = value;
    
    /* Update active voices (this is atomic on each voice) */
    if (instrument->sampler) {
        for (size_t i = 0; i < MS_MAX_VOICES; i++) {
            voice_t *voice = &instrument->sampler->voices[i];
            if (voice->active && voice->instrument == instrument) {
                float semitones = (value / 8192.0f) * instrument->pitch_bend_range;
                voice->pitch_bend_multiplier = powf(2.0f, semitones / 12.0f);
            }
        }
    }
    
    return MS_SUCCESS;
}

/* ============================================================================
 * Audio Processing (RT-safe, lock-free)
 * ========================================================================== */

/**
 * @brief Process pending events from lock-free queue
 */
static FORCE_INLINE void process_events(ms_sampler_t *sampler) {
    rt_event_t event;
    
    /* Process up to queue size events per call */
    for (int i = 0; i < RT_EVENT_QUEUE_SIZE; i++) {
        if (!rt_queue_pop(&sampler->event_queue, &event)) {
            break;  /* Queue empty */
        }
        
        ms_instrument_t *inst = (ms_instrument_t*)event.instrument;
        
        if (event.event_type == 0) {
            /* Note On */
            ms_sample_data_t *sample = instrument_find_sample(inst, event.note, event.velocity);
            if (!sample) continue;
            
            /* Find available voice */
            voice_t *available_voice = NULL;
            for (size_t j = 0; j < sampler->config.max_polyphony && j < MS_MAX_VOICES; j++) {
                if (!sampler->voices[j].active) {
                    available_voice = &sampler->voices[j];
                    break;
                }
            }
            
            /* Voice stealing if needed */
            if (!available_voice) {
                available_voice = &sampler->voices[0];
            }
            
            voice_trigger(available_voice, sample, event.note, event.velocity, &inst->envelope);
            available_voice->instrument = inst;
            
        } else {
            /* Note Off */
            for (size_t j = 0; j < sampler->config.max_polyphony && j < MS_MAX_VOICES; j++) {
                voice_t *voice = &sampler->voices[j];
                if (voice->active && voice->note == event.note && voice->instrument == inst) {
                    voice_release(voice);
                }
            }
        }
    }
}

ms_error_t ms_process(ms_sampler_t *sampler, float *output, size_t num_frames) {
    if (UNLIKELY(!sampler || !output)) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    /* Clear output buffer (optimized memset) */
    const size_t buffer_size = num_frames * sampler->config.channels;
    memset(output, 0, buffer_size * sizeof(float));
    
    /* Process pending events from lock-free queue */
    process_events(sampler);
    
    /* Process all active voices */
    const uint16_t max_voices = sampler->config.max_polyphony < MS_MAX_VOICES ? 
                                sampler->config.max_polyphony : MS_MAX_VOICES;
    
    for (size_t i = 0; i < max_voices; i++) {
        if (LIKELY(sampler->voices[i].active)) {
            voice_process(&sampler->voices[i], output, num_frames, sampler->config.channels);
        }
    }
    
    /* Update statistics */
    atomic_fetch_add_explicit(&sampler->frames_processed, num_frames, memory_order_relaxed);
    
    return MS_SUCCESS;
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

const char* ms_error_string(ms_error_t error) {
    switch (error) {
        case MS_SUCCESS: return "Success";
        case MS_ERROR_INVALID_PARAM: return "Invalid parameter";
        case MS_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case MS_ERROR_FILE_NOT_FOUND: return "File not found";
        case MS_ERROR_INVALID_FORMAT: return "Invalid format";
        case MS_ERROR_BUFFER_OVERFLOW: return "Buffer overflow";
        case MS_ERROR_NOT_INITIALIZED: return "Not initialized";
        case MS_ERROR_VOICE_LIMIT: return "Voice limit reached";
        default: return "Unknown error";
    }
}

const char* ms_version(void) {
    return MS_VERSION;
}

/**
 * @brief Get RT performance statistics
 */
void ms_get_stats(const ms_sampler_t *sampler, uint64_t *frames, uint32_t *xruns) {
    if (!sampler) return;
    
    if (frames) {
        *frames = atomic_load_explicit(&sampler->frames_processed, memory_order_relaxed);
    }
    if (xruns) {
        *xruns = atomic_load_explicit(&sampler->xruns, memory_order_relaxed);
    }
}
