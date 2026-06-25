#include "FileTransfer.h"
#include "NetUtils.h"
#include <shlobj.h>
#include <sstream>
#include <thread>
#include <filesystem>

static std::wstring Utf8ToWidePath(const std::string& text)
{
    if (text.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.c_str(), (int)text.size(), nullptr, 0);
    if (len <= 0)
    {
        len = MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), nullptr, 0);
        if (len <= 0) return L"";
        std::wstring fallback(len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), fallback.data(), len);
        return fallback;
    }
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), len);
    return wide;
}

// 获取下载目录
static std::string GetDownloadPath()
{
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, path)))
    {
        // 放到 "LanChat" 子目录
        std::wstring dir = std::wstring(path) + L"\\LanChat";
        CreateDirectoryW(dir.c_str(), nullptr);
        int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string ret(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, &ret[0], len, nullptr, nullptr);
        ret.resize(len - 1);
        return ret;
    }
    return "C:\\LanChat\\";
}

FileTransfer::FileTransfer()
    : m_nextTransferId(1)
{
    CreateDirectoryA("C:\\LanChat", nullptr);
}

FileTransfer::~FileTransfer()
{
    for (auto& [id, state] : m_receives)
    {
        if (state.fileStream.is_open())
            state.fileStream.close();
    }
    m_receives.clear();
}

void FileTransfer::SendFile(const std::string& fileName, const std::string& filePath,
                            SOCKET targetSock,
                            std::function<void(const FileProgressInfo&)> progressCallback)
{
    // 在新线程中发送文件，避免阻塞 UI
    std::thread([this, fileName, filePath, targetSock, progressCallback]() {
        std::ifstream file(std::filesystem::path(Utf8ToWidePath(filePath)), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            FileProgressInfo err;
            err.fileName = fileName;
            err.progress = -1;
            err.currentChunk = 0;
            err.totalChunks = 0;
            err.isSending = true;
            if (progressCallback) progressCallback(err);
            return;
        }

        uint64_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        uint32_t transferId = m_nextTransferId++;
        uint32_t totalChunks = Protocol::CalculateChunks(fileSize);

        // 发送文件元信息
        FileMeta meta;
        meta.fileName = fileName;
        meta.fileSize = fileSize;
        meta.transferId = transferId;
        meta.totalChunks = totalChunks;

        auto metaData = Protocol::EncodeFileMeta(meta);
        if (!NetUtils::SendAll(targetSock, metaData.data(), (int)metaData.size()))
        {
            FileProgressInfo err;
            err.fileName = fileName;
            err.progress = -1;
            err.currentChunk = 0;
            err.totalChunks = totalChunks;
            err.isSending = true;
            if (progressCallback) progressCallback(err);
            file.close();
            return;
        }

        // 逐块发送文件数据
        std::vector<uint8_t> buffer(CHUNK_SIZE);
        for (uint32_t i = 0; i < totalChunks; i++)
        {
            uint32_t toRead = (uint32_t)min((uint64_t)CHUNK_SIZE, fileSize - (uint64_t)i * CHUNK_SIZE);
            file.read((char*)buffer.data(), toRead);
            buffer.resize(toRead);

            auto chunkData = Protocol::EncodeFileData(transferId, i, totalChunks, buffer);

            if (!NetUtils::SendAll(targetSock, chunkData.data(), (int)chunkData.size()))
            {
                FileProgressInfo err;
                err.fileName = fileName;
                err.progress = -1;
                err.currentChunk = i;
                err.totalChunks = totalChunks;
                err.isSending = true;
                if (progressCallback) progressCallback(err);
                file.close();
                return;
            }

            // 计算进度
            int progress = (int)((i + 1) * 100 / totalChunks);
            if (progress > 100) progress = 100;

            FileProgressInfo prog;
            prog.fileName = fileName;
            prog.progress = progress;
            prog.currentChunk = i + 1;
            prog.totalChunks = totalChunks;
            prog.isSending = true;
            if (progressCallback) progressCallback(prog);

            buffer.resize(CHUNK_SIZE);
        }

        // 发送完成通知
        auto doneData = Protocol::EncodeFileComplete(transferId);
        NetUtils::SendAll(targetSock, doneData.data(), (int)doneData.size());

        file.close();
    }).detach();
}

void FileTransfer::SendFileDirect(const std::string& fileName, const std::string& filePath,
                                  ClientCore* clientCore,
                                  std::function<void(const FileProgressInfo&)> progressCallback)
{
    std::thread([this, fileName, filePath, clientCore, progressCallback]() {
        std::ifstream file(std::filesystem::path(Utf8ToWidePath(filePath)), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            FileProgressInfo err;
            err.fileName = fileName;
            err.progress = -1;
            if (progressCallback) progressCallback(err);
            return;
        }

        uint64_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        uint32_t transferId = m_nextTransferId++;
        uint32_t totalChunks = Protocol::CalculateChunks(fileSize);

        FileMeta meta;
        meta.fileName = fileName;
        meta.fileSize = fileSize;
        meta.transferId = transferId;
        meta.totalChunks = totalChunks;

        auto metaData = Protocol::EncodeFileMeta(meta);
        if (!clientCore->SendData(metaData))
        {
            FileProgressInfo err;
            err.fileName = fileName;
            err.progress = -1;
            err.currentChunk = 0;
            err.totalChunks = totalChunks;
            err.isSending = true;
            if (progressCallback) progressCallback(err);
            file.close();
            return;
        }

        std::vector<uint8_t> buffer(CHUNK_SIZE);
        for (uint32_t i = 0; i < totalChunks; i++)
        {
            uint32_t toRead = (uint32_t)min((uint64_t)CHUNK_SIZE, fileSize - (uint64_t)i * CHUNK_SIZE);
            file.read((char*)buffer.data(), toRead);
            buffer.resize(toRead);

            auto chunkData = Protocol::EncodeFileData(transferId, i, totalChunks, buffer);
            if (!clientCore->SendData(chunkData))
            {
                FileProgressInfo err;
                err.fileName = fileName;
                err.progress = -1;
                err.currentChunk = i;
                err.totalChunks = totalChunks;
                err.isSending = true;
                if (progressCallback) progressCallback(err);
                file.close();
                return;
            }

            int progress = (int)((i + 1) * 100 / totalChunks);
            if (progress > 100) progress = 100;

            FileProgressInfo prog;
            prog.fileName = fileName;
            prog.progress = progress;
            prog.currentChunk = i + 1;
            prog.totalChunks = totalChunks;
            prog.isSending = true;
            if (progressCallback) progressCallback(prog);

            buffer.resize(CHUNK_SIZE);
        }

        auto doneData = Protocol::EncodeFileComplete(transferId);
        clientCore->SendData(doneData);

        file.close();
    }).detach();
}

void FileTransfer::ReceiveFileStart(const FileMeta& meta,
                                    std::function<void(const FileProgressInfo&)> progressCallback)
{
    std::string saveDir = GetDownloadPath();
    std::string savePath = saveDir + "\\" + meta.fileName;

    // 避免文件名冲突
    std::string origPath = savePath;
    int dupCount = 1;
    while (std::ifstream(savePath, std::ios::binary).good())
    {
        size_t dot = origPath.find_last_of('.');
        if (dot != std::string::npos)
        {
            savePath = origPath.substr(0, dot) + "(" + std::to_string(dupCount) + ")" + origPath.substr(dot);
        }
        else
        {
            savePath = origPath + "(" + std::to_string(dupCount) + ")";
        }
        dupCount++;
    }

    ReceiveState state;
    state.fileName = meta.fileName;
    state.savePath = savePath;
    state.fileSize = meta.fileSize;
    state.transferId = meta.transferId;
    state.totalChunks = meta.totalChunks;
    state.receivedChunks = 0;
    state.active = true;
    state.progressCallback = progressCallback;

    state.fileStream.open(std::filesystem::path(Utf8ToWidePath(savePath)), std::ios::binary | std::ios::trunc);
    if (!state.fileStream.is_open())
    {
        FileProgressInfo err;
        err.fileName = meta.fileName;
        err.progress = -1;
        if (progressCallback) progressCallback(err);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_receives[meta.transferId] = std::move(state);
    }
}

void FileTransfer::ReceiveFileData(uint32_t transferId, uint32_t chunkIndex,
                                   const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_receives.find(transferId);
    if (it == m_receives.end() || !it->second.active)
        return;

    auto& state = it->second;
    state.fileStream.write((const char*)data.data(), data.size());
    state.receivedChunks++;

    int progress = (int)(state.receivedChunks * 100 / state.totalChunks);
    if (progress > 100) progress = 100;

    FileProgressInfo prog;
    prog.fileName = state.fileName;
    prog.progress = progress;
    prog.currentChunk = state.receivedChunks;
    prog.totalChunks = state.totalChunks;
    prog.isSending = false;

    if (state.progressCallback)
        state.progressCallback(prog);
}

void FileTransfer::ReceiveFileComplete(uint32_t transferId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_receives.find(transferId);
    if (it == m_receives.end()) return;

    auto& state = it->second;
    if (state.fileStream.is_open())
        state.fileStream.close();

    state.active = false;

    FileProgressInfo prog;
    prog.fileName = state.fileName;
    prog.progress = 100;
    prog.currentChunk = state.totalChunks;
    prog.totalChunks = state.totalChunks;
    prog.isSending = false;

    if (state.progressCallback)
        state.progressCallback(prog);

    m_receives.erase(it);
}

void FileTransfer::CancelTransfer(uint32_t transferId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_receives.find(transferId);
    if (it == m_receives.end()) return;

    if (it->second.fileStream.is_open())
        it->second.fileStream.close();

    // 删除未完成的文件
    remove(it->second.savePath.c_str());
    m_receives.erase(it);
}
