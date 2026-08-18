RtmpMonitor Linux ARM64 RASTER Engineering Preview
===================================================

This archive is cross-compiled for Ubuntu 22.04 Jammy AArch64. It is not a
generic package for every ARM64 board or Linux distribution. It has not been
qualified on the recipient's display controller, GPU, framebuffer, input,
audio hardware, or production camera network.

The package contains:

- the Release AArch64 RtmpMonitor executable;
- replaceable Qt 6.2.4, FFmpeg 8.1.2, and Paho MQTT C 1.3.16 shared libraries;
- Qt linuxfb, minimal, offscreen, evdev input, image format, compose, and
  certificate-only plugins;
- an offline media-server example configuration and license notices.

The package does not contain SRS, DVR, user MQTT settings, saved stream
settings, test media, source code, debug symbols, or a system service.

Target baseline
---------------

- Linux AArch64 with glibc compatible with Ubuntu 22.04;
- the system libraries listed in SYSTEM_RUNTIME_DEPENDENCIES.txt;
- a working framebuffer and input device configuration for linuxfb;
- system fonts and font configuration;
- ALSA/GStreamer/PulseAudio runtime components appropriate for the target if
  audio output is required.

Typical Jammy packages include libegl1, libfontconfig1, libx11-6,
libglib2.0-0, libdbus-1-3, libxkbcommon0, libglx0, libopengl0, libpng16-16,
libharfbuzz0b, libmd4c0, libfreetype6, zlib1g, libudev1, libmtdev1, libts0,
libinput10, libdrm2, libgl1, libgstreamer1.0-0,
libgstreamer-plugins-base1.0-0, gstreamer1.0-plugins-base,
gstreamer1.0-alsa, and libasound2. The exact image must be checked against
SYSTEM_RUNTIME_DEPENDENCIES.txt rather than assuming this example is complete.

Run
---

1. Extract the archive on the ARM64 target without changing its directory
   structure.
2. Make sure the launcher is executable:

       chmod +x run-rtmp-monitor.sh

3. For a framebuffer target:

       QT_QPA_PLATFORM=linuxfb ./run-rtmp-monitor.sh

4. For a command-line version smoke test:

       QT_QPA_PLATFORM=offscreen ./run-rtmp-monitor.sh --version

5. To use the packaged offline example explicitly:

       ./run-rtmp-monitor.sh \
         --media-server-config ./config/media-server.example.ini

The example configuration uses loopback only. Real endpoints must be entered
by the authorized operator and must not be written back into a distributed
package.

Limitations
-----------

The RASTER build uses the CPU rendering backend. The Ubuntu Qt build may still
have system-level EGL/OpenGL loader dependencies; that does not enable the
RtmpMonitor OpenGL renderer in this build.

Cross-compilation and QEMU smoke tests do not prove linuxfb, EGLFS, Wayland,
X11, GPU, VPU, audio, real-time video, temperature, or multi-stream performance
on a physical board. Run the repository's embedded qualification procedure on
the actual device before any production use.

No content integrity digest, digital signature, or trusted timestamp is
created or verified for this archive. The package therefore provides no
tamper-evidence guarantee.
