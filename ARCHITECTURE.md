# MIDI Sampler Architecture

## Design Philosophy

The MIDI Sampler library follows modern C design patterns emphasizing:

1. **Modularity**: Clear separation of concerns with well-defined interfaces
2. **Encapsulation**: Opaque pointer types hide implementation details
3. **Resource Management**: Explicit lifecycle management with no hidden allocations
4. **Thread Safety**: Mutex protection for concurrent access
5. **Error Handling**: Explicit error codes, no global error state

## Component Architecture

```
┌─────────────────────────────────────────────┐
│           Application Layer                  │
└─────────────────────────────────────────────┘
                    ▼
┌─────────────────────────────────────────────┐
│         Public API (midi_sampler.h)          │
│   - Sampler Lifecycle                        │
│   - Instrument Management                    │
│   - Playback Control                         │
│   - Audio Processing                         │
└─────────────────────────────────────────────┘
                    ▼
┌──────────────┬──────────────┬──────────────┐
│   Sampler    │  Instrument  │ MIDI Parser  │
│              │              │              │
│ - Voice Pool │ - Samples    │ - SMF Parse  │
│ - MIDI State │ - Envelope   │ - Events     │
│ - Threading  │ - Mapping    │              │
└──────────────┴──────────────┴──────────────┘
                    ▼
┌──────────────┬──────────────┬──────────────┐
│    Voice     │   Envelope   │Sample Loader │
│              │              │              │
│ - Playback   │ - ADSR       │ - WAV Parse  │
│ - Resampling │ - Stages     │ - PCM Conv   │
│ - Mixing     │ - Processing │              │
└──────────────┴──────────────┴──────────────┘
```

## Core Components

### 1. Sampler (`ms_sampler_t`)

**Responsibilities:**
- Manages pool of playback voices
- Coordinates MIDI event processing
- Provides thread-safe audio rendering
- Handles voice allocation/stealing

**Key Features:**
- Configurable polyphony limit
- Lock-based thread safety
- Zero-latency voice triggering
- Automatic voice stealing (oldest-first)

**State:**
```c
struct ms_sampler_t {
    ms_audio_config_t config;        // Audio parameters
    voice_t voices[MS_MAX_VOICES];   // Voice pool
    midi_track_t *current_track;     // Active MIDI track
    pthread_mutex_t lock;            // Thread safety
};
```

### 2. Instrument (`ms_instrument_t`)

**Responsibilities:**
- Stores sample collection
- Manages ADSR envelope settings
- Handles sample selection logic
- Tracks pitch bend state

**Key Features:**
- Multiple samples per instrument
- Velocity layer support
- Automatic nearest-note selection
- Per-instrument pitch bend range

**State:**
```c
struct ms_instrument_t {
    char name[64];
    ms_sample_data_t *samples[MAX_SAMPLES];
    ms_envelope_t envelope;
    float pitch_bend_range;
};
```

### 3. Voice (`voice_t`)

**Responsibilities:**
- Individual note playback
- Sample rate conversion
- Envelope application
- Audio mixing

**Key Features:**
- Linear interpolation resampling
- Real-time pitch adjustment
- Velocity-sensitive amplitude
- Sample looping support

**Processing Pipeline:**
```
Sample Data → Resample → Envelope → Velocity → Mix → Output
```

### 4. Envelope Generator (`envelope_generator_t`)

**Responsibilities:**
- ADSR envelope generation
- Stage transitions
- Level calculation

**States:**
```
IDLE → ATTACK → DECAY → SUSTAIN → RELEASE → IDLE
```

**Implementation:**
- Linear transitions (simple, efficient)
- Sample-accurate timing
- Configurable per-instrument
- No pops/clicks on transitions

### 5. Sample Loader

**Responsibilities:**
- WAV file parsing
- PCM format conversion
- Memory management

**Supported Formats:**
- 8-bit PCM (unsigned)
- 16-bit PCM (signed)
- Mono and stereo
- Any sample rate

**Conversion:**
- All samples converted to float [-1.0, 1.0]
- Consistent format for processing
- Automatic channel handling

### 6. MIDI Parser

**Responsibilities:**
- Standard MIDI File (SMF) parsing
- Event extraction
- Timing calculation

**Supported Events:**
- Note On/Off
- Pitch Bend
- Control Change (parsed but not processed)
- Tempo changes

## Data Flow

### Note Triggering

```
User Call → Find Sample → Allocate Voice → Trigger Envelope → Active
    ↓           ↓              ↓                ↓              ↓
ms_note_on → Sample     → Voice Pool    → ADSR Start    → Ready
            Selection     (or steal)      
```

### Audio Processing

```
ms_process() → For Each Voice → Resample → Envelope → Mix → Output
    ↓              ↓                ↓          ↓        ↓      ↓
Lock Mutex → Active Only?    → Linear   → ADSR   → Sum → Buffer
                                 Interp     Level
```

### MIDI Playback

```
MIDI File → Parse → Event Queue → Process → Trigger Voices
    ↓         ↓         ↓            ↓           ↓
Load SMF → Events → Timeline → Timing → ms_note_on/off
```

## Threading Model

### Thread Safety

The library uses a single mutex to protect shared state:

**Protected Operations:**
- Voice pool modification
- MIDI event processing
- Instrument state changes

**Lock-Free Operations:**
- Audio buffer processing (when isolated)
- Sample playback (read-only)
- Envelope calculation

### Typical Usage Pattern

```c
// Audio Thread
while (running) {
    ms_process(sampler, buffer, frames);  // Lock acquired here
    write_to_device(buffer);
}

// UI Thread
ms_note_on(instrument, note, velocity, NULL);  // Lock acquired here
```

## Memory Management

### Allocation Strategy

**Heap Allocations:**
- Sampler structure
- Instrument structures
- Sample data buffers
- MIDI event arrays

**Stack Usage:**
- Audio processing buffers (user-provided)
- Temporary calculation variables

### Ownership

```
Sampler owns:
  └─ Voice pool (fixed allocation)
  └─ MIDI track (dynamic)

Instrument owns:
  └─ Sample array (pointers)
      └─ Sample data (dynamic)
          └─ PCM buffer (dynamic)
```

### Lifecycle

```c
// Create
sampler = create()
instrument = create(sampler)
load_sample(instrument, ...)

// Use
note_on(instrument, ...)
process(sampler, ...)
note_off(instrument, ...)

// Destroy (reverse order)
destroy(instrument)    // Frees samples
destroy(sampler)       // Frees voices and MIDI
```

## Performance Characteristics

### Time Complexity

- **Voice Allocation**: O(n) where n = max_polyphony
- **Sample Selection**: O(m) where m = samples per instrument
- **Audio Processing**: O(v * f) where v = active voices, f = frames
- **Resampling**: O(1) per sample (linear interpolation)

### Space Complexity

- **Sampler**: O(1) fixed structure + O(v) voice pool
- **Instrument**: O(1) fixed structure + O(m * s) sample data
- **MIDI Track**: O(e) where e = number of events

### Optimization Opportunities

1. **SIMD Resampling**: Vectorize interpolation
2. **Voice Caching**: Reuse released voices without reset
3. **Sample Compression**: FLAC/OGG support
4. **Prefetching**: Cache-friendly memory layout
5. **Lock-Free Audio**: Separate control/audio paths

## Error Handling Strategy

### Philosophy

- Explicit error returns (no exceptions)
- Defensive parameter validation
- Graceful degradation where possible
- Clear error messages

### Error Propagation

```c
ms_error_t err = operation();
if (err != MS_SUCCESS) {
    // Handle error
    fprintf(stderr, "Error: %s\n", ms_error_string(err));
    // Cleanup
    // Return/continue
}
```

### Common Patterns

**Pre-condition Checks:**
```c
if (!param) return MS_ERROR_INVALID_PARAM;
```

**Resource Allocation:**
```c
ptr = malloc(size);
if (!ptr) return MS_ERROR_OUT_OF_MEMORY;
```

**File Operations:**
```c
FILE *fp = fopen(path, "rb");
if (!fp) return MS_ERROR_FILE_NOT_FOUND;
```

## Extension Points

### Adding New Sample Formats

1. Implement parser in new source file
2. Convert to internal float format
3. Update `ms_instrument_load_sample()`

### Adding Effects

1. Create effect processor module
2. Insert in voice processing pipeline
3. Add configuration to instrument

### Real-Time Audio I/O

1. Implement audio callback wrapper
2. Call `ms_process()` in callback
3. Handle buffer size negotiation

## Testing Strategy

### Unit Tests

- Envelope state transitions
- Sample selection logic
- MIDI parsing accuracy
- Voice allocation/stealing

### Integration Tests

- Full note lifecycle
- MIDI file playback
- Multi-voice polyphony
- Thread safety

### Performance Tests

- Audio processing latency
- Memory usage
- CPU usage under load
- Real-time safety

## Design Patterns Used

1. **Opaque Pointers**: Hide implementation, enable ABI stability
2. **Factory Pattern**: Create functions return initialized objects
3. **Pool Pattern**: Pre-allocated voice pool
4. **State Machine**: Envelope generator stages
5. **Strategy Pattern**: Sample selection algorithm
6. **RAII-like**: Explicit create/destroy pairs

## Standards Compliance

- **C11**: Modern C standard
- **POSIX Threads**: Cross-platform threading
- **IEEE Float**: Audio sample format
- **Standard MIDI**: SMF format compatibility
- **RIFF WAV**: Industry-standard sample format

---

*This architecture balances performance, maintainability, and ease of use while following professional software engineering practices.*
