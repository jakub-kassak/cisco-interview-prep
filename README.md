# PaintShare

A small C++23 / Qt 6 exercise. The first milestone is a bitmap painting app:
hold the left mouse button and move to draw with a black pen. A click draws a
dot. Clear resets the canvas; Save PNG exports it.

## Build and run on macOS

Install a C++23 compiler and standard library supporting `std::expected`
(the current Xcode Command Line Tools work), CMake, and Qt 6 Widgets.
With Homebrew:

```sh
brew install cmake qtbase
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
cmake --build build
./build/paint-share
```

For another Qt installation, set `CMAKE_PREFIX_PATH` to its installation
prefix. You can also open `CMakeLists.txt` in Qt Creator and choose a Qt 6 kit.

For Neovim/clangd, configure CMake before opening the source files. CMake exports
`build/compile_commands.json`, and the project `.clangd` points to it for `src/`
and `tests/`. This gives clangd the same Qt include paths, defines, and C++23
flags as the build. After configuring an existing editor session, restart clangd
with `:LspRestart clangd` (or close and reopen Neovim). If using a different build
directory, update `CompilationDatabase` in `.clangd` to match.

## Read the code

1. `src/main.cpp` creates the application, window, toolbar, and event loop.
   Qt parent objects own the widgets, so no explicit `delete` is needed.
2. `src/paint_canvas.h` defines a widget that owns a `QImage`, the actual bitmap.
3. `src/paint_canvas.cpp` handles mouse presses, moves, and releases. It connects
   consecutive mouse positions with lines, keeping quick strokes continuous.
   Drawing changes the image, then `update()` schedules a repaint.
4. `paintEvent()` copies the bitmap onto the window. Because the image stores
   the drawing, repainting after covering or resizing the window preserves it.

The bitmap stays at 960 × 540 pixels. Smaller windows scroll; larger windows
center the canvas. There is no scaling or coordinate conversion to debug.
The image uses `QImage::Format_RGB32`; use Qt's pixel accessors rather than
assuming raw byte channel order.

Qt's [Scribble example](https://doc.qt.io/qt-6/qtwidgets-widgets-scribble-example.html)
explains the same mouse-event / bitmap / repaint pattern in more detail.

## PNG transfer milestone

The separate `image-transfer` executable sends an existing PNG file over TCP
and renders it in the receiver. It does not use OpenH264 yet. Build it with the
same CMake commands above, then run these in two terminals, receiver first:

```sh
./build/image-transfer --mode receive --host 127.0.0.1 --port 5000
./build/image-transfer --mode send --host 127.0.0.1 --port 5000
```

In the sender, click **Choose PNG and send…**. Alternatively, add
`--file /absolute/path/image.png` to send automatically after connecting.
You can send additional PNGs over the same connection. Closing the sender lets
the receiver accept a new one. Restart the sender to reconnect after a failure.

For two computers, bind the receiver to its LAN IP (or `0.0.0.0`), and give the
sender that receiver's LAN IP. The machine's firewall must allow the chosen
port. This milestone uses plain TCP, without authentication or encryption.

The shared interfaces live in these headers:

| File | Contract |
| --- | --- |
| `src/paint_canvas.h` | `snapshot()` returns a `QImage` on the GUI thread |
| `src/image_view.h` | `setImage(QImage)` stores and paints an image on the GUI thread |
| `src/tcp_sender.h` | `sendMessage(QByteArray)` accepts an opaque payload or returns false |
| `src/tcp_receiver.h` | `messageReceived(QByteArray)` emits complete payloads in wire order |
| `src/transport_limits.h` | Wire format and shared size limits |

The wire format is a **4-byte big-endian unsigned payload length**, followed
by that many bytes. The payload is currently a complete PNG. Transport does not
interpret its contents, so a later codec can supply H.264 data instead.
Payloads must be between 1 byte and 16 MiB. Invalid lengths close the peer
connection. Partial reads/writes and multiple messages per read are supported.
Incomplete messages are discarded on disconnect.

The receiver's `tryReadMessage()` returns
`std::expected<std::optional<QByteArray>, QString>`: a complete message, a
successful `nullopt` when more bytes are needed, or an error string. Its caller
handles signal notifications and connection cleanup; the parser only reads and
assembles bytes. `std::expected` requires C++23.

The window owns the receiver and keeps it alive during signal callbacks.
Callbacks may close its connection, but must defer destruction of the receiver
or its owner until the callback chain returns (for example, using `deleteLater()`).

The sender writes complete framed messages directly into Qt's socket write
buffer, with a 32 MiB pending-byte budget including message headers. Qt handles
network writes asynchronously; there is no separate application message queue.
`sendMessage()` returning true means
accepted locally. `drained()` means pending application bytes have left Qt's
write buffer, not that the receiver has acknowledged or rendered the image.
Explicit `disconnectFromHost()` cancels pending work immediately.

For this file-transfer milestone, asynchronous sockets run on the GUI event
loop, and PNG loading/decoding is synchronous. There are no blocking socket
waits. Codec workers belong to the later video milestone; painting and display
will remain on the GUI thread. Do not call transport methods from another
thread directly.

File ownership for independent work: painting owns `paint_canvas.*`, display
owns `image_view.*`, sender owns `tcp_sender.*`, and receiver owns
`tcp_receiver.*`. Integration owns `image_transfer_main.cpp`, `transfer_options.h`,
`image_transfer_window.*`, CMake, and shared limits. The entry point parses typed
options and starts the window; the window wires sender/receiver events to named
file-loading and display methods. Public interface changes should be coordinated.

Run the transport checks with:

```sh
ctest --test-dir build --output-on-failure
```

These exercise real localhost sockets: exact wire bytes, binary payloads,
split headers and payloads, combined messages, invalid lengths, truncated
connections, and a fresh connection afterward. Tests require Qt Test, included
in Homebrew's Qt base module. To omit them, configure with `-DBUILD_TESTING=OFF`.

## Optional OpenH264 encoder

`H264Encoder` is a separate C++23 library target. It does not change the PNG
transfer application or painting app. It uses the local Cisco library built by
the existing standalone codec experiment:

```sh
bash codec/scripts/build-openh264.sh
cmake -S . -B build -DBUILD_H264_ENCODER=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The option defaults to `OFF`, so normal builds do not require OpenH264.
Set `OPENH264_ROOT` to use a different installation prefix; the default is
`codec/.deps/install`. The source-build script pins Cisco OpenH264 v2.7.0.

The API is intentionally small:

```cpp
auto created = H264Encoder::create(QSize(960, 540)); // 15 fps, 1 Mbit/s target.
if (!created) {
    // Report created.error().
    return;
}
auto encoder = std::move(created.value());
auto result = encoder->encode(canvas.snapshot(), timestampMs);
if (!result) {
    // Report result.error().
} else if (result->has_value()) {
    // Send result->value() as one TCP payload, in order.
} // Otherwise OpenH264 skipped the frame; do not send an empty payload.
```

One instance represents one video session with fixed even dimensions of at
least 16 × 16. Timestamps are nonnegative, nondecreasing milliseconds. Create a
fresh encoder for each new receiver connection: the first output contains an
IDR picture and parameter sets. Periodic IDRs are requested every two seconds
of configured frames. The codec uses bitrate control and may skip frames to
meet its target; the target is not a guaranteed network bandwidth limit.

`src/i420_image.cpp` converts Qt RGB pixels to BT.601 limited-range I420 with
2 × 2 chroma averaging. It handles Qt's padded scanlines and byte order through
`Format_RGB888`; alpha is discarded. It does not perform color-profile
conversion. No extra conversion dependency is needed for this learning
prototype. Every returned access unit owns its bytes independently of the
encoder's internal buffers, including after later calls or encoder destruction.

The wrapper has no signals or internal application worker thread. Call it
sequentially from one owning worker when integrating video; capture the canvas
on the GUI thread first and pass the `QImage` value to that worker. OpenH264's
internal encoding thread count is explicitly set to one. `H264Decoder` now
provides the matching owned-image decoder for the live-streaming worker.

With the option enabled, `encoder-tests` checks invalid configuration and input,
color/channel order, padded RGB rows, moving frames, decoded dimensions and
lossy quality tolerance, startup SPS/PPS/IDR NALs, and output ownership.

## Live painting over OpenH264 and TCP

The optional `paint-stream` executable combines the canvas, codec, and existing
length-prefixed TCP transport. Enable the codec as shown above, then start the
receiver first in one terminal and the sender in another:

```sh
./build/paint-stream --mode receive --host 127.0.0.1 --port 5000
./build/paint-stream --mode send --host 127.0.0.1 --port 5000
```

Draw with the sender's left mouse button. **Clear** resets its bitmap and the
next captured frame updates the receiver. The canvas is fixed at 960 × 540,
with a 15 fps capture timer and 1 Mbit/s encoder target. The original
`paint-share` and PNG `image-transfer` executables remain separate and runnable.
For two computers, bind the receiver to its LAN IP or `0.0.0.0`, and give the
sender that receiver's LAN IP. Plain TCP has no authentication or encryption;
this prototype is intended for local testing and a trusted LAN.

**Disconnect / Connect** stops and restarts the sender's session. If the peer
closes the connection or an error occurs, stop the old session before connecting
again. **Stop listening / Listen** does the same for the receiver. A normal sender
disconnect leaves the receiver listening for the next sender. Each new TCP
connection gets fresh encoder/decoder state, including initial SPS/PPS and IDR
NAL units. No frame can depend on the previous connection.

The thread boundaries are explicit:

```text
Sender GUI:    mouse → bitmap; timer → snapshot
Sender worker: snapshot → RGB/I420 → OpenH264 → TCP buffer
Receiver worker: TCP → complete access units → OpenH264 → owned RGB image
Receiver GUI:  latest image → repaint
```

`StreamSession` lives on the GUI thread and owns the worker's lifecycle.
`StreamWorker` creates and uses its socket and codec on one worker `QThread`.
Queued signals and queued method calls pass image values across the boundary;
workers never touch widgets. The timer checks `canCapture()` before taking a
snapshot, and only one capture job can be outstanding. A busy worker causes
capture ticks to be skipped, not accumulated.

Before encoding, the worker reserves enough TCP-buffer budget for the maximum
permitted access unit. If that budget is unavailable, it skips the raw capture
without changing codec state. If an already encoded frame cannot be sent,
the session ends: continuing after discarding a reference frame could corrupt
subsequent pictures. Encoded frames are always delivered to TCP in order.

On the receiving side, all encoded frames are decoded in order. Display
notifications are bounded to one outstanding GUI image and one replaceable
latest decoded image. The GUI acknowledges taking an image, allowing the worker
to publish the newest pending one. This acknowledgement is local scheduling,
not a network acknowledgement or a promise that the screen has repainted.

Stopping a session first runs socket/codec cleanup on its worker thread, then
quits and joins that thread. Normal frame processing never blocks the GUI.
Shutdown waits only for the current finite job and cleanup; it does not wait for
network delivery and never terminates a thread forcibly.

Useful review entry points:

| File | Responsibility |
| --- | --- |
| `src/paint_stream_main.cpp` | CLI and application startup |
| `src/paint_stream_window.cpp` | Canvas/view, GUI timer, Start/Stop controls |
| `src/stream_session.cpp` | Queued GUI/worker communication and bounded capture |
| `src/stream_worker.cpp` | Ordered codec/TCP pipeline and display coalescing |
| `src/h264_decoder.cpp` | OpenH264 decoding into independently owned images |
| `src/i420_image.cpp` | BT.601 limited-range RGB/I420 conversions |

With the codec enabled, `ctest --test-dir build --output-on-failure` runs six
suites: transport, encoder, decoder, threaded sessions, offscreen GUI, and
independent-process streaming. They cover colors and padded strides, image
ownership, fresh sessions, pre-encode backpressure, bounded captures/display,
shutdown during encoding and idle listening, drawing/Clear/reconnect, GUI event
progress, and decoded transfer between two separate processes. The GUI test
writes `build/received-stream-preview.png` for visual inspection. Normal
headless-platform/font warnings may appear; thread-affinity warnings are
checked separately by the session tests.

The current scope is one sender and one receiver, fixed dimensions, manual
reconnect, and BT.601 SDR conversion. There is no audio, automatic network
reconnection, dimension negotiation, or color-profile conversion. Cross-computer
operation uses the same transport but still needs a manual LAN check.

## Manual check

- Click once: a dot appears. Drag quickly: a continuous line appears.
- Right-click / move without pressing: nothing is drawn.
- Cover and uncover the window; shrink and enlarge it: the drawing remains.
- Save PNG and open it: it contains the full 960 × 540 bitmap.
- Clear: the entire canvas becomes white.
