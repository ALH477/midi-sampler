# Quick Start Guide

## Get Started in 5 Minutes

### 1. Build the Library

```bash
cd midi_sampler
make
```

This creates:
- `libmidi_sampler.a` - Static library
- `simple_example` - Demo program

### 2. Run the Example

```bash
./simple_example
```

You should see:
```
MIDI Sampler Library v1.0.0
Simple Example

✓ Created sampler (sample rate: 44100 Hz, polyphony: 16)
✓ Created instrument
✓ Configured envelope
✓ Loaded 4 samples
♪ Playing melody...
```

### 3. Use in Your Project

#### Basic Integration

```c
#include "midi_sampler.h"

int main() {
    // Configure
    ms_audio_config_t config = {
        .sample_rate = 44100,
        .channels = 2,
        .max_polyphony = 16,
        .buffer_size = 512
    };
    
    // Create
    ms_sampler_t *sampler;
    ms_sampler_create(&config, &sampler);
    
    ms_instrument_t *piano;
    ms_instrument_create(sampler, "Piano", &piano);
    
    // Load sample
    ms_sample_metadata_t meta = {
        .root_note = 60,  // C4
        .velocity_low = 0,
        .velocity_high = 127
    };
    ms_instrument_load_sample(piano, "piano.wav", &meta);
    
    // Play
    ms_note_on(piano, 60, 80, NULL);
    
    float buffer[512 * 2];
    ms_process(sampler, buffer, 512);
    // ... send buffer to audio output
    
    ms_note_off(piano, 60);
    
    // Cleanup
    ms_instrument_destroy(piano);
    ms_sampler_destroy(sampler);
}
```

#### Compile Your App

```bash
gcc myapp.c -I/path/to/midi_sampler/include \
    -L/path/to/midi_sampler -lmidi_sampler \
    -lm -lpthread -o myapp
```

### 4. Advanced: MIDI File Playback

```c
// Load MIDI file
ms_load_midi_file(sampler, instrument, "song.mid");

// Start playback
ms_start_playback(sampler);

// In your audio callback
while (ms_is_playing(sampler)) {
    ms_process(sampler, buffer, frames);
    // Output buffer to audio device
}
```

### 5. Common Patterns

#### Creating a Multi-Sample Instrument

```c
// Load multiple samples for different notes
const struct { const char *file; uint8_t note; } samples[] = {
    {"piano_c3.wav", 48},
    {"piano_c4.wav", 60},
    {"piano_c5.wav", 72}
};

for (int i = 0; i < 3; i++) {
    ms_sample_metadata_t meta = {
        .root_note = samples[i].note,
        .velocity_low = 0,
        .velocity_high = 127
    };
    ms_instrument_load_sample(piano, samples[i].file, &meta);
}
```

#### Velocity Layers

```c
// Load soft and loud samples
ms_sample_metadata_t soft = {
    .root_note = 60,
    .velocity_low = 0,
    .velocity_high = 64
};
ms_instrument_load_sample(piano, "piano_c4_soft.wav", &soft);

ms_sample_metadata_t loud = {
    .root_note = 60,
    .velocity_low = 65,
    .velocity_high = 127
};
ms_instrument_load_sample(piano, "piano_c4_loud.wav", &loud);
```

#### Custom Envelope

```c
ms_envelope_t envelope = {
    .attack_time = 0.01f,   // 10ms
    .decay_time = 0.2f,     // 200ms
    .sustain_level = 0.6f,  // 60%
    .release_time = 1.0f    // 1 second
};
ms_instrument_set_envelope(piano, &envelope);
```

### 6. Real-Time Audio Integration

#### PortAudio Example

```c
int audio_callback(const void *input, void *output,
                   unsigned long frames, ...) {
    float *out = (float*)output;
    ms_process(sampler, out, frames);
    return 0;
}

Pa_OpenStream(&stream, NULL, &output_params, 44100,
              512, 0, audio_callback, sampler);
Pa_StartStream(stream);
```

#### JACK Example

```c
int process(jack_nframes_t frames, void *arg) {
    ms_sampler_t *sampler = (ms_sampler_t*)arg;
    float *out = jack_port_get_buffer(output_port, frames);
    ms_process(sampler, out, frames);
    return 0;
}
```

### 7. Error Handling

Always check return values:

```c
ms_error_t err = ms_note_on(piano, 60, 80, NULL);
if (err != MS_SUCCESS) {
    fprintf(stderr, "Error: %s\n", ms_error_string(err));
    // Handle error
}
```

### 8. Memory Management

Remember to destroy in reverse order:

```c
// Create order: sampler → instrument → samples
// Destroy order: instrument → sampler
ms_instrument_destroy(instrument);  // Frees samples too
ms_sampler_destroy(sampler);
```

### 9. Thread Safety

Safe to call from different threads:

```c
// Audio thread
ms_process(sampler, buffer, frames);

// UI thread
ms_note_on(instrument, note, velocity, NULL);
ms_note_off(instrument, note);
```

### 10. Troubleshooting

**No sound?**
- Check sample rate matches your audio device
- Verify samples are loaded correctly
- Ensure envelope times aren't too long

**Crackles/pops?**
- Increase buffer size
- Check CPU usage
- Reduce polyphony if needed

**Build errors?**
- Make sure you have `pthread` and `libm`
- Use C11 or later: `-std=c11`
- Include math.h for M_PI

## Next Steps

- Read `README.md` for full API documentation
- Check `ARCHITECTURE.md` for design details
- Explore `examples/` for more code samples
- See `include/midi_sampler.h` for complete API reference

## Resources

- **Documentation**: See README.md and ARCHITECTURE.md
- **Examples**: Check examples/ directory
- **Header**: See include/midi_sampler.h for all APIs
- **License**: MIT (see LICENSE file)

---

**Happy coding! 🎵**
