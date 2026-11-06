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

## Later milestone: OpenH264 streaming

The painting and PNG-transfer executables remain separate. Build the video
pipeline as a later milestone, then connect it to the canvas:

1. First stream a generated test image using
   [Cisco OpenH264](https://github.com/cisco/openh264).
2. Convert RGB into I420 (Y, U, V planes), encode it, and use the existing TCP
   transport to send complete encoded access units in order.
3. Decode received units, convert back to a Qt image, and display through
   `ImageView::setImage()`.
4. Sample `PaintCanvas::snapshot()` on the GUI thread with a timer (for example,
   15 frames per second), then pass the snapshot to the encoding worker.
   Keep networking and encoding work off the GUI thread.

`snapshot()` returns a `QImage` by value. Qt's implicit sharing makes the initial
copy inexpensive; later edits detach the drawing buffer, preserving the older
snapshot. This is a useful starting point for passing frames to a worker.

## Manual check

- Click once: a dot appears. Drag quickly: a continuous line appears.
- Right-click / move without pressing: nothing is drawn.
- Cover and uncover the window; shrink and enlarge it: the drawing remains.
- Save PNG and open it: it contains the full 960 × 540 bitmap.
- Clear: the entire canvas becomes white.
