#include "Protocol.h"
#include <cstring>
#include <sstream>
#include <algorithm>

std::vector<uint8_t> Protocol::EncodePacket(MessageType type, const std::vector<uint8_t>& payload)
{
    PacketHeader hdr;
    hdr.payloadLength = Htonl((uint32_t)payload.size());
    hdr.type = (uint8_t)type;
    std::vector<uint8_t> packet(sizeof(hdr) + payload.size());
    memcpy(packet.data(), &hdr, sizeof(hdr));
    if (!payload.empty())
        memcpy(packet.data() + sizeof(hdr), payload.data(), payload.size());
    return packet;
}

std::vector<uint8_t> Protocol::EncodeText(const std::string& text)
{
    std::vector<uint8_t> payload(text.begin(), text.end());
    return EncodePacket(MessageType::TEXT_MSG, payload);
}

std::vector<uint8_t> Protocol::EncodeFileMeta(const FileMeta& meta)
{
    std::ostringstream oss;
    oss << meta.fileName << "|" << meta.fileSize << "|" << meta.transferId << "|" << meta.totalChunks;
    std::string s = oss.str();
    std::vector<uint8_t> p(s.begin(), s.end());
    return EncodePacket(MessageType::FILE_META, p);
}

std::vector<uint8_t> Protocol::EncodeFileAck(uint32_t transferId, uint32_t chunkIndex)
{
    std::vector<uint8_t> p(8);
    WriteU32(p.data(), transferId); WriteU32(p.data()+4, chunkIndex);
    return EncodePacket(MessageType::FILE_ACK, p);
}

std::vector<uint8_t> Protocol::EncodeFileData(uint32_t transferId, uint32_t chunkIndex,
                                              uint32_t totalChunks, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> p(12 + data.size());
    WriteU32(p.data(), transferId); WriteU32(p.data()+4, chunkIndex);
    WriteU32(p.data()+8, totalChunks);
    memcpy(p.data()+12, data.data(), data.size());
    return EncodePacket(MessageType::FILE_DATA, p);
}

std::vector<uint8_t> Protocol::EncodeFileComplete(uint32_t transferId)
{
    std::vector<uint8_t> p(4); WriteU32(p.data(), transferId);
    return EncodePacket(MessageType::FILE_COMPLETE, p);
}

std::vector<uint8_t> Protocol::EncodeFileCancel(uint32_t transferId)
{
    std::vector<uint8_t> p(4); WriteU32(p.data(), transferId);
    return EncodePacket(MessageType::FILE_CANCEL, p);
}

std::vector<uint8_t> Protocol::EncodeHeartbeat()
{
    return EncodePacket(MessageType::HEARTBEAT, {});
}

std::vector<uint8_t> Protocol::EncodeClientName(const std::string& name)
{
    std::vector<uint8_t> p(name.begin(), name.end());
    return EncodePacket(MessageType::CLIENT_NAME, p);
}

bool Protocol::TryParse(const std::vector<uint8_t>& buffer, ParsedPacket& outPacket, size_t& outConsumed)
{
    if (buffer.size() < sizeof(PacketHeader)) { outConsumed = 0; return false; }
    const PacketHeader* hdr = (const PacketHeader*)buffer.data();
    uint32_t plen = Ntohl(hdr->payloadLength);
    size_t total = sizeof(PacketHeader) + plen;
    if (buffer.size() < total) { outConsumed = 0; return false; }
    outPacket.type = (MessageType)hdr->type;
    outPacket.payload.assign(buffer.begin() + sizeof(PacketHeader), buffer.begin() + total);
    outConsumed = total;
    return true;
}

uint32_t Protocol::ReadU32(const uint8_t* data)
{
    uint32_t v; memcpy(&v, data, sizeof(v)); return Ntohl(v);
}

void Protocol::WriteU32(uint8_t* data, uint32_t val)
{
    val = Htonl(val); memcpy(data, &val, sizeof(val));
}

uint32_t Protocol::CalculateChunks(uint64_t fileSize)
{
    return (uint32_t)((fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE);
}
