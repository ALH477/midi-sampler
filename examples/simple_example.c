/**
 * @file simple_example.c
 * @brief Simple example demonstrating basic MIDI sampler usage
 */

#define _USE_MATH_DEFINES
#include "midi_sampler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Generate a simple sine wave for demonstration */
static void generate_sine_wave(float *buffer, size_t num_frames, 
                               float frequency, uint32_t sample_rate) {
    for (size_t i = 0; i < num_frames; i++) {
        float t = (float)i / sample_rate;
        buffer[i] = 0.3f * sinf(2.0f * M_PI * frequency * t);
    }
}

int main(int argc, char **argv) {
    printf("MIDI Sampler Library v%s\n", ms_version());
    printf("Simple Example\n\n");
    
    /* Configure audio */
    ms_audio_config_t config = {
        .sample_rate = 44100,
        .channels = 2,
        .max_polyphony = 16,
        .buffer_size = 512
    };
    
    /* Create sampler */
    ms_sampler_t *sampler = NULL;
    ms_error_t err = ms_sampler_create(&config, &sampler);
    if (err != MS_SUCCESS) {
        fprintf(stderr, "Failed to create sampler: %s\n", ms_error_string(err));
        return 1;
    }
    
    printf("✓ Created sampler (sample rate: %d Hz, polyphony: %d)\n", 
           config.sample_rate, config.max_polyphony);
    
    /* Create instrument */
    ms_instrument_t *piano = NULL;
    err = ms_instrument_create(sampler, "Demo Piano", &piano);
    if (err != MS_SUCCESS) {
        fprintf(stderr, "Failed to create instrument: %s\n", ms_error_string(err));
        ms_sampler_destroy(sampler);
        return 1;
    }
    
    printf("✓ Created instrument\n");
    
    /* Set envelope */
    ms_envelope_t envelope = {
        .attack_time = 0.02f,
        .decay_time = 0.1f,
        .sustain_level = 0.6f,
        .release_time = 0.5f
    };
    ms_instrument_set_envelope(piano, &envelope);
    
    printf("✓ Configured envelope (A:%.2fs D:%.2fs S:%.1f R:%.2fs)\n",
           envelope.attack_time, envelope.decay_time, 
           envelope.sustain_level, envelope.release_time);
    
    /* Generate and load some synthetic samples */
    const uint8_t notes[] = {60, 64, 67, 72}; /* C4, E4, G4, C5 */
    const float frequencies[] = {261.63f, 329.63f, 392.00f, 523.25f};
    
    for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        size_t num_frames = config.sample_rate * 2; /* 2 seconds */
        float *sample_data = (float*)malloc(num_frames * sizeof(float));
        
        generate_sine_wave(sample_data, num_frames, frequencies[i], config.sample_rate);
        
        ms_sample_metadata_t metadata = {
            .root_note = notes[i],
            .velocity_low = 0,
            .velocity_high = 127,
            .loop_enabled = true,
            .loop_start = config.sample_rate / 4,
            .loop_end = num_frames - config.sample_rate / 4
        };
        
        err = ms_instrument_load_sample_memory(piano, sample_data, num_frames, 
                                              1, &metadata);
        free(sample_data);
        
        if (err != MS_SUCCESS) {
            fprintf(stderr, "Failed to load sample: %s\n", ms_error_string(err));
            continue;
        }
    }
    
    printf("✓ Loaded %zu samples\n", sizeof(notes) / sizeof(notes[0]));
    
    /* Play a simple melody */
    printf("\n♪ Playing melody...\n");
    
    const struct {
        uint8_t note;
        float duration;
    } melody[] = {
        {60, 0.5f}, {64, 0.5f}, {67, 0.5f}, {72, 1.0f},
        {67, 0.5f}, {64, 0.5f}, {60, 1.0f}
    };
    
    float *audio_buffer = (float*)calloc(config.buffer_size * config.channels, 
                                         sizeof(float));
    
    for (size_t i = 0; i < sizeof(melody) / sizeof(melody[0]); i++) {
        printf("  Note: %d (%.1fs)\n", melody[i].note, melody[i].duration);
        
        /* Trigger note */
        ms_note_on(piano, melody[i].note, 80, NULL);
        
        /* Render audio for note duration */
        size_t total_frames = (size_t)(melody[i].duration * config.sample_rate);
        size_t frames_rendered = 0;
        
        while (frames_rendered < total_frames) {
            size_t frames_to_render = config.buffer_size;
            if (frames_rendered + frames_to_render > total_frames) {
                frames_to_render = total_frames - frames_rendered;
            }
            
            ms_process(sampler, audio_buffer, frames_to_render);
            frames_rendered += frames_to_render;
        }
        
        /* Release note */
        ms_note_off(piano, melody[i].note);
        
        /* Process release tail */
        for (int j = 0; j < 20; j++) {
            ms_process(sampler, audio_buffer, config.buffer_size);
        }
    }
    
    free(audio_buffer);
    
    printf("\n✓ Example completed successfully\n");
    
    /* Cleanup */
    ms_instrument_destroy(piano);
    ms_sampler_destroy(sampler);
    
    printf("✓ Cleaned up resources\n");
    
    return 0;
}
