#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

// --- 字节序工具 (内联，多文件共享) ---
#ifdef _MSC_VER
#include <stdlib.h>
#define BSWAP32(x) _byteswap_ulong((unsigned long)(x))
#else
#define BSWAP32(x) ( \
    (((uint32_t)(x) & 0xFF000000) >> 24) | (((uint32_t)(x) & 0x00FF0000) >> 8) | \
    (((uint32_t)(x) & 0x0000FF00) << 8)  | (((uint32_t)(x) & 0x000000FF) << 24))
#endif

inline uint32_t Htonl(uint32_t v) { return BSWAP32(v); }
inline uint32_t Ntohl(uint32_t v) { return BSWAP32(v); }

// --- 网络协议定义 ---

#define UDP_DISCOVER_PORT       54320
#define TCP_DATA_PORT           54321
#define DISCOVER_REQ_STR        "LANCHAT_DISCOVER_REQ"
#define DISCOVER_RSP_PREFIX     "LANCHAT_DISCOVER_RSP|"
#define HEARTBEAT_INTERVAL_MS   5000
#define CHUNK_SIZE              65536

enum class MessageType : uint8_t {
    HEARTBEAT       = 0x01,
    TEXT_MSG        = 0x02,
    FILE_META       = 0x03,
    FILE_ACK        = 0x04,
    FILE_DATA       = 0x05,
    FILE_COMPLETE   = 0x06,
    FILE_CANCEL     = 0x07,
    CLIENT_NAME     = 0x08,
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t payloadLength;
    uint8_t  type;
};
#pragma pack(pop)

struct FileMeta {
    std::string fileName;
    uint64_t    fileSize;
    uint32_t    transferId;
    uint32_t    totalChunks;
};

struct ParsedPacket {
    MessageType type;
    std::vector<uint8_t> payload;
};

class Protocol {
public:
    static std::vector<uint8_t> EncodePacket(MessageType type, const std::vector<uint8_t>& payload);
    static std::vector<uint8_t> EncodeText(const std::string& text);
    static std::vector<uint8_t> EncodeFileMeta(const FileMeta& meta);
    static std::vector<uint8_t> EncodeFileAck(uint32_t transferId, uint32_t chunkIndex);
    static std::vector<uint8_t> EncodeFileData(uint32_t transferId, uint32_t chunkIndex,
                                               uint32_t totalChunks, const std::vector<uint8_t>& data);
    static std::vector<uint8_t> EncodeFileComplete(uint32_t transferId);
    static std::vector<uint8_t> EncodeFileCancel(uint32_t transferId);
    static std::vector<uint8_t> EncodeHeartbeat();
    static std::vector<uint8_t> EncodeClientName(const std::string& name);
    static bool TryParse(const std::vector<uint8_t>& buffer, ParsedPacket& outPacket, size_t& outConsumed);
    static uint32_t ReadU32(const uint8_t* data);
    static void     WriteU32(uint8_t* data, uint32_t val);
    static uint32_t CalculateChunks(uint64_t fileSize);
};
