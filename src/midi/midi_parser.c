/**
 * @file midi_parser.c
 * @brief MIDI file parsing
 */

#include "internal/internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read big-endian values */
static uint16_t read_be16(FILE *fp) {
    uint8_t bytes[2];
    fread(bytes, 1, 2, fp);
    return (bytes[0] << 8) | bytes[1];
}

static uint32_t read_be32(FILE *fp) {
    uint8_t bytes[4];
    fread(bytes, 1, 4, fp);
    return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}

static uint32_t read_variable_length(FILE *fp) {
    uint32_t value = 0;
    uint8_t byte;
    
    do {
        fread(&byte, 1, 1, fp);
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);
    
    return value;
}

static ms_error_t add_midi_event(midi_track_t *track, const midi_event_t *event) {
    if (track->num_events >= track->capacity) {
        size_t new_capacity = track->capacity == 0 ? 1024 : track->capacity * 2;
        midi_event_t *new_events = (midi_event_t*)realloc(
            track->events, 
            new_capacity * sizeof(midi_event_t)
        );
        
        if (!new_events) {
            return MS_ERROR_OUT_OF_MEMORY;
        }
        
        track->events = new_events;
        track->capacity = new_capacity;
    }
    
    track->events[track->num_events++] = *event;
    return MS_SUCCESS;
}

ms_error_t midi_parse_file(const char *filepath, midi_track_t *track) {
    if (!filepath || !track) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    memset(track, 0, sizeof(*track));
    
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        return MS_ERROR_FILE_NOT_FOUND;
    }
    
    /* Read header chunk */
    char header_id[4];
    fread(header_id, 1, 4, fp);
    
    if (memcmp(header_id, "MThd", 4) != 0) {
        fclose(fp);
        return MS_ERROR_INVALID_FORMAT;
    }
    
    uint32_t header_length = read_be32(fp);
    uint16_t format = read_be16(fp);
    uint16_t num_tracks = read_be16(fp);
    uint16_t division = read_be16(fp);
    
    track->ticks_per_beat = division & 0x7FFF;
    track->tempo = 500000; /* Default: 120 BPM */
    
    /* Skip any extra header data */
    if (header_length > 6) {
        fseek(fp, header_length - 6, SEEK_CUR);
    }
    
    /* Read first track */
    char track_id[4];
    fread(track_id, 1, 4, fp);
    
    if (memcmp(track_id, "MTrk", 4) != 0) {
        fclose(fp);
        return MS_ERROR_INVALID_FORMAT;
    }
    
    uint32_t track_length = read_be32(fp);
    long track_end = ftell(fp) + track_length;
    
    uint32_t current_time = 0;
    uint8_t running_status = 0;
    
    while (ftell(fp) < track_end) {
        uint32_t delta_time = read_variable_length(fp);
        current_time += delta_time;
        
        uint8_t status;
        fread(&status, 1, 1, fp);
        
        /* Handle running status */
        if (status < 0x80) {
            fseek(fp, -1, SEEK_CUR);
            status = running_status;
        } else {
            running_status = status;
        }
        
        uint8_t event_type = status & 0xF0;
        uint8_t channel = status & 0x0F;
        
        if (event_type == 0x90 || event_type == 0x80) {
            /* Note On / Note Off */
            uint8_t note, velocity;
            fread(&note, 1, 1, fp);
            fread(&velocity, 1, 1, fp);
            
            midi_event_t event = {
                .timestamp = current_time,
                .type = (event_type == 0x90 && velocity > 0) ? MIDI_NOTE_ON : MIDI_NOTE_OFF,
                .channel = channel,
                .data1 = note,
                .data2 = velocity
            };
            
            add_midi_event(track, &event);
            
        } else if (event_type == 0xE0) {
            /* Pitch Bend */
            uint8_t lsb, msb;
            fread(&lsb, 1, 1, fp);
            fread(&msb, 1, 1, fp);
            
            int16_t bend_value = ((msb << 7) | lsb) - 8192;
            
            midi_event_t event = {
                .timestamp = current_time,
                .type = MIDI_PITCH_BEND,
                .channel = channel,
                .data1 = bend_value & 0xFF,
                .data2 = (bend_value >> 8) & 0xFF
            };
            
            add_midi_event(track, &event);
            
        } else if (event_type == 0xB0) {
            /* Control Change */
            uint8_t controller, value;
            fread(&controller, 1, 1, fp);
            fread(&value, 1, 1, fp);
            
        } else if (event_type == 0xC0 || event_type == 0xD0) {
            /* Program Change / Channel Pressure - 1 data byte */
            uint8_t data;
            fread(&data, 1, 1, fp);
            
        } else if (event_type == 0xA0) {
            /* Polyphonic Key Pressure - 2 data bytes */
            uint8_t data1, data2;
            fread(&data1, 1, 1, fp);
            fread(&data2, 1, 1, fp);
            
        } else if (status == 0xFF) {
            /* Meta event */
            uint8_t meta_type;
            fread(&meta_type, 1, 1, fp);
            uint32_t length = read_variable_length(fp);
            
            if (meta_type == 0x51 && length == 3) {
                /* Set Tempo */
                uint8_t bytes[3];
                fread(bytes, 1, 3, fp);
                track->tempo = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
            } else {
                /* Skip other meta events */
                fseek(fp, length, SEEK_CUR);
            }
        } else if (status == 0xF0 || status == 0xF7) {
            /* SysEx event */
            uint32_t length = read_variable_length(fp);
            fseek(fp, length, SEEK_CUR);
        }
    }
    
    fclose(fp);
    return MS_SUCCESS;
}

void midi_track_destroy(midi_track_t *track) {
    if (track && track->events) {
        free(track->events);
        track->events = NULL;
        track->num_events = 0;
        track->capacity = 0;
    }
}
