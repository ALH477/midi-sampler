/**
 * @file sampler.c
 * @brief Main sampler implementation
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

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
    
    ms_sampler_t *s = (ms_sampler_t*)calloc(1, sizeof(ms_sampler_t));
    if (!s) {
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    s->config = *config;
    s->next_voice_id = 1;
    
    /* Initialize voices */
    for (size_t i = 0; i < config->max_polyphony && i < MS_MAX_VOICES; i++) {
        voice_init(&s->voices[i], s->next_voice_id++, config->sample_rate);
    }
    
    pthread_mutex_init(&s->lock, NULL);
    
    *sampler = s;
    return MS_SUCCESS;
}

void ms_sampler_destroy(ms_sampler_t *sampler) {
    if (!sampler) return;
    
    pthread_mutex_lock(&sampler->lock);
    
    if (sampler->current_track) {
        midi_track_destroy(sampler->current_track);
        free(sampler->current_track);
    }
    
    pthread_mutex_unlock(&sampler->lock);
    pthread_mutex_destroy(&sampler->lock);
    
    free(sampler);
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
    
    /* Default envelope */
    inst->envelope.attack_time = 0.01f;
    inst->envelope.decay_time = 0.1f;
    inst->envelope.sustain_level = 0.7f;
    inst->envelope.release_time = 0.3f;
    
    inst->pitch_bend_range = 2.0f; /* ±2 semitones */
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
    
    ms_sample_data_t *sample = (ms_sample_data_t*)calloc(1, sizeof(ms_sample_data_t));
    if (!sample) {
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    ms_error_t err = load_wav_file(filepath, sample);
    if (err != MS_SUCCESS) {
        free(sample);
        return err;
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
    
    ms_sample_data_t *sample = (ms_sample_data_t*)calloc(1, sizeof(ms_sample_data_t));
    if (!sample) {
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    size_t data_size = num_frames * channels * sizeof(float);
    sample->data = (float*)malloc(data_size);
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
    
    /* Find best matching sample based on note and velocity */
    ms_sample_data_t *best_match = NULL;
    int min_note_distance = 128;
    
    for (size_t i = 0; i < instrument->num_samples; i++) {
        ms_sample_data_t *sample = instrument->samples[i];
        
        /* Check velocity range */
        if (velocity >= sample->meta.velocity_low && 
            velocity <= sample->meta.velocity_high) {
            
            int note_distance = abs(note - sample->meta.root_note);
            if (note_distance < min_note_distance) {
                min_note_distance = note_distance;
                best_match = sample;
            }
        }
    }
    
    /* If no velocity match, use any sample with closest note */
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
 * Playback Control
 * ========================================================================== */

ms_error_t ms_note_on(ms_instrument_t *instrument, uint8_t note, 
                      uint8_t velocity, uint32_t *voice_id) {
    if (!instrument || !instrument->sampler) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    ms_sample_data_t *sample = instrument_find_sample(instrument, note, velocity);
    if (!sample) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    ms_sampler_t *sampler = instrument->sampler;
    pthread_mutex_lock(&sampler->lock);
    
    /* Find an available voice */
    voice_t *available_voice = NULL;
    for (size_t i = 0; i < sampler->config.max_polyphony && i < MS_MAX_VOICES; i++) {
        if (!sampler->voices[i].active) {
            available_voice = &sampler->voices[i];
            break;
        }
    }
    
    /* If no voice available, steal the oldest one */
    if (!available_voice) {
        available_voice = &sampler->voices[0];
    }
    
    voice_trigger(available_voice, sample, note, velocity, &instrument->envelope);
    available_voice->instrument = instrument;
    
    if (voice_id) {
        *voice_id = available_voice->voice_id;
    }
    
    pthread_mutex_unlock(&sampler->lock);
    return MS_SUCCESS;
}

ms_error_t ms_note_off(ms_instrument_t *instrument, uint8_t note) {
    if (!instrument || !instrument->sampler) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    ms_sampler_t *sampler = instrument->sampler;
    pthread_mutex_lock(&sampler->lock);
    
    /* Release all voices playing this note on this instrument */
    for (size_t i = 0; i < sampler->config.max_polyphony && i < MS_MAX_VOICES; i++) {
        voice_t *voice = &sampler->voices[i];
        if (voice->active && voice->note == note && voice->instrument == instrument) {
            voice_release(voice);
        }
    }
    
    pthread_mutex_unlock(&sampler->lock);
    return MS_SUCCESS;
}

void ms_all_notes_off(ms_sampler_t *sampler) {
    if (!sampler) return;
    
    pthread_mutex_lock(&sampler->lock);
    
    for (size_t i = 0; i < sampler->config.max_polyphony && i < MS_MAX_VOICES; i++) {
        sampler->voices[i].active = false;
    }
    
    pthread_mutex_unlock(&sampler->lock);
}

ms_error_t ms_pitch_bend(ms_instrument_t *instrument, int16_t value) {
    if (!instrument) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    instrument->current_pitch_bend = value;
    
    /* Update all active voices for this instrument */
    if (instrument->sampler) {
        pthread_mutex_lock(&instrument->sampler->lock);
        
        for (size_t i = 0; i < MS_MAX_VOICES; i++) {
            voice_t *voice = &instrument->sampler->voices[i];
            if (voice->active && voice->instrument == instrument) {
                /* Convert pitch bend to multiplier */
                float semitones = (value / 8192.0f) * instrument->pitch_bend_range;
                voice->pitch_bend_multiplier = pow(2.0f, semitones / 12.0f);
            }
        }
        
        pthread_mutex_unlock(&instrument->sampler->lock);
    }
    
    return MS_SUCCESS;
}

/* ============================================================================
 * Audio Processing
 * ========================================================================== */

ms_error_t ms_process(ms_sampler_t *sampler, float *output, size_t num_frames) {
    if (!sampler || !output) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&sampler->lock);
    
    /* Clear output buffer */
    size_t buffer_size = num_frames * sampler->config.channels;
    memset(output, 0, buffer_size * sizeof(float));
    
    /* Process all active voices */
    for (size_t i = 0; i < sampler->config.max_polyphony && i < MS_MAX_VOICES; i++) {
        if (sampler->voices[i].active) {
            voice_process(&sampler->voices[i], output, num_frames, sampler->config.channels);
        }
    }
    
    /* Process MIDI playback if active */
    if (sampler->is_playing && sampler->current_track) {
        /* This would handle MIDI event processing - simplified for now */
    }
    
    sampler->playback_sample_count += num_frames;
    
    pthread_mutex_unlock(&sampler->lock);
    return MS_SUCCESS;
}

/* ============================================================================
 * MIDI File Support
 * ========================================================================== */

ms_error_t ms_load_midi_file(ms_sampler_t *sampler, ms_instrument_t *instrument,
                             const char *filepath) {
    if (!sampler || !instrument || !filepath) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&sampler->lock);
    
    if (sampler->current_track) {
        midi_track_destroy(sampler->current_track);
        free(sampler->current_track);
    }
    
    sampler->current_track = (midi_track_t*)calloc(1, sizeof(midi_track_t));
    if (!sampler->current_track) {
        pthread_mutex_unlock(&sampler->lock);
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    ms_error_t err = midi_parse_file(filepath, sampler->current_track);
    if (err != MS_SUCCESS) {
        free(sampler->current_track);
        sampler->current_track = NULL;
        pthread_mutex_unlock(&sampler->lock);
        return err;
    }
    
    sampler->playback_event_index = 0;
    sampler->playback_sample_count = 0;
    
    pthread_mutex_unlock(&sampler->lock);
    return MS_SUCCESS;
}

ms_error_t ms_start_playback(ms_sampler_t *sampler) {
    if (!sampler) return MS_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock(&sampler->lock);
    sampler->is_playing = true;
    sampler->playback_event_index = 0;
    sampler->playback_sample_count = 0;
    pthread_mutex_unlock(&sampler->lock);
    
    return MS_SUCCESS;
}

void ms_stop_playback(ms_sampler_t *sampler) {
    if (!sampler) return;
    
    pthread_mutex_lock(&sampler->lock);
    sampler->is_playing = false;
    pthread_mutex_unlock(&sampler->lock);
}

bool ms_is_playing(const ms_sampler_t *sampler) {
    return sampler && sampler->is_playing;
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
