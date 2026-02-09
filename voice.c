/**
 * @file voice.c
 * @brief Voice playback and management
 */

#include "internal.h"
#include <math.h>
#include <string.h>

/* MIDI note to frequency conversion */
static double midi_note_to_frequency(uint8_t note) {
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

void voice_init(voice_t *voice, uint32_t voice_id, float sample_rate) {
    if (!voice) return;
    
    memset(voice, 0, sizeof(*voice));
    voice->voice_id = voice_id;
    voice->pitch_bend_multiplier = 1.0f;
}

void voice_trigger(voice_t *voice, ms_sample_data_t *sample, uint8_t note, 
                   uint8_t velocity, const ms_envelope_t *envelope) {
    if (!voice || !sample || !envelope) return;
    
    voice->active = true;
    voice->note = note;
    voice->velocity = velocity;
    voice->sample = sample;
    voice->playback_position = 0.0;
    
    /* Calculate playback speed based on note difference */
    double target_freq = midi_note_to_frequency(note);
    double sample_freq = midi_note_to_frequency(sample->meta.root_note);
    voice->playback_speed = (target_freq / sample_freq) * voice->pitch_bend_multiplier;
    
    /* Initialize envelope */
    envelope_init(&voice->envelope, 44100.0f, envelope);
    envelope_trigger(&voice->envelope);
}

void voice_release(voice_t *voice) {
    if (!voice) return;
    envelope_release(&voice->envelope);
}

void voice_process(voice_t *voice, float *output, size_t num_frames, uint16_t channels) {
    if (!voice || !voice->active || !voice->sample || !output) return;
    
    ms_sample_data_t *sample = voice->sample;
    double position = voice->playback_position;
    double speed = voice->playback_speed;
    
    /* Velocity scaling (0-127 -> 0.0-1.0) */
    float velocity_gain = voice->velocity / 127.0f;
    
    for (size_t i = 0; i < num_frames; i++) {
        /* Check if we've reached the end of the sample */
        if (position >= sample->num_frames) {
            if (sample->meta.loop_enabled && 
                sample->meta.loop_end > sample->meta.loop_start) {
                /* Loop back to loop start */
                position = sample->meta.loop_start;
            } else {
                /* Sample finished */
                voice->active = false;
                break;
            }
        }
        
        /* Linear interpolation between samples */
        size_t index = (size_t)position;
        float frac = (float)(position - index);
        
        float sample_value;
        if (sample->channels == 1) {
            /* Mono sample */
            float s0 = sample->data[index];
            float s1 = (index + 1 < sample->num_frames) ? sample->data[index + 1] : s0;
            sample_value = s0 + frac * (s1 - s0);
        } else {
            /* Stereo sample - just use left channel for now */
            float s0 = sample->data[index * 2];
            float s1 = (index + 1 < sample->num_frames) ? sample->data[(index + 1) * 2] : s0;
            sample_value = s0 + frac * (s1 - s0);
        }
        
        /* Apply envelope */
        float env_level = envelope_process(&voice->envelope);
        float final_value = sample_value * env_level * velocity_gain;
        
        /* Mix into output buffer */
        if (channels == 1) {
            output[i] += final_value;
        } else {
            /* Stereo output */
            output[i * 2] += final_value;
            output[i * 2 + 1] += final_value;
        }
        
        /* Advance playback position */
        position += speed;
        
        /* Check if envelope has finished */
        if (!envelope_is_active(&voice->envelope)) {
            voice->active = false;
            break;
        }
    }
    
    voice->playback_position = position;
}

bool voice_is_active(const voice_t *voice) {
    return voice && voice->active;
}
