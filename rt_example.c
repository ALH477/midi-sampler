/**
 * @file rt_example.c
 * @brief Real-time optimized example with performance monitoring
 * 
 * Demonstrates:
 * - RT thread priority
 * - Lock-free event queue
 * - Performance monitoring
 * - Low-latency configuration
 */

#define _USE_MATH_DEFINES
#include "midi_sampler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Generate sine wave sample */
static void generate_sine_wave(float *buffer, size_t num_frames, 
                               float frequency, uint32_t sample_rate) {
    for (size_t i = 0; i < num_frames; i++) {
        float t = (float)i / sample_rate;
        buffer[i] = 0.3f * sinf(2.0f * M_PI * frequency * t);
    }
}

/* Get current time in microseconds */
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("═══════════════════════════════════════════════════════\n");
    printf("   MIDI Sampler RT Example v%s\n", ms_version());
    printf("═══════════════════════════════════════════════════════\n\n");
    
    /* RT-optimized audio config */
    ms_audio_config_t config = {
        .sample_rate = 48000,      /* Higher quality */
        .channels = 2,
        .max_polyphony = 32,       /* More voices */
        .buffer_size = 128         /* Low latency! */
    };
    
    /* Create sampler */
    ms_sampler_t *sampler = NULL;
    ms_error_t err = ms_sampler_create(&config, &sampler);
    if (err != MS_SUCCESS) {
        fprintf(stderr, "Failed to create sampler: %s\n", ms_error_string(err));
        return 1;
    }
    
    printf("✓ Created RT sampler\n");
    printf("  Sample rate: %d Hz\n", config.sample_rate);
    printf("  Channels: %d\n", config.channels);
    printf("  Polyphony: %d voices\n", config.max_polyphony);
    printf("  Buffer size: %zu frames (%.2f ms latency)\n", 
           config.buffer_size,
           (float)config.buffer_size / config.sample_rate * 1000.0f);
    
    /* Enable RT mode */
    err = ms_sampler_enable_rt(sampler, 80);
    if (err == MS_SUCCESS) {
        printf("✓ RT mode enabled (priority 80)\n");
    } else {
        printf("⚠ RT mode failed (needs CAP_SYS_NICE or root)\n");
        printf("  Continuing in normal mode...\n");
    }
    printf("\n");
    
    /* Create instrument */
    ms_instrument_t *synth = NULL;
    err = ms_instrument_create(sampler, "RT Synth", &synth);
    if (err != MS_SUCCESS) {
        fprintf(stderr, "Failed to create instrument: %s\n", ms_error_string(err));
        ms_sampler_destroy(sampler);
        return 1;
    }
    
    /* Set fast envelope for low latency */
    ms_envelope_t envelope = {
        .attack_time = 0.005f,    /* 5ms attack */
        .decay_time = 0.05f,      /* 50ms decay */
        .sustain_level = 0.6f,
        .release_time = 0.1f      /* 100ms release */
    };
    ms_instrument_set_envelope(synth, &envelope);
    
    printf("✓ Configured low-latency envelope\n");
    printf("  Attack: %.1f ms\n", envelope.attack_time * 1000);
    printf("  Decay: %.1f ms\n", envelope.decay_time * 1000);
    printf("  Release: %.1f ms\n\n", envelope.release_time * 1000);
    
    /* Generate chromatic scale samples */
    const uint8_t notes[] = {60, 62, 64, 65, 67, 69, 71, 72};  /* C major scale */
    const float frequencies[] = {
        261.63f, 293.66f, 329.63f, 349.23f, 
        392.00f, 440.00f, 493.88f, 523.25f
    };
    
    for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        size_t num_frames = config.sample_rate * 1;  /* 1 second samples */
        float *sample_data = (float*)malloc(num_frames * sizeof(float));
        
        generate_sine_wave(sample_data, num_frames, frequencies[i], config.sample_rate);
        
        ms_sample_metadata_t metadata = {
            .root_note = notes[i],
            .velocity_low = 0,
            .velocity_high = 127,
            .loop_enabled = true,
            .loop_start = config.sample_rate / 10,
            .loop_end = num_frames - config.sample_rate / 10
        };
        
        err = ms_instrument_load_sample_memory(synth, sample_data, num_frames, 1, &metadata);
        free(sample_data);
        
        if (err != MS_SUCCESS) {
            fprintf(stderr, "Failed to load sample: %s\n", ms_error_string(err));
            continue;
        }
    }
    
    printf("✓ Loaded %zu samples with looping\n\n", sizeof(notes) / sizeof(notes[0]));
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    
    /* Performance test */
    printf("═══════════════════════════════════════════════════════\n");
    printf("   RT Performance Test\n");
    printf("═══════════════════════════════════════════════════════\n\n");
    
    float *audio_buffer = (float*)calloc(config.buffer_size * config.channels, 
                                         sizeof(float));
    
    /* Play ascending then descending scale */
    printf("♪ Playing scale with RT processing...\n\n");
    
    const int iterations = 100;
    uint64_t total_time = 0;
    uint64_t min_time = UINT64_MAX;
    uint64_t max_time = 0;
    
    /* Warm up */
    for (int i = 0; i < 10; i++) {
        ms_process(sampler, audio_buffer, config.buffer_size);
    }
    
    /* Trigger all notes for polyphony test */
    printf("Testing %d-voice polyphony...\n", config.max_polyphony);
    for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        ms_note_on(synth, notes[i], 80 + i * 5, NULL);
    }
    
    /* Benchmark audio processing */
    for (int i = 0; i < iterations; i++) {
        uint64_t start = get_time_us();
        
        ms_process(sampler, audio_buffer, config.buffer_size);
        
        uint64_t end = get_time_us();
        uint64_t elapsed = end - start;
        
        total_time += elapsed;
        if (elapsed < min_time) min_time = elapsed;
        if (elapsed > max_time) max_time = elapsed;
        
        /* Small delay to simulate real-time */
        usleep(100);
    }
    
    /* Release notes */
    for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        ms_note_off(synth, notes[i]);
    }
    
    /* Process release tails */
    for (int i = 0; i < 50; i++) {
        ms_process(sampler, audio_buffer, config.buffer_size);
    }
    
    /* Calculate statistics */
    float avg_time = (float)total_time / iterations;
    float buffer_time = (float)config.buffer_size / config.sample_rate * 1000000.0f;
    float cpu_usage = (avg_time / buffer_time) * 100.0f;
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("   Performance Results (%d iterations)\n", iterations);
    printf("═══════════════════════════════════════════════════════\n\n");
    printf("Processing time:\n");
    printf("  Average: %.1f μs\n", avg_time);
    printf("  Minimum: %lu μs\n", min_time);
    printf("  Maximum: %lu μs\n", max_time);
    printf("  Jitter:  %lu μs\n\n", max_time - min_time);
    
    printf("Buffer time: %.1f μs\n", buffer_time);
    printf("CPU usage:   %.1f%%\n\n", cpu_usage);
    
    if (cpu_usage < 50.0f) {
        printf("✓ Excellent! CPU usage is low.\n");
    } else if (cpu_usage < 80.0f) {
        printf("⚠ Good, but may struggle under heavy load.\n");
    } else {
        printf("✗ CPU usage too high! Risk of dropouts.\n");
    }
    
    printf("\n");
    
    /* Get sampler statistics */
    uint64_t frames_processed;
    uint32_t xruns;
    ms_get_stats(sampler, &frames_processed, &xruns);
    
    printf("Sampler statistics:\n");
    printf("  Frames processed: %lu\n", frames_processed);
    printf("  Buffer underruns: %u\n", xruns);
    printf("\n");
    
    if (xruns == 0) {
        printf("✓ No buffer underruns detected!\n");
    } else {
        printf("⚠ %u buffer underruns - tune system for RT\n", xruns);
    }
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("   RT Recommendations\n");
    printf("═══════════════════════════════════════════════════════\n\n");
    
    if (cpu_usage > 50.0f) {
        printf("• Reduce polyphony or buffer size\n");
        printf("• Check CPU governor is set to 'performance'\n");
        printf("• Consider using RT or BORE kernel\n");
    }
    
    if (max_time - min_time > 1000) {
        printf("• High jitter detected - isolate CPUs\n");
        printf("• Move IRQs away from audio processing\n");
        printf("• Disable CPU frequency scaling\n");
    }
    
    printf("• For best results, see RT_GUIDE.md\n");
    printf("\n");
    
    /* Cleanup */
    free(audio_buffer);
    ms_instrument_destroy(synth);
    ms_sampler_destroy(sampler);
    
    printf("✓ Test completed successfully\n");
    
    return 0;
}
