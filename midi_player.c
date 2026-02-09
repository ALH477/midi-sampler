/**
 * @file midi_player.c
 * @brief Example MIDI file player
 */

#include "midi_sampler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("\nOptions:\n");
    printf("  -s <file>    Sample WAV file to load\n");
    printf("  -m <file>    MIDI file to play\n");
    printf("  -n <note>    Root note of the sample (default: 60/C4)\n");
    printf("  -h           Show this help message\n");
    printf("\nExample:\n");
    printf("  %s -s piano_c4.wav -n 60 -m song.mid\n", prog_name);
}

int main(int argc, char **argv) {
    const char *sample_file = NULL;
    const char *midi_file = NULL;
    uint8_t root_note = 60;
    
    /* Parse command line arguments */
    int opt;
    while ((opt = getopt(argc, argv, "s:m:n:h")) != -1) {
        switch (opt) {
            case 's':
                sample_file = optarg;
                break;
            case 'm':
                midi_file = optarg;
                break;
            case 'n':
                root_note = (uint8_t)atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (!sample_file || !midi_file) {
        fprintf(stderr, "Error: Sample file and MIDI file are required\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    printf("MIDI Sampler Library v%s\n", ms_version());
    printf("MIDI Player Example\n\n");
    
    /* Configure audio */
    ms_audio_config_t config = {
        .sample_rate = 44100,
        .channels = 2,
        .max_polyphony = 32,
        .buffer_size = 512
    };
    
    /* Create sampler */
    ms_sampler_t *sampler = NULL;
    ms_error_t err = ms_sampler_create(&config, &sampler);
    if (err != MS_SUCCESS) {
        fprintf(stderr, "Failed to create sampler: %s\n", ms_error_string(err));
        return 1;
    }
    
    printf("✓ Created sampler\n");
    
    /* Create instrument */
    ms_instrument_t *instrument = NULL;
    err = ms_instrument_create(sampler, "MIDI Instrument", &instrument);
    if (err != MS_SUCCESS) {
        fprintf(stderr, "Failed to create instrument: %s\n", ms_error_string(err));
        ms_sampler_destroy(sampler);
        return 1;
    }
    
    /* Configure envelope for piano-like sound */
    ms_envelope_t envelope = {
        .attack_time = 0.005f,
        .decay_time = 0.2f,
        .sustain_level = 0.5f,
        .release_time = 0.8f
    };
    ms_instrument_set_envelope(instrument, &envelope);
    
    /* Load sample */
    printf("Loading sample: %s (root note: %d)\n", sample_file, root_note);
    
    ms_sample_metadata_t metadata = {
        .root_note = root_note,
        .velocity_low = 0,
        .velocity_high = 127,
        .loop_enabled = false,
        .loop_start = 0,
        .loop_end = 0
    };
    
    err = ms_instrument_load_sample(instrument, sample_file, &metadata);
    if (err != MS_SUCCESS) {
        fprintf(stderr, "Failed to load sample: %s\n", ms_error_string(err));
        ms_instrument_destroy(instrument);
        ms_sampler_destroy(sampler);
        return 1;
    }
    
    printf("✓ Loaded sample\n");
    
    /* Load MIDI file */
    printf("Loading MIDI: %s\n", midi_file);
    
    err = ms_load_midi_file(sampler, instrument, midi_file);
    if (err != MS_SUCCESS) {
        fprintf(stderr, "Failed to load MIDI file: %s\n", ms_error_string(err));
        ms_instrument_destroy(instrument);
        ms_sampler_destroy(sampler);
        return 1;
    }
    
    printf("✓ Loaded MIDI file\n");
    
    /* Start playback */
    printf("\n♪ Playing MIDI file...\n");
    printf("(This is a demonstration - audio would be sent to output device)\n\n");
    
    err = ms_start_playback(sampler);
    if (err != MS_SUCCESS) {
        fprintf(stderr, "Failed to start playback: %s\n", ms_error_string(err));
        ms_instrument_destroy(instrument);
        ms_sampler_destroy(sampler);
        return 1;
    }
    
    /* Simulate playback (in real application, this would be in audio callback) */
    float *audio_buffer = (float*)calloc(config.buffer_size * config.channels, 
                                         sizeof(float));
    
    int frame_count = 0;
    int max_frames = config.sample_rate * 30; /* Max 30 seconds */
    
    while (ms_is_playing(sampler) && frame_count < max_frames) {
        ms_process(sampler, audio_buffer, config.buffer_size);
        frame_count += config.buffer_size;
        
        /* Print progress every second */
        if (frame_count % config.sample_rate == 0) {
            printf("  %d seconds...\n", frame_count / config.sample_rate);
        }
    }
    
    free(audio_buffer);
    
    printf("\n✓ Playback completed\n");
    
    /* Cleanup */
    ms_instrument_destroy(instrument);
    ms_sampler_destroy(sampler);
    
    printf("✓ Cleaned up resources\n");
    
    return 0;
}
