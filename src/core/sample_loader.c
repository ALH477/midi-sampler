/**
 * @file sample_loader.c
 * @brief WAV file loading and sample management
 */

#include "internal/internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WAV file header structures */
typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
} wav_header_t;

typedef struct {
    char id[4];
    uint32_t size;
} chunk_header_t;

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_chunk_t;

static ms_error_t read_wav_header(FILE *fp, uint32_t *sample_rate, 
                                  uint16_t *channels, uint16_t *bits_per_sample,
                                  uint32_t *data_size) {
    wav_header_t header;
    
    /* Read RIFF header */
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        return MS_ERROR_INVALID_FORMAT;
    }
    
    if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0) {
        return MS_ERROR_INVALID_FORMAT;
    }
    
    /* Find fmt chunk */
    chunk_header_t chunk;
    wav_fmt_chunk_t fmt = {0};
    bool found_fmt = false;
    
    while (fread(&chunk, sizeof(chunk), 1, fp) == 1) {
        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            if (fread(&fmt, sizeof(fmt), 1, fp) != 1) {
                return MS_ERROR_INVALID_FORMAT;
            }
            found_fmt = true;
            
            /* Skip any extra fmt data */
            if (chunk.size > sizeof(fmt)) {
                fseek(fp, chunk.size - sizeof(fmt), SEEK_CUR);
            }
            break;
        } else {
            fseek(fp, chunk.size, SEEK_CUR);
        }
    }
    
    if (!found_fmt) {
        return MS_ERROR_INVALID_FORMAT;
    }
    
    /* Only support PCM format */
    if (fmt.audio_format != 1) {
        return MS_ERROR_INVALID_FORMAT;
    }
    
    *sample_rate = fmt.sample_rate;
    *channels = fmt.num_channels;
    *bits_per_sample = fmt.bits_per_sample;
    
    /* Find data chunk */
    while (fread(&chunk, sizeof(chunk), 1, fp) == 1) {
        if (memcmp(chunk.id, "data", 4) == 0) {
            *data_size = chunk.size;
            return MS_SUCCESS;
        } else {
            fseek(fp, chunk.size, SEEK_CUR);
        }
    }
    
    return MS_ERROR_INVALID_FORMAT;
}

ms_error_t load_wav_file(const char *filepath, ms_sample_data_t *sample) {
    if (!filepath || !sample) {
        return MS_ERROR_INVALID_PARAM;
    }
    
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        return MS_ERROR_FILE_NOT_FOUND;
    }
    
    uint32_t sample_rate, data_size;
    uint16_t channels, bits_per_sample;
    
    ms_error_t err = read_wav_header(fp, &sample_rate, &channels, 
                                     &bits_per_sample, &data_size);
    if (err != MS_SUCCESS) {
        fclose(fp);
        return err;
    }
    
    /* Calculate number of frames */
    size_t bytes_per_sample = bits_per_sample / 8;
    size_t num_frames = data_size / (channels * bytes_per_sample);
    
    /* Allocate buffer for float data */
    sample->data = (float*)malloc(num_frames * channels * sizeof(float));
    if (!sample->data) {
        fclose(fp);
        return MS_ERROR_OUT_OF_MEMORY;
    }
    
    sample->num_frames = num_frames;
    sample->channels = channels;
    
    /* Read and convert audio data */
    if (bits_per_sample == 16) {
        int16_t *temp_buffer = (int16_t*)malloc(data_size);
        if (!temp_buffer) {
            free(sample->data);
            fclose(fp);
            return MS_ERROR_OUT_OF_MEMORY;
        }
        
        if (fread(temp_buffer, 1, data_size, fp) != data_size) {
            free(temp_buffer);
            free(sample->data);
            fclose(fp);
            return MS_ERROR_INVALID_FORMAT;
        }
        
        /* Convert 16-bit PCM to float [-1.0, 1.0] */
        for (size_t i = 0; i < num_frames * channels; i++) {
            sample->data[i] = temp_buffer[i] / 32768.0f;
        }
        
        free(temp_buffer);
    } else if (bits_per_sample == 8) {
        uint8_t *temp_buffer = (uint8_t*)malloc(data_size);
        if (!temp_buffer) {
            free(sample->data);
            fclose(fp);
            return MS_ERROR_OUT_OF_MEMORY;
        }
        
        if (fread(temp_buffer, 1, data_size, fp) != data_size) {
            free(temp_buffer);
            free(sample->data);
            fclose(fp);
            return MS_ERROR_INVALID_FORMAT;
        }
        
        /* Convert 8-bit PCM to float [-1.0, 1.0] */
        for (size_t i = 0; i < num_frames * channels; i++) {
            sample->data[i] = (temp_buffer[i] - 128) / 128.0f;
        }
        
        free(temp_buffer);
    } else {
        free(sample->data);
        fclose(fp);
        return MS_ERROR_INVALID_FORMAT;
    }
    
    fclose(fp);
    return MS_SUCCESS;
}

void sample_data_destroy(ms_sample_data_t *sample) {
    if (sample && sample->data) {
        free(sample->data);
        sample->data = NULL;
    }
}
