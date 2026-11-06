# PaintShare

A C++23 / Qt 6 exercise: draw on a bitmap, encode it with Cisco OpenH264,
and display it in another process over TCP.

## Build and run

Requires a C++23 compiler/library with `std::expected`, CMake, and Qt 6.
On macOS, use Xcode Command Line Tools and Homebrew:

```sh
brew install cmake qtbase
bash codec/scripts/build-openh264.sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)" \
    -DBUILD_H264_ENCODER=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

The script builds pinned Cisco OpenH264 v2.7.0 inside `codec/.deps/`; it does
not install anything globally. Set `OPENH264_ROOT` to use another installation.
For another Qt installation, set `CMAKE_PREFIX_PATH` to its prefix.

Start the receiver first, then the sender in another terminal:

```sh
./build/paint-stream --mode receive --host 127.0.0.1 --port 5000
./build/paint-stream --mode send --host 127.0.0.1 --port 5000
```

Draw with the left mouse button. Click **Clear** to reset the canvas.
The sender captures a fixed 960 × 540 bitmap at 15 fps with a 1 Mbit/s encoder
target. That target does not guarantee a particular network bandwidth.

**Disconnect / Connect** stops and restarts a sender session. After a connection
error, stop the old session before connecting again. **Stop listening / Listen**
controls the receiver. A sender disconnect leaves the receiver listening.

For two computers, bind the receiver to its LAN IP or `0.0.0.0`, then give the
sender the receiver's LAN IP. Allow the selected port through its firewall.
Transport is plain TCP, intended for local testing or a trusted LAN.

## Smaller prototypes

| Executable | Purpose |
| --- | --- |
| `paint-share` | Painting only; Clear and Save PNG |
| `image-transfer` | Send a PNG file and display it over TCP |
| `paint-stream` | Live painting with OpenH264 and worker threads |

The first two build without OpenH264 when `BUILD_H264_ENCODER=OFF` (the default).
For PNG transfer, use `image-transfer --mode receive` and
`image-transfer --mode send`, then choose a PNG in the sender. Add
`--file /absolute/path/image.png` to send automatically. Restart that sender to
reconnect. Both transfer applications default to `127.0.0.1:5000`.

The standalone codec experiment has its own [instructions](codec/README.md).

## Follow one frame

```text
GUI:    mouse → PaintCanvas bitmap; timer → snapshot
Worker: snapshot → RGB/I420 → H264Encoder → TCP
Worker: TCP → complete message → H264Decoder → owned RGB image
GUI:    ImageView → repaint
```

| Read in this order | Responsibility |
| --- | --- |
| `src/paint_stream_main.cpp` | Parse arguments and start the window |
| `src/paint_stream_window.cpp` | Canvas/view, capture timer, controls |
| `src/paint_canvas.cpp` | Mouse strokes on a persistent `QImage` |
| `src/stream_session.cpp` | GUI-side worker lifecycle and queued requests |
| `src/stream_worker.cpp` | Codec and socket operations on one worker thread |
| `src/h264_encoder.cpp`, `src/h264_decoder.cpp` | Codec ownership, configuration, results |
| `src/i420_image.cpp` | BT.601 limited-range RGB/I420 conversions |
| `src/tcp_sender.cpp`, `src/tcp_receiver.cpp` | Bounded writes and incremental message parsing |
| `src/image_view.cpp` | Display the latest image with preserved aspect ratio |

Widgets and `PaintCanvas::snapshot()` stay on the GUI thread. A `StreamWorker`
creates and uses its sockets and codecs on its own `QThread`. Queued calls and
signals pass image values between threads. Stopping a session cleans up on the
worker thread, then quits and joins it; normal processing never blocks the GUI.

Only one capture job may be outstanding. Busy ticks are skipped. Before
encoding, the worker checks TCP-buffer capacity; if capacity is insufficient,
it skips the raw capture without advancing codec state. A failure to send an
already encoded frame ends the session, protecting reference-frame dependencies.

The receiver decodes every access unit in order but bounds display delivery to
one outstanding GUI notification and one replaceable latest image. The GUI
acknowledgement controls scheduling; it does not acknowledge network delivery.

## Data and ownership contracts

- `snapshot()` returns an implicitly shared `QImage` value. Later painting
  detaches the canvas buffer, preserving earlier snapshots.
- Codec methods use `std::expected` for errors and `std::optional` when no frame
  is produced. Returned bytes/images own their storage independently of Cisco's
  temporary buffers.
- Each connection gets fresh encoder/decoder state. Initial SPS/PPS and an IDR
  picture allow decoding without the previous session.
- Encoder input has fixed even dimensions of at least 16 × 16; timestamps are
  nonnegative, nondecreasing milliseconds. I420 uses 2 × 2 chroma subsampling;
  conversion respects padded rows, discards alpha, and does not transform color
  profiles. OpenH264 uses one internal encoding thread.
- TCP messages contain a four-byte big-endian unsigned payload length followed
  by 1 byte–16 MiB of opaque payload. Payloads are PNGs or H.264 access units,
  depending on the executable. Split/coalesced reads are supported; invalid
  lengths close the peer, and disconnect discards incomplete messages.
- Qt's outgoing buffer is capped at 32 MiB, including headers. `sendMessage()`
  success means accepted locally; `drained()` means Qt's buffer is empty.
  Explicit disconnect cancels pending data.
- Owners keep Qt objects alive through signal callbacks. Defer destruction of
  an active object or its owner until the callback chain returns, using
  `deleteLater()` where appropriate.

## Verification and editor setup

With the codec enabled, CTest runs six suites: transport, encoder, decoder,
threaded sessions, offscreen GUI, and independent-process streaming. They cover
colors/strides, buffer ownership, split messages, malformed-input recovery,
backpressure, display coalescing, drawing/Clear/reconnect, GUI progress, and
shutdown. The GUI test saves `build/received-stream-preview.png`. Qt Test is
included in Homebrew's Qt base module; use `-DBUILD_TESTING=OFF` to omit tests.

For a manual check, draw a dot and fast stroke, clear the canvas, resize/cover
both windows, disconnect/reconnect, and close them during streaming. A LAN test
between two computers remains manual. Current scope is one receiver, fixed
resolution, manual reconnect, BT.601 SDR color, and no audio.

CMake exports `build/compile_commands.json`; `.clangd` points `src/` and `tests/`
to it. Configure before opening Neovim, then run `:LspRestart clangd` if needed.
If you change build directories, update `.clangd`. Qt Creator can open
`CMakeLists.txt` directly with a Qt 6 kit. Qt's
[Scribble example](https://doc.qt.io/qt-6/qtwidgets-widgets-scribble-example.html)
explains the bitmap painting pattern.
