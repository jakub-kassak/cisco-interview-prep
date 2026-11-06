# OpenH264 practice — first milestone

This standalone C++23 console project builds Cisco OpenH264 from source and
checks a complete encode/decode roundtrip. Qt and socket transport are later
milestones.

## Build and run

Prerequisites on macOS: Xcode Command Line Tools (`xcode-select --install`),
Git, Make, and CMake. Apple Silicon uses the native ARM64 build; an x86 build
also needs NASM for Cisco's assembly code.

The example requires C++23 and disables compiler-specific language extensions.
OpenH264 itself keeps the C++ standard selected by Cisco's upstream Makefile.

From the workspace root:

```sh
cd codec
bash scripts/build-openh264.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/codec_smoke build/sample.h264
```

The script pins [Cisco OpenH264 v2.7.0](https://github.com/cisco/openh264/releases/tag/v2.7.0)
to commit `1bca62311e447fd9c39fc93de2c4b96cd5ce971c`. It downloads source into
`.deps/openh264` and installs the static library and headers into `.deps/install`.
Nothing is installed globally. Run the script again for an incremental build.
`JOBS=8 bash scripts/build-openh264.sh` changes build parallelism.

## Read the example

Start with `src/main.cpp`, following these steps:

1. Create encoder and decoder objects. Custom `unique_ptr` deleters release them
   on both success and exceptions.
2. Configure a 320×240 screen-content encoder at 30 fps with fixed quantization
   and frame skipping disabled, so every test frame is checked.
3. Generate 30 I420 frames with a moving square. I420 stores Y, U and V in
   separate planes; U and V each have half the width and height of Y.
4. Encode each frame and copy all output NAL units into an owned byte vector.
   The codec owns its output buffers, so copy before encoding the next frame.
5. Pass each complete encoded frame directly to the decoder in memory.
6. Check decoded dimensions and mean luma error, reading rows using the
   decoder's stride. H.264 is lossy, so exact pixel equality is inappropriate.

The optional output is a raw Annex B H.264 stream, not an MP4 container.
The program processes frames as fast as possible; timestamps describe a 30 fps
sequence but do not pace execution.

## Next learning milestones

1. Make a separate Qt painting prototype: fixed-size `QImage`, one pen,
   mouse press/move/release, and `paintEvent()` to display it.
2. Convert that image's RGB pixels to I420 and feed the encoder. Convert decoded
   I420 back to RGB for a second window. Start with fixed even dimensions.
3. Replace the in-memory handoff with a local TCP connection. Send a 4-byte
   unsigned frame length in network byte order, then that frame's Annex B bytes.
   Handle partial writes and reads, cap accepted lengths, and retain incomplete
   messages until more bytes arrive: TCP does not preserve message boundaries.
4. Use a timer to capture at a modest frame rate and bound queued frames so a
   slow receiver cannot accumulate unlimited latency. Keep blocking work off
   the Qt GUI thread.

macOS also supports Unix domain sockets. TCP on `127.0.0.1` is a useful first
transport because the same protocol can later work between machines.

API reference and examples live in Cisco's
[`codec_api.h`](https://github.com/cisco/openh264/blob/v2.7.0/codec/api/wels/codec_api.h).
The downloaded source includes Cisco's license in `.deps/openh264/LICENSE`.
