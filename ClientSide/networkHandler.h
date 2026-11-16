#ifndef NETWORK_HANDLER_H
#define NETWORK_HANDLER_H

#include "messageQueue.h"
#include "../Protocol.h"

#include <openssl/ssl.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

enum class IOResult {
    Progress,
    WantRead,
    WantWrite,
    Closed,
    Error
};

class NetworkHandler {
private:
    SSL* ssl;
    int sockfd;

    MessageQueue& incoming;
    MessageQueue& outgoing;
    std::atomic<bool>& running;

    // Current framed outgoing message. Keep unchanged across WANT_* retries.
    std::string sendBuffer;
    size_t sendOffset = 0;

    // Incremental incoming frame state.
    char header[FRAME_HEADER_SIZE]{};
    size_t headerOffset = 0;
    uint32_t messageLength = 0;
    std::string receiveBuffer;
    size_t receiveOffset = 0;
    bool readingPayload = false;

public:
    NetworkHandler(
        SSL* ssl,
        int sockfd,
        MessageQueue& incoming,
        MessageQueue& outgoing,
        std::atomic<bool>& running
    );

    void run();

private:
    bool prepareOutgoingMessage();
    IOResult processRead();
    IOResult processWrite();
    void resetReceiveState();
};

#endif
