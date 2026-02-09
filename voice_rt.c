/**
 * @file voice_rt.c
 * @brief Real-time optimized voice playback
 * 
 * Optimizations:
 * - Inlined MIDI to frequency conversion
 * - Pre-calculated velocity gain
 * - SIMD-friendly loop structure
 * - Prefetching for sample data
 * - Branch prediction hints
 */

#include "internal_rt.h"
#include <math.h>
#include <string.h>

/* Pre-calculated MIDI note to frequency table (avoids pow() in RT path) */
static const double MIDI_FREQ_TABLE[128] = {
    8.176, 8.662, 9.177, 9.723, 10.301, 10.913, 11.562, 12.250,
    12.978, 13.750, 14.568, 15.434, 16.352, 17.324, 18.354, 19.445,
    20.602, 21.827, 23.125, 24.500, 25.957, 27.500, 29.135, 30.868,
    32.703, 34.648, 36.708, 38.891, 41.203, 43.654, 46.249, 48.999,
    51.913, 55.000, 58.270, 61.735, 65.406, 69.296, 73.416, 77.782,
    82.407, 87.307, 92.499, 97.999, 103.826, 110.000, 116.541, 123.471,
    130.813, 138.591, 146.832, 155.563, 164.814, 174.614, 184.997, 195.998,
    207.652, 220.000, 233.082, 246.942, 261.626, 277.183, 293.665, 311.127,
    329.628, 349.228, 369.994, 391.995, 415.305, 440.000, 466.164, 493.883,
    523.251, 554.365, 587.330, 622.254, 659.255, 698.456, 739.989, 783.991,
    830.609, 880.000, 932.328, 987.767, 1046.502, 1108.731, 1174.659, 1244.508,
    1318.510, 1396.913, 1479.978, 1567.982, 1661.219, 1760.000, 1864.655, 1975.533,
    2093.005, 2217.461, 2349.318, 2489.016, 2637.020, 2793.826, 2959.955, 3135.963,
    3322.438, 3520.000, 3729.310, 3951.066, 4186.009, 4434.922, 4698.636, 4978.032,
    5274.041, 5587.652, 5919.911, 6271.927, 6644.875, 7040.000, 7458.620, 7902.133,
    8372.018, 8869.844, 9397.273, 9956.063, 10548.082, 11175.303, 11839.822, 12543.854
};

static FORCE_INLINE double midi_note_to_frequency(uint8_t note) {
    return MIDI_FREQ_TABLE[note & 0x7F];
}

void voice_init(voice_t *voice, uint32_t voice_id, float sample_rate) {
    if (UNLIKELY(!voice)) return;
    
    memset(voice, 0, sizeof(*voice));
    voice->voice_id = voice_id;
    voice->pitch_bend_multiplier = 1.0f;
}

void voice_trigger(voice_t *voice, ms_sample_data_t *sample, uint8_t note, 
                   uint8_t velocity, const ms_envelope_t *envelope) {
    if (UNLIKELY(!voice || !sample || !envelope)) return;
    
    voice->active = true;
    voice->note = note;
    voice->velocity = velocity;
    voice->sample = sample;
    voice->playback_position = 0.0;
    
    /* Pre-calculate velocity gain (avoid division in RT path) */
    voice->velocity_gain = velocity * (1.0f / 127.0f);
    
    /* Calculate playback speed using lookup table */
    double target_freq = midi_note_to_frequency(note);
    double sample_freq = midi_note_to_frequency(sample->meta.root_note);
    voice->playback_speed = (target_freq / sample_freq) * voice->pitch_bend_multiplier;
    
    /* Initialize envelope with pre-calculated coefficients */
    envelope_init(&voice->envelope, 44100.0f, envelope);
    envelope_trigger(&voice->envelope);
}

void voice_release(voice_t *voice) {
    if (UNLIKELY(!voice)) return;
    envelope_release(&voice->envelope);
}

/**
 * @brief RT-safe voice processing with optimizations
 * 
 * Optimizations:
 * - Loop unrolling friendly structure
 * - Prefetching for sample data
 * - Minimal branching in hot loop
 * - SIMD-friendly memory access patterns
 */
void voice_process(voice_t *voice, float *output, size_t num_frames, uint16_t channels) {
    if (UNLIKELY(!voice || !voice->active || !voice->sample || !output)) return;
    
    ms_sample_data_t *sample = voice->sample;
    double position = voice->playback_position;
    const double speed = voice->playback_speed;
    const float velocity_gain = voice->velocity_gain;
    const bool is_mono = (sample->channels == 1);
    const bool is_stereo_out = (channels == 2);
    
    /* Prefetch first sample data */
    PREFETCH_READ(sample->data);
    
    /* Loop parameters */
    const bool loop_enabled = sample->meta.loop_enabled;
    const uint32_t loop_start = sample->meta.loop_start;
    const uint32_t loop_end = sample->meta.loop_end;
    const size_t max_frames = sample->num_frames;
    
    for (size_t i = 0; i < num_frames; i++) {
        /* Check bounds */
        if (UNLIKELY(position >= max_frames)) {
            if (loop_enabled && loop_end > loop_start) {
                position = loop_start;
            } else {
                voice->active = false;
                break;
            }
        }
        
        /* Get interpolation parameters */
        const size_t index = (size_t)position;
        const float frac = (float)(position - index);
        
        /* Prefetch next cache line */
        if (LIKELY((i & 15) == 0)) {
            PREFETCH_READ(&sample->data[index + 64]);
        }
        
        /* Linear interpolation */
        float sample_value;
        if (is_mono) {
            const float s0 = sample->data[index];
            const float s1 = (index + 1 < max_frames) ? sample->data[index + 1] : s0;
            sample_value = s0 + frac * (s1 - s0);
        } else {
            /* Stereo sample - use left channel */
            const float s0 = sample->data[index * 2];
            const float s1 = (index + 1 < max_frames) ? sample->data[(index + 1) * 2] : s0;
            sample_value = s0 + frac * (s1 - s0);
        }
        
        /* Apply envelope (inlined for performance) */
        const float env_level = envelope_process(&voice->envelope);
        const float final_value = sample_value * env_level * velocity_gain;
        
        /* Mix into output */
        if (is_stereo_out) {
            output[i * 2] += final_value;
            output[i * 2 + 1] += final_value;
        } else {
            output[i] += final_value;
        }
        
        /* Advance position */
        position += speed;
        
        /* Check if envelope finished (less common, put at end) */
        if (UNLIKELY(!envelope_is_active(&voice->envelope))) {
            voice->active = false;
            break;
        }
    }
    
    voice->playback_position = position;
}

bool voice_is_active(const voice_t *voice) {
    return voice && voice->active;
}
