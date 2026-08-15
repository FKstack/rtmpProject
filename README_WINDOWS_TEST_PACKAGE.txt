RtmpMonitor v0.1.0-alpha.1 Windows x64 Test Package
===================================================

This is a Development Preview for authorized testing, not a production release.

1. Extract the ZIP into a new, empty test directory. This Development Preview does
   not include or require a separately generated package hash file.
2. On a clean Windows machine, run vc_redist.x64.exe once if the current Microsoft
   Visual C++ x64 Runtime is not already installed. The installer is unmodified,
   Microsoft-signed, and must be version 14.41 or newer for this build.
3. Start rtmp_monitor.exe. The command "rtmp_monitor.exe --version" must print
   0.1.0-alpha.1 and exit immediately.
4. Copy media-server.example.ini to media-server.ini before editing it. Use only
   a test SRS address and test stream key; never put production credentials in
   the example file.
5. This package does not include SRS. SRS 6.0.184 remains a separate process.
6. MQTT control defaults to the unencrypted test broker documented by the project.
   Secure the car on a stand before sending movement commands. Device-side
   disconnect/timeout auto-stop is not certified by this desktop package.

See LICENSE, THIRD_PARTY_NOTICES, VERSION.txt, DEPENDENCY_SOURCES.txt, and the
licenses directory for provenance and licensing information.
