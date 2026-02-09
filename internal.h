/**
 * @file internal.h
 * @brief Internal structures and definitions
 * @internal
 */

#ifndef MIDI_SAMPLER_INTERNAL_H
#define MIDI_SAMPLER_INTERNAL_H

#include "midi_sampler.h"
#include <pthread.h>

/* ============================================================================
 * Constants
 * ========================================================================== */

#define MS_MAX_SAMPLES_PER_INSTRUMENT 128
#define MS_MAX_VOICES 64
#define MS_VERSION "1.0.0"

/* ============================================================================
 * Sample Structure
 * ========================================================================== */

typedef struct {
    float *data;                /**< PCM data (interleaved if stereo) */
    size_t num_frames;          /**< Number of audio frames */
    uint16_t channels;          /**< Number of channels */
    ms_sample_metadata_t meta;  /**< Sample metadata */
} ms_sample_data_t;

/* ============================================================================
 * Envelope Generator
 * ========================================================================== */

typedef enum {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} envelope_stage_t;

typedef struct {
    envelope_stage_t stage;
    float current_level;
    float sample_rate;
    ms_envelope_t params;
    uint32_t stage_samples;
    uint32_t samples_processed;
} envelope_generator_t;

void envelope_init(envelope_generator_t *env, float sample_rate, const ms_envelope_t *params);
void envelope_trigger(envelope_generator_t *env);
void envelope_release(envelope_generator_t *env);
float envelope_process(envelope_generator_t *env);
bool envelope_is_active(const envelope_generator_t *env);

/* ============================================================================
 * Voice
 * ========================================================================== */

typedef struct {
    bool active;
    uint32_t voice_id;
    uint8_t note;
    uint8_t velocity;
    
    ms_sample_data_t *sample;
    double playback_position;
    double playback_speed;
    
    envelope_generator_t envelope;
    float pitch_bend_multiplier;
    
    struct ms_instrument_t *instrument;
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
 * Sampler
 * ========================================================================== */

struct ms_sampler_t {
    ms_audio_config_t config;
    voice_t voices[MS_MAX_VOICES];
    uint32_t next_voice_id;
    
    midi_track_t *current_track;
    size_t playback_event_index;
    uint64_t playback_sample_count;
    bool is_playing;
    
    pthread_mutex_t lock;
};

#endif /* MIDI_SAMPLER_INTERNAL_H */
