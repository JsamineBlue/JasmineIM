#pragma once

#ifndef __DEF_H_
#define __DEF_H_

#define DEF_PROT_BASE 1000
//注册
#define DEF_PROT_REGISTER_RQ (DEF_PROT_BASE + 0)
#define DEF_PROT_REGISTER_RS (DEF_PROT_BASE + 1)
//登录
#define DEF_PPROT_LOGIN_RQ (DEF_PROT_BASE + 2)
#define DEF_PPROT_LOGIN_RS (DEF_PROT_BASE + 3)
//用户信息协议
#define DEF_PROT_FRIEND_INFO (DEF_PROT_BASE + 4)
//聊天请求
#define DEF_PROT_CHAT_INFO_RQ (DEF_PROT_BASE + 5)
//聊天回复
#define DEF_PROT_CHAT_INFO_RS (DEF_PROT_BASE + 6)
//添加好友请求
#define DEF_PROT_ADD_FRIEND_RQ (DEF_PROT_BASE + 7)
//添加好友回复
#define DEF_PROT_ADD_FRIEND_RS (DEF_PROT_BASE + 8)
//用户下线
#define DEF_PROT_FRIEND_OFFLINE (DEF_PROT_BASE + 9)

//注册结果
#define REGISTER_RESULT_SUCC    0
#define REGISTER_RESULT_FAIL    1

//登录结果
#define LOGIN_RESULT_SUCC    0
#define LOGIN_RESULT_NOEXIST    1   //用户不存在
#define LOGIN_RESULT_PASSERR    2   //密码错误

//离线、在线状态
#define FRIEND_STATUS_ONLINE    1   //在线
#define FRIEND_STATUS_OFFLINE   0   //离线

//发送内容最大长度
#define MSG_LEN   (1024*4)

//发送结果
#define CHAT_RESULT_SUCC    1
#define CHAT_RESULT_FAIL    0

//添加好友回复的结果
#define ADDFRI_RESULT_ACCEPT    0
#define ADDFRI_RESULT_REJECT    1
#define ADDFRI_RESULT_OFFLINE    2
#define ADDFRI_RESULT_NOEXIST    3

using prot_type = unsigned int;     //协议类型，4字节

//注册请求
struct PROT_REGISTER_RQ{
    prot_type prottype;     //协议类型
    char nick[30];     //昵称
    char tel[15];       //电话号码
    char pass[15];      //密码
    PROT_REGISTER_RQ():prottype(DEF_PROT_REGISTER_RQ),nick{0},tel{0},pass{0}{}
};

//注册回复
struct PROT_REGISTER_RS{
    prot_type prottype;     //协议类型
    int result;     //回复注册的结果
    PROT_REGISTER_RS():prottype(DEF_PROT_REGISTER_RS),result(REGISTER_RESULT_SUCC){}
};

//登录请求
struct PROT_LOGIN_RQ{
    prot_type prottype;
    char tel[15];
    char pass[15];
    PROT_LOGIN_RQ():prottype(DEF_PPROT_LOGIN_RQ),tel{0},pass{0}{}
};

//登录回复
struct PROT_LOGIN_RS{
    prot_type prottype;
    int userid;     //登录成功，会返回用户的id
    int result;
    PROT_LOGIN_RS():prottype(DEF_PPROT_LOGIN_RS),userid(0),result(LOGIN_RESULT_SUCC){}
};

//朋友信息协议
struct PROT_FRIEND_INFO{
    prot_type prottype;
    int friendid;     //朋友的id
    int iconid;     //头像id
    int status;     //在线状态：0 离线、1 在线
    char nick[30];      //昵称
    char feeling[100];  //个性签名
    PROT_FRIEND_INFO():prottype(DEF_PROT_FRIEND_INFO),friendid(0),iconid(0),status(FRIEND_STATUS_ONLINE),nick{0},feeling{0}{}
};

//聊天请求协议
struct PROT_CHAT_INFO_RQ{
    prot_type prottype;
    int myid;   //我的id
    int friendid;   //接收方id
    char msg[MSG_LEN];    //发送信息
    PROT_CHAT_INFO_RQ():prottype(DEF_PROT_CHAT_INFO_RQ),myid(0),friendid(0),msg{0}{}
};

//聊天回复协议
struct PROT_CHAT_INFO_RS{
    prot_type prottype;
    int myid;   //我的id
    int friendid;   //接收方id
    int result;    //发送结果   1：成功、0：失败
    PROT_CHAT_INFO_RS():prottype(DEF_PROT_CHAT_INFO_RS),myid(0),friendid(0),result(CHAT_RESULT_SUCC){}
};

//添加好友请求
struct PROT_ADD_FRIEND_RQ{
    prot_type prottype;
    int myid;   //我的id
    char mynick[30];   //我的昵称
    char frinick[30];    //要添加的朋友昵称
    PROT_ADD_FRIEND_RQ():prottype(DEF_PROT_ADD_FRIEND_RQ),myid(0),mynick{0},frinick{0}{}
};

//添加好友回复
struct PROT_ADD_FRIEND_RS{
    prot_type prottype;
    int result;     //添加结果  0：同意、1：拒绝、2：下线、3：不存在
    int destid;
    char destnick[30];
    int myid;
    char mynick[30];
    PROT_ADD_FRIEND_RS():prottype(DEF_PROT_ADD_FRIEND_RS),result(ADDFRI_RESULT_ACCEPT),destid(0),destnick{0},myid(0),mynick{0}{}
};

//下线
struct PROT_FRIEND_OFFLINE{
    prot_type prottype;
    int offUserid;      //下线用户id
    PROT_FRIEND_OFFLINE():prottype(DEF_PROT_FRIEND_OFFLINE),offUserid(0){}
};

#endif