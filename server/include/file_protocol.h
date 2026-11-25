#pragma once

#include <cstddef>
#include <cstdint>

// 文件申请协议
constexpr std::uint32_t DEF_PROT_FILE_OFFER_RQ = 1200;
constexpr std::uint32_t DEF_PROT_FILE_OFFER_RS = 1201;
constexpr std::uint32_t DEF_PROT_FILE_OFFER_NOTIFY = 1202;
constexpr std::uint32_t DEF_PROT_FILE_END_NOTIFY = 1203;
constexpr std::uint32_t DEF_PROT_FILE_DECISION_RQ = 1204;
constexpr std::uint32_t DEF_PROT_FILE_DECISION_RS = 1205;
constexpr std::uint32_t DEF_PROT_FILE_START_NOTIFY = 1206;
constexpr std::uint32_t DEF_PROT_FILE_CHUNK_RQ = 1207;
constexpr std::uint32_t DEF_PROT_FILE_CHUNK_NOTIFY = 1208;
constexpr std::uint32_t DEF_PROT_FILE_CHUNK_ACK = 1209;
constexpr std::uint32_t DEF_PROT_FILE_CHUNK_RS = 1210;
constexpr std::uint32_t DEF_PROT_FILE_FINISH_RQ = 1211;
constexpr std::uint32_t DEF_PROT_FILE_FINISH_NOTIFY = 1212;
constexpr std::uint32_t DEF_PROT_FILE_FINISH_RS = 1213;

constexpr std::uint64_t MAX_TRANSFER_FILE_BYTES = 64ULL * 1024 * 1024;

constexpr std::size_t FILE_CHUNK_BYTES = 16 * 1024;

// UTF-8 文件名，包含末尾 '\0'，最多 255 字节
constexpr std::size_t FILE_NAME_BYTES = 256;

constexpr std::size_t FILE_SENDER_NICK_BYTES = 32;

// 保存原始摘要字节，不是 64 字符的十六进制文本
constexpr std::size_t FILE_SHA256_BYTES = 32;

// 文件传输结果
constexpr std::int32_t FILE_RESULT_SUCC = 0;
constexpr std::int32_t FILE_RESULT_FAIL = 1;
constexpr std::int32_t FILE_RESULT_NOT_FRIEND = 2;
constexpr std::int32_t FILE_RESULT_OFFLINE = 3;
constexpr std::int32_t FILE_RESULT_INVALID = 4;
constexpr std::int32_t FILE_RESULT_BUSY = 5;
constexpr std::int32_t FILE_RESULT_REJECTED = 6;
constexpr std::int32_t FILE_RESULT_EXPIRED = 7;
constexpr std::int32_t FILE_RESULT_CANCELLED = 8;
constexpr std::int32_t FILE_RESULT_HASH_MISMATCH = 9;

// 接收方接收文件结果
constexpr std::int32_t FILE_DECISION_REJECT = 0;
constexpr std::int32_t FILE_DECISION_ACCEPT = 1;

// 发送者 -> 服务端：申请发送文件
struct PROT_FILE_OFFER_RQ {
    std::uint32_t prottype = DEF_PROT_FILE_OFFER_RQ;
    std::uint32_t requestId = 0;

    std::uint64_t fileSize = 0;
    std::int32_t targetUserId = 0;

    char fileName[FILE_NAME_BYTES]{};
    unsigned char sha256[FILE_SHA256_BYTES]{};
};

// 服务端 -> 发送者：申请是否成功转发
// SUCC 只表示申请已转发，不表示对方已经接受
struct PROT_FILE_OFFER_RS {
    std::uint32_t prottype = DEF_PROT_FILE_OFFER_RS;
    std::uint32_t requestId = 0;

    // 服务端生成，0 表示无有效传输编号
    std::uint32_t transferId = 0;
    std::int32_t result = FILE_RESULT_FAIL;
};

// 服务端 -> 接收者：收到文件传输申请
struct PROT_FILE_OFFER_NOTIFY {
    std::uint32_t prottype = DEF_PROT_FILE_OFFER_NOTIFY;
    std::uint32_t transferId = 0;

    std::uint64_t fileSize = 0;
    std::int32_t senderId = 0;

    char senderNick[FILE_SENDER_NICK_BYTES]{};
    char fileName[FILE_NAME_BYTES]{};
    unsigned char sha256[FILE_SHA256_BYTES]{};
};

struct PROT_FILE_END_NOTIFY {
    std::uint32_t prottype = DEF_PROT_FILE_END_NOTIFY;
    std::uint32_t transferId = 0;

    std::int32_t result = FILE_RESULT_FAIL;

    // 对方的用户 ID，由服务端填写
    std::int32_t peerUserId = 0;
};

// 接收者 -> 服务端
struct PROT_FILE_DECISION_RQ {
    std::uint32_t prottype = DEF_PROT_FILE_DECISION_RQ;
    std::uint32_t requestId = 0;
    std::uint32_t transferId = 0;

    std::int32_t decision = FILE_DECISION_REJECT;
};

struct PROT_FILE_DECISION_RS {
    std::uint32_t prottype = DEF_PROT_FILE_DECISION_RS;
    std::uint32_t requestId = 0;
    std::uint32_t transferId = 0;

    // SUCC 表示接受决定已确认，不表示文件已接收完成
    std::int32_t result = FILE_RESULT_FAIL;
};

// 服务端 -> 发送者：接收者已接受，可以开始传输
struct PROT_FILE_START_NOTIFY {
    std::uint32_t prottype = DEF_PROT_FILE_START_NOTIFY;
    std::uint32_t transferId = 0;

    std::int32_t targetUserId = 0;
};

// 发送者 -> 服务端
struct PROT_FILE_CHUNK_RQ {
    std::uint32_t prottype = DEF_PROT_FILE_CHUNK_RQ;
    std::uint32_t transferId = 0;

    // 本块起始位置，从文件开头计算
    std::uint64_t offset = 0;

    // 本块有效字节数，范围 1～FILE_CHUNK_BYTES
    std::uint32_t dataLength = 0;

    unsigned char data[FILE_CHUNK_BYTES]{};
};

// 服务端 -> 接收者
struct PROT_FILE_CHUNK_NOTIFY {
    std::uint32_t prottype = DEF_PROT_FILE_CHUNK_NOTIFY;
    std::uint32_t transferId = 0;

    std::uint64_t offset = 0;
    std::uint32_t dataLength = 0;

    unsigned char data[FILE_CHUNK_BYTES]{};
};

// 接收者 -> 服务端：本块已写入临时文件
struct PROT_FILE_CHUNK_ACK {
    std::uint32_t prottype = DEF_PROT_FILE_CHUNK_ACK;
    std::uint32_t transferId = 0;

    std::uint64_t offset = 0;
    std::uint32_t dataLength = 0;

    std::int32_t result = FILE_RESULT_FAIL;
};

// 服务端 -> 发送者：本块处理结果
struct PROT_FILE_CHUNK_RS {
    std::uint32_t prottype = DEF_PROT_FILE_CHUNK_RS;
    std::uint32_t transferId = 0;

    std::uint64_t offset = 0;
    std::uint32_t dataLength = 0;

    std::int32_t result = FILE_RESULT_FAIL;
};

// 发送者 -> 服务端：所有数据块已得到确认
struct PROT_FILE_FINISH_RQ {
    std::uint32_t prottype = DEF_PROT_FILE_FINISH_RQ;
    std::uint32_t transferId = 0;
};

// 服务端 -> 接收者：检查完整文件并提交保存
struct PROT_FILE_FINISH_NOTIFY {
    std::uint32_t prottype = DEF_PROT_FILE_FINISH_NOTIFY;
    std::uint32_t transferId = 0;
};

// 接收者 -> 服务端：最终校验和保存结果
struct PROT_FILE_FINISH_RS {
    std::uint32_t prottype = DEF_PROT_FILE_FINISH_RS;
    std::uint32_t transferId = 0;

    std::int32_t result = FILE_RESULT_FAIL;
};

static_assert(sizeof(PROT_FILE_END_NOTIFY) == 16);

static_assert(sizeof(PROT_FILE_OFFER_RQ) == 312);
static_assert(offsetof(PROT_FILE_OFFER_RQ, fileSize) == 8);
static_assert(offsetof(PROT_FILE_OFFER_RQ, fileName) == 20);
static_assert(offsetof(PROT_FILE_OFFER_RQ, sha256) == 276);

static_assert(sizeof(PROT_FILE_OFFER_RS) == 16);

static_assert(sizeof(PROT_FILE_OFFER_NOTIFY) == 344);
static_assert(offsetof(PROT_FILE_OFFER_NOTIFY, fileSize) == 8);
static_assert(offsetof(PROT_FILE_OFFER_NOTIFY, senderNick) == 20);
static_assert(offsetof(PROT_FILE_OFFER_NOTIFY, fileName) == 52);
static_assert(offsetof(PROT_FILE_OFFER_NOTIFY, sha256) == 308);

static_assert(sizeof(PROT_FILE_END_NOTIFY) == 16);

static_assert(sizeof(PROT_FILE_DECISION_RQ) == 16);
static_assert(sizeof(PROT_FILE_DECISION_RS) == 16);

static_assert(sizeof(PROT_FILE_START_NOTIFY) == 12);

static_assert(sizeof(PROT_FILE_CHUNK_RQ) == 16408);
static_assert(offsetof(PROT_FILE_CHUNK_RQ, offset) == 8);
static_assert(offsetof(PROT_FILE_CHUNK_RQ, data) == 20);

static_assert(sizeof(PROT_FILE_CHUNK_NOTIFY) == 16408);
static_assert(offsetof(PROT_FILE_CHUNK_NOTIFY, offset) == 8);
static_assert(offsetof(PROT_FILE_CHUNK_NOTIFY, data) == 20);

static_assert(sizeof(PROT_FILE_CHUNK_ACK) == 24);
static_assert(offsetof(PROT_FILE_CHUNK_ACK, offset) == 8);

static_assert(sizeof(PROT_FILE_CHUNK_RS) == 24);
static_assert(offsetof(PROT_FILE_CHUNK_RS, offset) == 8);

static_assert(sizeof(PROT_FILE_FINISH_RQ) == 8);
static_assert(sizeof(PROT_FILE_FINISH_NOTIFY) == 8);
static_assert(sizeof(PROT_FILE_FINISH_RS) == 12);
