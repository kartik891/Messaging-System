# C++ TCP Messaging System

A command-line messaging prototype written in C++ using TCP sockets on
Linux. The server pairs two clients and forwards messages between them.

## Features

-   TCP communication using POSIX sockets.
-   Two-client sessions with server-side message forwarding.
-   Length-prefixed messages that can contain spaces.
-   Client-side `poll()` to monitor keyboard input and socket activity.
-   `/quit` command for ending a session.
-   Basic connection and send/receive error handling.

## Project Structure

``` text
Message/
├── main.cpp
├── Protocol.h
├── ClientSide/
│   ├── Client.h
│   └── Client.cpp
└── ServerSide/
    ├── server.cpp
    └── handleClient.cpp
```

## Requirements

-   Linux or another POSIX-compatible operating system.
-   `g++` with C++17 support.
-   POSIX sockets and pthread support.

## Build

Run these commands from the `Message/` directory.

### Client

``` bash
g++ -std=c++17 -Wall -Wextra -Wpedantic main.cpp ClientSide/Client.cpp -pthread -o Client
```

### Server

``` bash
g++ -std=c++17 -Wall -Wextra -Wpedantic ServerSide/server.cpp -pthread -o server
```

## Run

From the `Message/` directory, start the server:

``` bash
./server
```

Then open two other terminals and run one client in each:

``` bash
./Client Alice
```

``` bash
./Client Bob
```

The client connects to `localhost:9000`. Start the server first and
connect two clients for a pair to communicate.

Type a message and press Enter to send it. Enter `/quit` to signal that
you are leaving the session.

## Message Protocol

Each message consists of:

1.  A 32-bit unsigned integer containing the payload length in network
    byte order.
2.  The payload, containing that many bytes.

TCP is a byte stream and does not preserve application message
boundaries, so the length prefix lets the receiver determine how many
bytes belong to each message.

The current maximum payload size is **1 MiB** (`1024 * 1024` bytes).

## How `poll()` Works in This Client

The client monitors two file descriptors:

-   `STDIN_FILENO`: keyboard input.
-   The connected socket: incoming messages and connection events.

When either descriptor becomes ready, the client handles that event and
returns to `poll()`.

**Current limitation:** `poll()` only indicates that some data is
available, not that an entire framed message has arrived. The current
receive helper performs blocking reads until the full frame is received.
If a message arrives in pieces, the event loop can block waiting for the
remaining bytes. A robust event-driven implementation should use
nonblocking sockets, an input buffer, and incremental message parsing.

## Current Scope and Limitations

This is a learning prototype, not a production-ready messaging service.

-   The server pairs clients two at a time; it is not a multi-room or
    many-to-many chat server.
-   Blocking receive calls limit how safely the client can use `poll()`.
-   Socket ownership and shutdown need more coordination because the
    server uses detached threads.
-   Host and port are currently hard-coded to `localhost:9000`.
-   There is no authentication, encryption, or persistent message
    history.
-   Graceful shutdown behavior can be improved.

## Troubleshooting

### Connection refused

Start the server first and confirm that it is listening on port `9000`.

### Address already in use

Check whether another server process is running. Consider setting
`SO_REUSEADDR` on the listening socket before calling `bind()`.

### Multiple-definition linker errors

Mark the free functions in `Protocol.h` as `inline`, or move their
definitions into `Protocol.cpp` and compile that file exactly once.

### Clients are not exchanging messages

The current server expects two clients for a session. Start the server
and connect two clients, then check all three terminals for errors.

## Possible Improvements

1.  Use nonblocking sockets with incremental frame parsing.
2.  Coordinate session shutdown and socket ownership.
3.  Iterate through all `getaddrinfo()` results.
4.  Add `SO_REUSEADDR` and improve `accept()` error handling.
5.  Add tests for partial reads, disconnects, and malformed message
    lengths.
6.  Make host and port configurable.
7.  Add authentication and TLS if the project is extended beyond local
    experimentation.