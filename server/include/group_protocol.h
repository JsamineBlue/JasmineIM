#pragma once

#include <cstddef>
#include <cstdint>

// 群聊协议从 1100 开始，避免与已有协议冲突。
constexpr std::uint32_t DEF_PROT_GROUP_CREATE_RQ = 1100;
constexpr std::uint32_t DEF_PROT_GROUP_CREATE_RS = 1101;
constexpr std::uint32_t DEF_PROT_GROUP_INFO = 1102;
constexpr std::uint32_t DEF_PROT_GROUP_MEMBERS_RQ = 1103;
constexpr std::uint32_t DEF_PROT_GROUP_MEMBERS_RS = 1104;
constexpr std::uint32_t DEF_PROT_GROUP_INVITE_RQ = 1105;
constexpr std::uint32_t DEF_PROT_GROUP_INVITE_RS = 1106;
constexpr std::uint32_t DEF_PROT_GROUP_INVITE_NOTIFY = 1107;
constexpr std::uint32_t DEF_PROT_GROUP_INVITE_DECISION_RQ = 1108;
constexpr std::uint32_t DEF_PROT_GROUP_INVITE_DECISION_RS = 1109;
constexpr std::uint32_t DEF_PROT_GROUP_INVITE_RESULT_NOTIFY = 1110;
constexpr std::uint32_t DEF_PROT_GROUP_CHAT_RQ = 1111;
constexpr std::uint32_t DEF_PROT_GROUP_CHAT_RS = 1112;
constexpr std::uint32_t DEF_PROT_GROUP_CHAT_NOTIFY = 1113;
constexpr std::uint32_t DEF_PROT_GROUP_LEAVE_RQ = 1114;
constexpr std::uint32_t DEF_PROT_GROUP_LEAVE_RS = 1115;
constexpr std::uint32_t DEF_PROT_GROUP_MEMBER_LEFT = 1116;

constexpr std::size_t GROUP_NAME_BYTES = 128;

constexpr std::int32_t GROUP_CREATE_SUCC = 0;
constexpr std::int32_t GROUP_CREATE_FAIL = 1;
constexpr std::int32_t GROUP_CREATE_INVALID_NAME = 2;

constexpr std::size_t MAX_GROUP_MEMBERS = 64;

constexpr std::int32_t GROUP_MEMBERS_SUCC = 0;
constexpr std::int32_t GROUP_MEMBERS_FAIL = 1;
constexpr std::int32_t GROUP_MEMBERS_NOT_MEMBER = 2;
constexpr std::int32_t GROUP_MEMBERS_TOO_MANY = 3;
constexpr std::int32_t GROUP_MEMBERS_NOT_FOUND = 4;

constexpr std::int32_t GROUP_INVITE_SUCC = 0;
constexpr std::int32_t GROUP_INVITE_FAIL = 1;
constexpr std::int32_t GROUP_INVITE_NOT_OWNER = 2;
constexpr std::int32_t GROUP_INVITE_NOT_FRIEND = 3;
constexpr std::int32_t GROUP_INVITE_OFFLINE = 4;
constexpr std::int32_t GROUP_INVITE_ALREADY_MEMBER = 5;
constexpr std::int32_t GROUP_INVITE_FULL = 6;
constexpr std::int32_t GROUP_INVITE_NOT_FOUND = 7;
constexpr std::int32_t GROUP_INVITE_PENDING = 8;
constexpr std::int32_t GROUP_INVITE_EXPIRED = 9;
constexpr std::int32_t GROUP_INVITE_REJECTED = 10;

constexpr std::int32_t GROUP_INVITE_DECISION_REJECT = 0;
constexpr std::int32_t GROUP_INVITE_DECISION_ACCEPT = 1;

constexpr std::size_t GROUP_MESSAGE_BYTES = 4096;

constexpr std::size_t GROUP_CHAT_NICK_BYTES = 32;

constexpr std::int32_t GROUP_CHAT_SUCC = 0;
constexpr std::int32_t GROUP_CHAT_FAIL = 1;
constexpr std::int32_t GROUP_CHAT_NOT_MEMBER = 2;
constexpr std::int32_t GROUP_CHAT_NOT_FOUND = 3;
constexpr std::int32_t GROUP_CHAT_INVALID_MESSAGE = 4;

constexpr std::int32_t GROUP_LEAVE_SUCC = 0;
constexpr std::int32_t GROUP_LEAVE_FAIL = 1;
constexpr std::int32_t GROUP_LEAVE_NOT_MEMBER = 2;
constexpr std::int32_t GROUP_LEAVE_OWNER = 3;
constexpr std::int32_t GROUP_LEAVE_NOT_FOUND = 4;

struct PROT_GROUP_CREATE_RQ{
    std::uint32_t prottype = DEF_PROT_GROUP_CREATE_RQ;

    // 用于客户端匹配请求和回复。
    std::uint32_t requestId = 0;

    char groupName[GROUP_NAME_BYTES]{};
};

struct PROT_GROUP_CREATE_RS{
    std::uint32_t prottype = DEF_PROT_GROUP_CREATE_RS;
    std::uint32_t requestId = 0;

    std::int32_t result = GROUP_CREATE_FAIL;
    std::int32_t groupId = 0;
    std::int32_t ownerId = 0;

    char groupName[GROUP_NAME_BYTES]{};
};

struct PROT_GROUP_INFO{
    std::uint32_t prottype = DEF_PROT_GROUP_INFO;

    std::int32_t groupId = 0;
    std::int32_t ownerId = 0;

    char groupName[GROUP_NAME_BYTES]{};
};

struct PROT_GROUP_MEMBERS_RQ{
    std::uint32_t prottype = DEF_PROT_GROUP_MEMBERS_RQ;
    std::uint32_t requestId = 0;

    std::int32_t groupId = 0;
};

struct GROUP_MEMBER_INFO{
    std::int32_t userId = 0;
    std::int32_t iconId = 0;

    char nick[30]{};
};

struct PROT_GROUP_MEMBERS_RS{
    std::uint32_t prottype = DEF_PROT_GROUP_MEMBERS_RS;
    std::uint32_t requestId = 0;

    std::int32_t result = GROUP_MEMBERS_FAIL;
    std::int32_t groupId = 0;
    std::int32_t memberCount = 0;

    GROUP_MEMBER_INFO members[MAX_GROUP_MEMBERS]{};
};

struct PROT_GROUP_INVITE_RQ
{
    std::uint32_t prottype = DEF_PROT_GROUP_INVITE_RQ;
    std::uint32_t requestId = 0;

    std::int32_t groupId = 0;
    std::int32_t targetUserId = 0;
};

struct PROT_GROUP_INVITE_RS
{
    std::uint32_t prottype = DEF_PROT_GROUP_INVITE_RS;
    std::uint32_t requestId = 0;
    std::uint32_t invitationId = 0;

    std::int32_t groupId = 0;
    std::int32_t targetUserId = 0;
    std::int32_t result = GROUP_INVITE_FAIL;
};

struct PROT_GROUP_INVITE_NOTIFY
{
    std::uint32_t prottype = DEF_PROT_GROUP_INVITE_NOTIFY;
    std::uint32_t invitationId = 0;

    std::int32_t groupId = 0;
    std::int32_t inviterId = 0;

    char groupName[GROUP_NAME_BYTES]{};
    char inviterNick[30]{};
};

struct PROT_GROUP_INVITE_DECISION_RQ
{
    std::uint32_t prottype =
        DEF_PROT_GROUP_INVITE_DECISION_RQ;

    std::uint32_t requestId = 0;
    std::uint32_t invitationId = 0;

    std::int32_t decision = GROUP_INVITE_DECISION_REJECT;
};

struct PROT_GROUP_INVITE_DECISION_RS
{
    std::uint32_t prottype =
        DEF_PROT_GROUP_INVITE_DECISION_RS;

    std::uint32_t requestId = 0;
    std::uint32_t invitationId = 0;

    std::int32_t groupId = 0;
    std::int32_t result = GROUP_INVITE_FAIL;
};

struct PROT_GROUP_INVITE_RESULT_NOTIFY
{
    std::uint32_t prottype =
        DEF_PROT_GROUP_INVITE_RESULT_NOTIFY;

    std::uint32_t invitationId = 0;

    std::int32_t groupId = 0;
    std::int32_t targetUserId = 0;
    std::int32_t result = GROUP_INVITE_FAIL;
};

struct PROT_GROUP_CHAT_RQ {
    std::uint32_t prottype = DEF_PROT_GROUP_CHAT_RQ;
    std::uint32_t requestId = 0;

    std::int32_t groupId = 0;

    std::uint32_t contentLength = 0;
    char content[GROUP_MESSAGE_BYTES]{};
};

struct PROT_GROUP_CHAT_RS {
    std::uint32_t prottype = DEF_PROT_GROUP_CHAT_RS;
    std::uint32_t requestId = 0;

    std::int32_t groupId = 0;
    std::int32_t result = GROUP_CHAT_FAIL;

    std::int32_t forwardedCount = 0;
};

struct PROT_GROUP_CHAT_NOTIFY {
    std::uint32_t prottype = DEF_PROT_GROUP_CHAT_NOTIFY;
    std::uint32_t requestId = 0;

    std::int32_t groupId = 0;
    std::int32_t senderId = 0;

    std::uint32_t contentLength = 0;

    char senderNick[GROUP_CHAT_NICK_BYTES]{};
    char content[GROUP_MESSAGE_BYTES]{};
};

struct PROT_GROUP_LEAVE_RQ {
    std::uint32_t prottype = DEF_PROT_GROUP_LEAVE_RQ;
    std::uint32_t requestId = 0;
    std::int32_t groupId = 0;
};

struct PROT_GROUP_LEAVE_RS {
    std::uint32_t prottype = DEF_PROT_GROUP_LEAVE_RS;
    std::uint32_t requestId = 0;
    std::int32_t groupId = 0;
    std::int32_t result = GROUP_LEAVE_FAIL;
};

struct PROT_GROUP_MEMBER_LEFT {
    std::uint32_t prottype = DEF_PROT_GROUP_MEMBER_LEFT;
    std::int32_t groupId = 0;
    std::int32_t userId = 0;
};

static_assert(sizeof(PROT_GROUP_CREATE_RQ) == 136);
static_assert(offsetof(PROT_GROUP_CREATE_RQ, groupName) == 8);

static_assert(sizeof(PROT_GROUP_CREATE_RS) == 148);
static_assert(offsetof(PROT_GROUP_CREATE_RS, groupName) == 20);

static_assert(sizeof(PROT_GROUP_INFO) == 140);
static_assert(offsetof(PROT_GROUP_INFO, groupName) == 12);

static_assert(sizeof(PROT_GROUP_MEMBERS_RQ) == 12);

static_assert(sizeof(GROUP_MEMBER_INFO) == 40);
static_assert(offsetof(GROUP_MEMBER_INFO, nick) == 8);

static_assert(sizeof(PROT_GROUP_MEMBERS_RS) == 2580);
static_assert(offsetof(PROT_GROUP_MEMBERS_RS, members) == 20);

static_assert(sizeof(PROT_GROUP_INVITE_RQ) == 16);
static_assert(sizeof(PROT_GROUP_INVITE_RS) == 24);

static_assert(sizeof(PROT_GROUP_INVITE_NOTIFY) == 176);
static_assert(offsetof(PROT_GROUP_INVITE_NOTIFY, groupName) == 16);
static_assert(offsetof(PROT_GROUP_INVITE_NOTIFY, inviterNick) == 144);

static_assert(sizeof(PROT_GROUP_INVITE_DECISION_RQ) == 16);
static_assert(sizeof(PROT_GROUP_INVITE_DECISION_RS) == 20);
static_assert(sizeof(PROT_GROUP_INVITE_RESULT_NOTIFY) == 20);

static_assert(sizeof(PROT_GROUP_CHAT_RQ) == 4112);
static_assert(offsetof(PROT_GROUP_CHAT_RQ, content) == 16);

static_assert(sizeof(PROT_GROUP_CHAT_RS) == 20);

static_assert(sizeof(PROT_GROUP_CHAT_NOTIFY) == 4148);
static_assert(offsetof(PROT_GROUP_CHAT_NOTIFY, senderNick) == 20);
static_assert(offsetof(PROT_GROUP_CHAT_NOTIFY, content) == 52);

static_assert(sizeof(PROT_GROUP_LEAVE_RQ) == 12);
static_assert(sizeof(PROT_GROUP_LEAVE_RS) == 16);
static_assert(sizeof(PROT_GROUP_MEMBER_LEFT) == 12);
