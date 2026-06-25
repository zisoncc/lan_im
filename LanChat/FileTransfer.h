#pragma once
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <mutex>
#include <winsock2.h>
#include <fstream>
#include <atomic>
#include "Protocol.h"
#include "ClientCore.h"

struct FileProgressInfo {
    std::string fileName;
    int         progress;       // 0-100
    uint32_t    currentChunk;
    uint32_t    totalChunks;
    bool        isSending;      // true=发送 false=接收
};

class FileTransfer {
public:
    FileTransfer();
    ~FileTransfer();

    // 发送文件 (服务端 -> 指定客户端 socket)
    void SendFile(const std::string& fileName, const std::string& filePath,
                  SOCKET targetSock,
                  std::function<void(const FileProgressInfo&)> progressCallback);

    // 发送文件 (客户端 -> 通过 ClientCore)
    void SendFileDirect(const std::string& fileName, const std::string& filePath,
                        ClientCore* clientCore,
                        std::function<void(const FileProgressInfo&)> progressCallback);

    // 接收文件 (服务端或客户端收到 FILE_META 后调用)
    void ReceiveFileStart(const FileMeta& meta,
                          std::function<void(const FileProgressInfo&)> progressCallback);

    // 接收文件数据块
    void ReceiveFileData(uint32_t transferId, uint32_t chunkIndex, const std::vector<uint8_t>& data);

    // 接收完成
    void ReceiveFileComplete(uint32_t transferId);

    // 取消传输
    void CancelTransfer(uint32_t transferId);

private:
    // 当前接收中的文件传输
    struct ReceiveState {
        std::string fileName;
        std::string savePath;
        uint64_t    fileSize;
        uint32_t    transferId;
        uint32_t    totalChunks;
        uint32_t    receivedChunks;
        std::ofstream fileStream;
        std::function<void(const FileProgressInfo&)> progressCallback;
        bool        active;
    };

    std::map<uint32_t, ReceiveState> m_receives;
    std::mutex m_mutex;
    uint32_t m_nextTransferId;
};
