# Real-Time Linux Guide for MIDI Sampler

## Overview

This MIDI sampler has been optimized for real-time performance on Linux systems, particularly those running:
- **BORE (Burst-Oriented Response Enhancer) scheduler**
- **RT-PREEMPT kernel patches**
- **Low-latency desktop configurations**

## Real-Time Optimizations

### Code-Level Optimizations

1. **Lock-Free Event Queue**
   - Note on/off events use atomic lock-free ring buffer
   - Zero blocking in audio thread
   - Predictable latency

2. **Cache-Aligned Data Structures**
   - 64-byte alignment for critical structures
   - Reduces cache thrashing
   - Better CPU cache utilization

3. **Pre-Calculated Coefficients**
   - MIDI note-to-frequency lookup table
   - Envelope coefficients calculated once
   - No `pow()` calls in audio path

4. **Compiler Optimizations**
   - `-O3` maximum optimization
   - `-march=native` CPU-specific instructions
   - `-ffast-math` for audio processing
   - Loop unrolling and function inlining

5. **Memory Management**
   - No allocations in audio thread
   - Memory locking with `mlockall()`
   - Pre-allocated voice pool

6. **Branch Prediction Hints**
   - `LIKELY()` and `UNLIKELY()` macros
   - Optimized hot paths
   - Minimal branching in loops

7. **Prefetching**
   - Memory prefetch hints for sample data
   - Better cache line utilization

## Building for Real-Time

### Using Nix Flake (Recommended)

```bash
# Enter development environment
nix develop

# Build with RT optimizations (default)
mkdir build && cd build
cmake .. -DENABLE_RT_OPTIMIZATIONS=ON
make -j$(nproc)
```

### Manual Build

```bash
# With maximum RT optimizations
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_RT_OPTIMIZATIONS=ON \
  -DCMAKE_C_FLAGS="-O3 -march=native -mtune=native -ffast-math"
make -j$(nproc)
```

## System Configuration

### 1. RT Kernel Setup

#### Install RT Kernel (Arch Linux)

```bash
# Install RT kernel
sudo pacman -S linux-rt linux-rt-headers

# Update bootloader
sudo grub-mkconfig -o /boot/grub/grub.cfg

# Reboot and select RT kernel
```

#### BORE Scheduler

If using BORE scheduler (available in some kernels):

```bash
# Check if BORE is available
cat /sys/kernel/debug/sched/features | grep BORE

# Enable BORE features
echo BORE_ENABLED > /sys/kernel/debug/sched/features
```

### 2. Kernel Parameters

Add these to your kernel command line (`/etc/default/grub`):

```bash
GRUB_CMDLINE_LINUX_DEFAULT="... threadirqs nohz_full=1-7 isolcpus=1-7 rcu_nocbs=1-7 processor.max_cstate=1"
```

Explanation:
- `threadirqs`: Make IRQ handlers threaded (required for RT)
- `nohz_full=1-7`: Disable timer ticks on CPUs 1-7
- `isolcpus=1-7`: Isolate CPUs from general scheduler
- `rcu_nocbs=1-7`: Move RCU callbacks off isolated CPUs
- `processor.max_cstate=1`: Prevent deep CPU sleep states

Update GRUB:
```bash
sudo grub-mkconfig -o /boot/grub/grub.cfg
sudo reboot
```

### 3. System Limits

Edit `/etc/security/limits.conf`:

```
@audio   -   rtprio     99
@audio   -   memlock    unlimited
@audio   -   nice       -19
```

Add your user to audio group:
```bash
sudo usermod -a -G audio $USER
```

### 4. CPU Governor

Set CPU governor to performance:

```bash
# One-time
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Persistent (systemd)
sudo systemctl enable --now cpupower.service
```

Edit `/etc/default/cpupower`:
```
governor='performance'
```

### 5. Disable CPU Frequency Scaling

```bash
# Disable turbo boost (Intel)
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# Or use fixed frequency
sudo cpupower frequency-set -f 3.5GHz
```

### 6. IRQ Affinity

Move IRQs away from audio CPU:

```bash
# Find your audio devices
cat /proc/interrupts | grep -i audio

# Set IRQ affinity (example for IRQ 125)
echo 1 | sudo tee /proc/irq/125/smp_affinity
```

## NixOS Configuration

### Complete NixOS Example

```nix
{ config, pkgs, ... }:

{
  # Import MIDI sampler module
  imports = [
    /path/to/midi_sampler/flake.nix#nixosModules.default
  ];

  # Enable MIDI sampler service
  services.midi-sampler = {
    enable = true;
    user = "myuser";
    rtPriority = 80;
  };

  # RT kernel
  boot.kernelPackages = pkgs.linuxPackages_rt;
  
  # Or use BORE kernel (if available)
  # boot.kernelPackages = pkgs.linuxPackages_bore;

  # Kernel parameters
  boot.kernelParams = [
    "threadirqs"
    "nohz_full=1-7"
    "isolcpus=1-7"
    "rcu_nocbs=1-7"
    "processor.max_cstate=1"
  ];

  # CPU governor
  powerManagement.cpuFreqGovernor = "performance";

  # Audio group and limits
  security.rtkit.enable = true;
  
  users.users.myuser = {
    extraGroups = [ "audio" ];
  };

  # Install audio tools
  environment.systemPackages = with pkgs; [
    jack2
    qjackctl
    pipewire
  ];
}
```

## Usage

### Basic RT-Safe Usage

```c
#include "midi_sampler.h"

int main() {
    // Create sampler
    ms_audio_config_t config = {
        .sample_rate = 48000,
        .channels = 2,
        .max_polyphony = 16,
        .buffer_size = 128  // Low latency!
    };
    
    ms_sampler_t *sampler;
    ms_sampler_create(&config, &sampler);
    
    // Enable RT mode (requires CAP_SYS_NICE or root)
    ms_sampler_enable_rt(sampler, 80);  // Priority 80
    
    // Create instrument and load samples
    ms_instrument_t *piano;
    ms_instrument_create(sampler, "Piano", &piano);
    
    ms_sample_metadata_t meta = { .root_note = 60 };
    ms_instrument_load_sample(piano, "piano.wav", &meta);
    
    // Trigger notes (RT-safe via lock-free queue)
    ms_note_on(piano, 60, 80, NULL);
    
    // Audio thread (RT priority set automatically)
    float buffer[128 * 2];
    while (running) {
        ms_process(sampler, buffer, 128);  // RT-safe!
        write_to_audio_device(buffer);
    }
    
    ms_note_off(piano, 60);
    
    // Cleanup
    ms_instrument_destroy(piano);
    ms_sampler_destroy(sampler);
}
```

### Running with RT Priority

```bash
# Check current limits
ulimit -r

# Run with RT priority
chrt -f 80 ./my_sampler_app

# Or use sudo if needed
sudo chrt -f 80 ./my_sampler_app
```

### JACK Integration

```c
#include <jack/jack.h>
#include "midi_sampler.h"

ms_sampler_t *sampler;

int process_callback(jack_nframes_t nframes, void *arg) {
    float *out_l = jack_port_get_buffer(port_out_l, nframes);
    float *out_r = jack_port_get_buffer(port_out_r, nframes);
    
    // Process interleaved
    float buffer[nframes * 2];
    ms_process(sampler, buffer, nframes);
    
    // Deinterleave
    for (jack_nframes_t i = 0; i < nframes; i++) {
        out_l[i] = buffer[i * 2];
        out_r[i] = buffer[i * 2 + 1];
    }
    
    return 0;
}

int main() {
    // Setup sampler with JACK's sample rate
    jack_client_t *client = jack_client_open("sampler", JackNullOption, NULL);
    
    ms_audio_config_t config = {
        .sample_rate = jack_get_sample_rate(client),
        .channels = 2,
        .max_polyphony = 32,
        .buffer_size = jack_get_buffer_size(client)
    };
    
    ms_sampler_create(&config, &sampler);
    ms_sampler_enable_rt(sampler, 75);  // Slightly lower than JACK
    
    // Register JACK callback
    jack_set_process_callback(client, process_callback, NULL);
    jack_activate(client);
    
    // ... rest of application
}
```

## Performance Monitoring

### Check RT Performance

```bash
# Install monitoring tools
sudo pacman -S linux-tools  # Arch
sudo apt install linux-tools-generic  # Ubuntu

# Monitor RT latency
sudo perf sched record -a sleep 30
sudo perf sched latency

# Check context switches
perf stat -e context-switches,cpu-migrations ./my_app

# CPU affinity monitoring
htop  # Press 'F2' -> Display options -> Show custom thread names
```

### Using Performance Counters

```c
#include "midi_sampler.h"

// Get statistics
uint64_t frames;
uint32_t xruns;
ms_get_stats(sampler, &frames, &xruns);

printf("Frames processed: %lu\n", frames);
printf("Buffer underruns: %u\n", xruns);
```

## Troubleshooting

### Audio Dropouts / Xruns

**Symptoms**: Crackling, pops, silence

**Solutions**:
1. Increase buffer size (reduce real-time requirements)
2. Check CPU governor is "performance"
3. Disable CPU frequency scaling
4. Move IRQs away from audio CPUs
5. Check for background processes on isolated CPUs
6. Reduce polyphony if CPU is overloaded

### Cannot Set RT Priority

**Error**: `Could not set RT priority`

**Solutions**:
1. Check `/etc/security/limits.conf` settings
2. Add user to `audio` group
3. Log out and back in
4. Run with `sudo` (not recommended for production)
5. Use `rtkit` (systemd feature)

### High Latency

**Solutions**:
1. Reduce buffer size (e.g., 128 or 64 frames)
2. Use RT kernel instead of generic
3. Isolate CPUs with `isolcpus=`
4. Disable hyperthreading if causing issues
5. Check IRQ distribution

### Memory Lock Fails

**Error**: `Could not lock memory`

**Solutions**:
1. Increase memlock limit in `/etc/security/limits.conf`
2. Check with `ulimit -l` (should be "unlimited")
3. Add `CAP_IPC_LOCK` capability to binary:
   ```bash
   sudo setcap cap_ipc_lock=+ep ./my_sampler_app
   ```

## Benchmarking

### Latency Test

```bash
# Compile example with RT optimizations
cd build
./simple_example

# With cyclictest (measures kernel latency)
sudo cyclictest -p 80 -t8 -n -m

# With rteval (comprehensive)
sudo rteval --duration=600 --loads-cpulist=0 --measurement-cpulist=1-7
```

### Expected Performance

With proper configuration:
- **Buffer size**: 64-256 frames
- **Latency**: < 10ms total (including hardware)
- **Xruns**: 0 under normal load
- **CPU**: < 30% for 16 voices @ 48kHz
- **Jitter**: < 100μs

## Best Practices

1. **Always use RT kernel** for production audio
2. **Test extensively** under load before live use
3. **Monitor xruns** and tune system accordingly
4. **Isolate CPUs** for dedicated audio processing
5. **Lock memory** to prevent page faults
6. **Use JACK or PipeWire** for professional routing
7. **Profile regularly** to catch performance regressions
8. **Keep system minimal** - disable unnecessary services

## References

- [Linux RT Wiki](https://wiki.linuxfoundation.org/realtime/start)
- [JACK Audio Connection Kit](https://jackaudio.org/)
- [PipeWire Real-Time](https://gitlab.freedesktop.org/pipewire/pipewire/-/wikis/home)
- [Arch Linux Pro Audio](https://wiki.archlinux.org/title/Professional_audio)

---

**Optimized for sub-10ms latency on BORE/RT Linux kernels.**
