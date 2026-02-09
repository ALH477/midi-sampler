{
  description = "High-quality MIDI instrument sampler library optimized for real-time Linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        
        # Real-time optimization flags
        rtOptimizations = [
          "-O3"                    # Maximum optimization
          "-march=native"          # CPU-specific optimizations
          "-mtune=native"
          "-ffast-math"           # Fast math (safe for audio)
          "-funroll-loops"        # Loop unrolling
          "-fomit-frame-pointer"  # Reduce overhead
          "-DNDEBUG"             # Disable assertions in release
        ];

        # Real-time compile flags
        rtCFlags = builtins.concatStringsSep " " rtOptimizations;

      in {
        packages = {
          default = self.packages.${system}.midi_sampler;
          
          midi_sampler = pkgs.stdenv.mkDerivation {
            pname = "midi_sampler";
            version = "1.0.0";

            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
            ];

            buildInputs = with pkgs; [
              # Core dependencies for threading and math
              # Note: libm is implicit with gcc/clang
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DBUILD_EXAMPLES=ON"
              "-DBUILD_SHARED_LIBS=ON"
              "-DENABLE_RT_OPTIMIZATIONS=ON"
            ];

            # Set CFLAGS for RT optimizations
            CFLAGS = "-O3 -march=native -mtune=native -ffast-math -funroll-loops -DNDEBUG";

            meta = with pkgs.lib; {
              description = "High-quality MIDI sampler library optimized for real-time Linux with BORE/RT scheduler support";
              homepage = "https://github.com/ALH477/midi-sampler";
              license = licenses.mit;
              platforms = platforms.unix ++ [ "x86_64-windows" "x86_64-darwin" ];
              maintainers = with pkgs.lib.maintainers; [ ashers ];
            };
          };

          # Static library variant for embedded use
          midi_sampler-static = self.packages.${system}.midi_sampler.overrideAttrs (old: {
            cmakeFlags = old.cmakeFlags ++ [
              "-DBUILD_SHARED_LIBS=OFF"
            ];
          });

          # Standard build variant (no RT optimizations)
          midi_sampler-standard = pkgs.stdenv.mkDerivation {
            pname = "midi_sampler";
            version = "1.0.0";

            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
            ];

            buildInputs = with pkgs; [
              # Core dependencies for threading and math
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DBUILD_EXAMPLES=ON"
              "-DBUILD_SHARED_LIBS=ON"
              "-DENABLE_RT_OPTIMIZATIONS=OFF"
            ];

            meta = with pkgs.lib; {
              description = "High-quality MIDI sampler library (standard build)";
              homepage = "https://github.com/ALH477/midi-sampler";
              license = licenses.mit;
              platforms = platforms.unix ++ [ "x86_64-windows" "x86_64-darwin" ];
              maintainers = [ ];
            };
          };

          # Development shell tools
          rt-tools = pkgs.buildEnv {
            name = "midi-sampler-rt-tools";
            paths = with pkgs; [
              linuxPackages.perf
              htop
              stress-ng
              rtkit
              pipewire
              jack2
              qjackctl
            ];
          };
        };

        # Development shell with RT kernel utilities
        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.midi_sampler ];
          
          packages = with pkgs; [
            # Development tools
            clang-tools
            cmake
            gdb
            valgrind
            
            # Real-time tools
            linuxPackages.perf
            rtkit
            jack2
            qjackctl
            
            # Audio tools
            portaudio
            alsa-lib
            pipewire
            
            # Performance analysis
            hotspot
            heaptrack
            
            # Documentation
            doxygen
            graphviz
          ];

          shellHook = ''
            echo "🎵 MIDI Sampler RT Development Environment"
            echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
            echo "Optimized for BORE scheduler on RT Linux"
            echo ""
            echo "Build commands:"
            echo "  mkdir build && cd build"
            echo "  cmake .. -DCMAKE_C_FLAGS='${rtCFlags}'"
            echo "  make -j\$(nproc)"
            echo ""
            echo "RT Performance tips:"
            echo "  - Check RT limits: ulimit -r"
            echo "  - Set CPU governor: echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
            echo "  - Disable CPU frequency scaling for consistency"
            echo "  - Use 'chrt -f 80' to run with RT priority"
            echo ""
            echo "BORE scheduler check:"
            echo "  cat /sys/kernel/debug/sched/features | grep BORE"
            echo ""
          '';

          # Real-time environment variables
          CFLAGS = rtCFlags;
          CMAKE_BUILD_TYPE = "Release";
          
          # Suggest RT priority settings
          RT_PRIO = "80";
          AUDIO_BUFFER_SIZE = "256";
        };

        # NixOS module for system-level RT configuration
        nixosModules.default = { config, lib, pkgs, ... }: {
          options.services.midi-sampler = {
            enable = lib.mkEnableOption "MIDI Sampler RT service";
            
            user = lib.mkOption {
              type = lib.types.str;
              default = "audio";
              description = "User to run the MIDI sampler service";
            };

            rtPriority = lib.mkOption {
              type = lib.types.int;
              default = 80;
              description = "Real-time priority (1-99)";
            };
          };

          config = lib.mkIf config.services.midi-sampler.enable {
            # RT kernel optimizations
            boot.kernelParams = [
              "threadirqs"           # Threaded IRQs for RT
              "nohz_full=1-7"       # Disable ticks on CPU 1-7
              "isolcpus=1-7"        # Isolate CPUs for RT tasks
              "rcu_nocbs=1-7"       # RCU callbacks off RT CPUs
              "processor.max_cstate=1"  # Prevent deep C-states
            ];

            # System configuration for RT audio
            security.rtkit.enable = true;
            
            security.pam.loginLimits = [
              { domain = "@audio"; type = "-"; item = "rtprio"; value = "99"; }
              { domain = "@audio"; type = "-"; item = "memlock"; value = "unlimited"; }
              { domain = "@audio"; type = "-"; item = "nice"; value = "-19"; }
            ];

            # CPU governor for consistent performance
            powerManagement.cpuFreqGovernor = "performance";

            # Install the library system-wide
            environment.systemPackages = [
              self.packages.${pkgs.system}.midi_sampler
              self.packages.${pkgs.system}.rt-tools
            ];

            # Audio group configuration
            users.groups.audio = {};
            users.users.${config.services.midi-sampler.user}.extraGroups = [ "audio" ];
          };
        };

        # Checks for CI
        checks = {
          build = self.packages.${system}.midi_sampler;
          
          # Test compilation with RT flags
          rt-compile-test = pkgs.runCommand "rt-compile-test" {
            buildInputs = [ self.packages.${system}.midi_sampler ];
          } ''
            echo "Testing RT-optimized build..."
            mkdir -p $out
            echo "Build successful" > $out/result
          '';
        };

        # Apps for easy running
        apps = {
          simple_example = {
            type = "app";
            program = "${self.packages.${system}.midi_sampler}/bin/simple_example";
            meta = with pkgs.lib; {
              description = "Simple MIDI sampler example demonstrating basic functionality";
            };
          };
          
          midi_player = {
            type = "app";
            program = "${self.packages.${system}.midi_sampler}/bin/midi_player";
            meta = with pkgs.lib; {
              description = "MIDI file player example using the MIDI sampler library";
            };
          };
          
          rt_example = {
            type = "app";
            program = "${pkgs.writeShellScriptBin "rt_example" ''
              export RT_PRIO=80
              echo "🚀 Running RT Example with priority 80"
              chrt -f 80 ${self.packages.${system}.midi_sampler}/bin/rt_example "$@"
            ''}/bin/rt_example";
            meta = with pkgs.lib; {
              description = "Real-time optimized example with performance monitoring and RT priority";
            };
          };
        };

        # Formatter
        formatter = pkgs.nixpkgs-fmt;
      }
    );
}
