#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::uint32_t DEF_PROT_BASE = 1000;

constexpr std::uint32_t DEF_PROT_REGISTER_RQ = DEF_PROT_BASE+0;
constexpr std::uint32_t DEF_PROT_REGISTER_RS = DEF_PROT_BASE+1;
constexpr std::uint32_t DEF_PPROT_LOGIN_RQ = DEF_PROT_BASE+2;
constexpr std::uint32_t DEF_PPROT_LOGIN_RS = DEF_PROT_BASE+3;
constexpr std::uint32_t DEF_PROT_FRIEND_INFO = DEF_PROT_BASE+4;
constexpr std::uint32_t DEF_PROT_CHAT_INFO_RQ = DEF_PROT_BASE+5;
constexpr std::uint32_t DEF_PROT_CHAT_INFO_RS = DEF_PROT_BASE+6;
constexpr std::uint32_t DEF_PROT_ADD_FRIEND_RQ = DEF_PROT_BASE+7;
constexpr std::uint32_t DEF_PROT_ADD_FRIEND_RS = DEF_PROT_BASE+8;
constexpr std::uint32_t DEF_PROT_FRIEND_OFFLINE = DEF_PROT_BASE+9;
constexpr std::uint32_t DEF_PROT_DELETE_FRIEND_RQ = 1010;
constexpr std::uint32_t DEF_PROT_DELETE_FRIEND_RS = 1011;
constexpr std::uint32_t DEF_PROT_DELETE_FRIEND_NOTIFY = 1012;
constexpr std::uint32_t DEF_PROT_UPDATE_PROFILE_RQ = 1013;
constexpr std::uint32_t DEF_PROT_UPDATE_PROFILE_RS = 1014;
constexpr std::uint32_t DEF_PROT_CHANGE_PASSWORD_RQ = 1015;
constexpr std::uint32_t DEF_PROT_CHANGE_PASSWORD_RS = 1016;

//注册结果
constexpr std::int32_t REGISTER_RESULT_SUCC = 0;
constexpr std::int32_t REGISTER_RESULT_FAIL_NICK = 1;
constexpr std::int32_t REGISTER_RESULT_FAIL_TEL = 2;

//登录结果
constexpr std::int32_t LOGIN_RESULT_SUCC = 0;
constexpr std::int32_t LOGIN_RESULT_NOEXIST = 1;
constexpr std::int32_t LOGIN_RESULT_PASSERR = 2;
constexpr std::int32_t LOGIN_RESULT_ALREADY_ONLINE = 3;
constexpr std::int32_t LOGIN_RESULT_CONNECTION_LOGGED_IN = 4;

//在线状态
constexpr std::uint32_t FRIEND_STATUS_ONLINE = 1;
constexpr std::uint32_t FRIEND_STATUS_OFFLINE = 0;

constexpr std::size_t MSG_LEN = 1024*4;

//聊天结果
constexpr std::int32_t CHAT_RESULT_SUCC = 1;
constexpr std::int32_t CHAT_RESULT_FAIL = 0;

//添加好友结果
constexpr std::int32_t ADDFRI_RESULT_ACCEPT = 0;
constexpr std::int32_t ADDFRI_RESULT_REJECT = 1;
constexpr std::int32_t ADDFRI_RESULT_OFFLINE = 2;
constexpr std::int32_t ADDFRI_RESULT_NOEXIST = 3;
constexpr std::int32_t ADDFRI_RESULT_SELF = 4;
constexpr std::int32_t ADDFRI_RESULT_ALREADY_FRIEND = 5;
constexpr std::int32_t ADDFRI_RESULT_PENDING = 6;
constexpr std::int32_t ADDFRI_RESULT_BUSY = 7;

//删除好友结果
constexpr std::int32_t DELETE_FRIEND_SUCC = 0;
constexpr std::int32_t DELETE_FRIEND_FAIL = 1;
constexpr std::int32_t DELETE_FRIEND_NOT_FRIEND = 2;

constexpr std::int32_t UPDATE_PROFILE_SUCC = 0;
constexpr std::int32_t UPDATE_PROFILE_FAIL = 1;
constexpr std::int32_t UPDATE_PROFILE_NICK_EXISTS = 2;
constexpr std::int32_t UPDATE_PROFILE_INVALID = 3;

constexpr std::int32_t CHANGE_PASSWORD_SUCC = 0;
constexpr std::int32_t CHANGE_PASSWORD_FAIL = 1;
constexpr std::int32_t CHANGE_PASSWORD_OLD_WRONG = 2;
constexpr std::int32_t CHANGE_PASSWORD_INVALID = 3;
constexpr std::int32_t CHANGE_PASSWORD_SAME = 4;

using prot_type = std::uint32_t;

struct PROT_REGISTER_RQ{
    prot_type prottype = DEF_PROT_REGISTER_RQ;
    char nick[30]{};
    char tel[15]{};
    char pass[15]{};
};

struct PROT_REGISTER_RS{
    prot_type prottype = DEF_PROT_REGISTER_RS;
    std::int32_t result = REGISTER_RESULT_SUCC;
};

struct PROT_LOGIN_RQ{
    prot_type prottype = DEF_PPROT_LOGIN_RQ;
    char tel[15]{};
    char pass[15]{};
};

struct PROT_LOGIN_RS{
    prot_type prottype = DEF_PPROT_LOGIN_RS;
    std::int32_t userid = 0;
    std::int32_t result = LOGIN_RESULT_SUCC;
};

struct PROT_FRIEND_INFO{
    prot_type prottype = DEF_PROT_FRIEND_INFO;

    std::int32_t friendid = 0;
    std::int32_t iconid = 0;
    std::int32_t status = FRIEND_STATUS_ONLINE;

    char nick[30];
    char feeling[100]{};
};

struct PROT_FRIEND_OFFLINE{
    prot_type prottype = DEF_PROT_FRIEND_OFFLINE;
    std::int32_t offUserid = 0;
};

struct PROT_CHAT_INFO_RQ{
    prot_type prottype = DEF_PROT_CHAT_INFO_RQ;
    std::int32_t myid = 0;
    std::int32_t friendid = 0;
    char msg[MSG_LEN]{};
};

struct PROT_CHAT_INFO_RS{
    prot_type prottype = DEF_PROT_CHAT_INFO_RS;
    std::int32_t myid = 0;
    std::int32_t friendid = 0;
    std::int32_t result = CHAT_RESULT_FAIL;
};

struct PROT_ADD_FRIEND_RQ{
    prot_type prottype = DEF_PROT_ADD_FRIEND_RQ;

    std::int32_t myid = 0;

    char mynick[30]{};
    char frinick[30]{};
};

struct PROT_ADD_FRIEND_RS{
    prot_type prottype = DEF_PROT_ADD_FRIEND_RS;

    std::int32_t result = ADDFRI_RESULT_ACCEPT;

    std::int32_t destid = 0;
    char destnick[30]{};

    std::int32_t myid = 0;
    char mynick[30]{};
};

// 请求删除好友
// 当前用户身份由服务端登录会话确定，不由客户端填写
struct PROT_DELETE_FRIEND_RQ {
    std::uint32_t prottype = DEF_PROT_DELETE_FRIEND_RQ;
    std::uint32_t requestId = 0;

    std::int32_t friendId = 0;
};

// 服务端回复发起删除的一方
struct PROT_DELETE_FRIEND_RS {
    std::uint32_t prottype = DEF_PROT_DELETE_FRIEND_RS;
    std::uint32_t requestId = 0;

    std::int32_t friendId = 0;
    std::int32_t result = DELETE_FRIEND_FAIL;
};

// 服务端通知另一方：与这个用户的好友关系已经解除
struct PROT_DELETE_FRIEND_NOTIFY {
    std::uint32_t prottype = DEF_PROT_DELETE_FRIEND_NOTIFY;

    std::int32_t friendId = 0;
};

// 当前用户身份由服务端登录会话确定
struct PROT_UPDATE_PROFILE_RQ {
    std::uint32_t prottype = DEF_PROT_UPDATE_PROFILE_RQ;
    std::uint32_t requestId = 0;

    std::int32_t iconId = 0;
    char nick[30]{};
    char feeling[100]{};
};

// 返回服务端确认后的资料
struct PROT_UPDATE_PROFILE_RS {
    std::uint32_t prottype = DEF_PROT_UPDATE_PROFILE_RS;
    std::uint32_t requestId = 0;

    std::int32_t result = UPDATE_PROFILE_FAIL;
    std::int32_t iconId = 0;

    char nick[30]{};
    char feeling[100]{};
};

struct PROT_CHANGE_PASSWORD_RQ {
    std::uint32_t prottype = DEF_PROT_CHANGE_PASSWORD_RQ;
    std::uint32_t requestId = 0;

    char oldPassword[15]{};
    char newPassword[15]{};
};

struct PROT_CHANGE_PASSWORD_RS {
    std::uint32_t prottype = DEF_PROT_CHANGE_PASSWORD_RS;
    std::uint32_t requestId = 0;

    std::int32_t result = CHANGE_PASSWORD_FAIL;
};

//检查当前编译环境下的结构体布局
static_assert(sizeof(PROT_REGISTER_RQ)== 64);
static_assert(offsetof(PROT_REGISTER_RQ,nick)== 4);
static_assert(offsetof(PROT_REGISTER_RQ,tel)== 34);
static_assert(offsetof(PROT_REGISTER_RQ,pass)== 49);

static_assert(sizeof(PROT_REGISTER_RS)== 8);
static_assert(offsetof(PROT_REGISTER_RS,result)== 4);

static_assert(sizeof(PROT_LOGIN_RQ)==36);
static_assert(offsetof(PROT_LOGIN_RQ,tel)== 4);
static_assert(offsetof(PROT_LOGIN_RQ,pass)== 19);

static_assert(sizeof(PROT_LOGIN_RS)== 12);;
static_assert(offsetof(PROT_LOGIN_RS,userid)== 4);
static_assert(offsetof(PROT_LOGIN_RS,result)== 8);

static_assert(sizeof(PROT_FRIEND_INFO)== 148);
static_assert(offsetof(PROT_FRIEND_INFO,nick)== 16);
static_assert(offsetof(PROT_FRIEND_INFO,feeling)== 46);

static_assert(sizeof(PROT_FRIEND_OFFLINE)== 8);

static_assert(sizeof(PROT_CHAT_INFO_RQ)== 4108);
static_assert(offsetof(PROT_CHAT_INFO_RQ,msg)== 12);

static_assert(sizeof(PROT_CHAT_INFO_RS)== 16);

static_assert(sizeof(PROT_ADD_FRIEND_RQ)== 68);
static_assert(offsetof(PROT_ADD_FRIEND_RQ,myid)== 4);
static_assert(offsetof(PROT_ADD_FRIEND_RQ,mynick)== 8);
static_assert(offsetof(PROT_ADD_FRIEND_RQ,frinick)== 38);

static_assert(sizeof(PROT_ADD_FRIEND_RS)== 80);
static_assert(offsetof(PROT_ADD_FRIEND_RS,result)== 4);
static_assert(offsetof(PROT_ADD_FRIEND_RS,destid)== 8);
static_assert(offsetof(PROT_ADD_FRIEND_RS,destnick)== 12);
static_assert(offsetof(PROT_ADD_FRIEND_RS,myid)== 44);
static_assert(offsetof(PROT_ADD_FRIEND_RS,mynick)== 48);

static_assert(sizeof(PROT_DELETE_FRIEND_RQ) == 12);
static_assert(sizeof(PROT_DELETE_FRIEND_RS) == 16);
static_assert(sizeof(PROT_DELETE_FRIEND_NOTIFY) == 8);

static_assert(sizeof(PROT_UPDATE_PROFILE_RQ) == 144);
static_assert(sizeof(PROT_UPDATE_PROFILE_RS) == 148);

static_assert(sizeof(PROT_CHANGE_PASSWORD_RQ) == 40);
static_assert(sizeof(PROT_CHANGE_PASSWORD_RS) == 12);
