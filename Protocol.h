#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstddef>
#include <cstdint>

// Every application message is framed as a 4-byte network-order length
// followed by exactly that many payload bytes.
inline constexpr std::size_t FRAME_HEADER_SIZE = sizeof(std::uint32_t);
inline constexpr std::uint32_t MAX_MESSAGE_SIZE = 1024U * 1024U;

#endif
