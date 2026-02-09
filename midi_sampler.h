/**
 * @file midi_sampler.h
 * @brief High-quality MIDI instrument sampler library
 * @author DeMoD LLC
 * @license MIT
 * 
 * A professional MIDI sampler with support for polyphony, velocity layers,
 * ADSR envelopes, and high-quality sample rate conversion.
 */

#ifndef MIDI_SAMPLER_H
#define MIDI_SAMPLER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Codes
 * ========================================================================== */

typedef enum {
    MS_SUCCESS = 0,
    MS_ERROR_INVALID_PARAM = -1,
    MS_ERROR_OUT_OF_MEMORY = -2,
    MS_ERROR_FILE_NOT_FOUND = -3,
    MS_ERROR_INVALID_FORMAT = -4,
    MS_ERROR_BUFFER_OVERFLOW = -5,
    MS_ERROR_NOT_INITIALIZED = -6,
    MS_ERROR_VOICE_LIMIT = -7,
    MS_ERROR_UNKNOWN = -99
} ms_error_t;

/* ============================================================================
 * Opaque Types
 * ========================================================================== */

/** Opaque handle to a sampler instance */
typedef struct ms_sampler_t ms_sampler_t;

/** Opaque handle to a sample instrument */
typedef struct ms_instrument_t ms_instrument_t;

/* ============================================================================
 * Configuration Structures
 * ========================================================================== */

/**
 * @brief Audio configuration for the sampler
 */
typedef struct {
    uint32_t sample_rate;      /**< Sample rate in Hz (e.g., 44100, 48000) */
    uint16_t channels;         /**< Number of audio channels (1=mono, 2=stereo) */
    uint16_t max_polyphony;    /**< Maximum simultaneous voices */
    size_t buffer_size;        /**< Audio buffer size in frames */
} ms_audio_config_t;

/**
 * @brief ADSR envelope parameters
 */
typedef struct {
    float attack_time;     /**< Attack time in seconds */
    float decay_time;      /**< Decay time in seconds */
    float sustain_level;   /**< Sustain level (0.0 to 1.0) */
    float release_time;    /**< Release time in seconds */
} ms_envelope_t;

/**
 * @brief Sample metadata
 */
typedef struct {
    uint8_t root_note;         /**< MIDI note number of the sample */
    uint8_t velocity_low;      /**< Minimum velocity for this sample */
    uint8_t velocity_high;     /**< Maximum velocity for this sample */
    bool loop_enabled;         /**< Whether the sample should loop */
    uint32_t loop_start;       /**< Loop start point in samples */
    uint32_t loop_end;         /**< Loop end point in samples */
} ms_sample_metadata_t;

/* ============================================================================
 * Sampler Lifecycle
 * ========================================================================== */

/**
 * @brief Create a new sampler instance
 * 
 * @param config Audio configuration parameters
 * @param sampler Output pointer for the created sampler
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_sampler_create(
    const ms_audio_config_t *config,
    ms_sampler_t **sampler
);

/**
 * @brief Destroy a sampler instance and free all resources
 * 
 * @param sampler Sampler instance to destroy
 */
void ms_sampler_destroy(ms_sampler_t *sampler);

/* ============================================================================
 * Instrument Management
 * ========================================================================== */

/**
 * @brief Create a new instrument
 * 
 * @param sampler Sampler instance
 * @param name Instrument name (optional, can be NULL)
 * @param instrument Output pointer for the created instrument
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_instrument_create(
    ms_sampler_t *sampler,
    const char *name,
    ms_instrument_t **instrument
);

/**
 * @brief Load a WAV sample into an instrument
 * 
 * @param instrument Target instrument
 * @param filepath Path to WAV file
 * @param metadata Sample metadata (root note, velocity range, etc.)
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_instrument_load_sample(
    ms_instrument_t *instrument,
    const char *filepath,
    const ms_sample_metadata_t *metadata
);

/**
 * @brief Load a sample from memory
 * 
 * @param instrument Target instrument
 * @param data PCM audio data (interleaved if stereo)
 * @param num_frames Number of audio frames
 * @param channels Number of channels in the data
 * @param metadata Sample metadata
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_instrument_load_sample_memory(
    ms_instrument_t *instrument,
    const float *data,
    size_t num_frames,
    uint16_t channels,
    const ms_sample_metadata_t *metadata
);

/**
 * @brief Set the envelope for an instrument
 * 
 * @param instrument Target instrument
 * @param envelope ADSR envelope parameters
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_instrument_set_envelope(
    ms_instrument_t *instrument,
    const ms_envelope_t *envelope
);

/**
 * @brief Destroy an instrument and free its resources
 * 
 * @param instrument Instrument to destroy
 */
void ms_instrument_destroy(ms_instrument_t *instrument);

/* ============================================================================
 * Playback Control
 * ========================================================================== */

/**
 * @brief Trigger a note on an instrument
 * 
 * @param instrument Instrument to play
 * @param note MIDI note number (0-127)
 * @param velocity MIDI velocity (0-127)
 * @param voice_id Output pointer for voice ID (optional, can be NULL)
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_note_on(
    ms_instrument_t *instrument,
    uint8_t note,
    uint8_t velocity,
    uint32_t *voice_id
);

/**
 * @brief Release a note
 * 
 * @param instrument Instrument playing the note
 * @param note MIDI note number to release
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_note_off(
    ms_instrument_t *instrument,
    uint8_t note
);

/**
 * @brief Stop all playing notes immediately
 * 
 * @param sampler Sampler instance
 */
void ms_all_notes_off(ms_sampler_t *sampler);

/**
 * @brief Apply pitch bend to an instrument
 * 
 * @param instrument Target instrument
 * @param value Pitch bend value (-8192 to +8191, 0 = no bend)
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_pitch_bend(
    ms_instrument_t *instrument,
    int16_t value
);

/* ============================================================================
 * Audio Processing
 * ========================================================================== */

/**
 * @brief Process audio and fill output buffer
 * 
 * @param sampler Sampler instance
 * @param output Output buffer (interleaved if stereo)
 * @param num_frames Number of frames to generate
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_process(
    ms_sampler_t *sampler,
    float *output,
    size_t num_frames
);

/* ============================================================================
 * MIDI File Support
 * ========================================================================== */

/**
 * @brief Load and play a MIDI file
 * 
 * @param sampler Sampler instance
 * @param instrument Instrument to use for playback
 * @param filepath Path to MIDI file
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_load_midi_file(
    ms_sampler_t *sampler,
    ms_instrument_t *instrument,
    const char *filepath
);

/**
 * @brief Start MIDI file playback
 * 
 * @param sampler Sampler instance
 * @return MS_SUCCESS on success, error code otherwise
 */
ms_error_t ms_start_playback(ms_sampler_t *sampler);

/**
 * @brief Stop MIDI file playback
 * 
 * @param sampler Sampler instance
 */
void ms_stop_playback(ms_sampler_t *sampler);

/**
 * @brief Check if MIDI is currently playing
 * 
 * @param sampler Sampler instance
 * @return true if playing, false otherwise
 */
bool ms_is_playing(const ms_sampler_t *sampler);

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * @brief Get error message for an error code
 * 
 * @param error Error code
 * @return Human-readable error message
 */
const char* ms_error_string(ms_error_t error);

/**
 * @brief Get library version string
 * 
 * @return Version string (e.g., "1.0.0")
 */
const char* ms_version(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_SAMPLER_H */
