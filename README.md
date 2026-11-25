# IM 即时通信系统

基于 Qt Widgets 和 Linux C++17 实现的即时通信学习项目。

客户端运行于 Windows，服务端采用 epoll、线程池和 MySQL 连接池。



## 功能

- 用户注册、登录、重复登录限制和退出登录

- 修改昵称、默认头像、个性签名和密码

- 好友申请、接受、拒绝、删除及上下线通知

- 在线好友私聊

- 创建群、邀请入群、群成员查询和普通成员退群

- 群内在线消息转发

- 在线好友文件传输：接受、拒绝、分块传输、SHA-256 校验和保存



文件传输支持最大 64 MiB 文件，每块最多 16 KiB。

接收端使用 QSaveFile，校验通过后提交保存。



## 项目结构

```text
client/    Qt 客户端源码、界面及资源
server/    Linux 服务端源码
```



## 开发环境

- 客户端：Windows、Qt 5.15.2、MinGW 64-bit、C++17

- 服务端：Linux、GCC、C++17

- 数据库：MySQL 8.0

- 密码哈希：libsodium Argon2id

客户端使用 Qt Widgets 和 Qt Concurrent 模块，并链接 Windows Winsock。



## 客户端构建

使用 Qt Creator 打开：

```text
client/04\_IMClient.pro
```

配置 Qt 5.15.2 MinGW 64-bit Kit，执行 qmake 后构建运行。



服务器地址配置位于：

```text
client/net/TCP\_Client.cpp
```



TCP 端口定义位于：

```text
client/net/def.h
```

请将服务器 IP 修改为实际部署地址。



## 服务端构建

需要安装 MySQL 客户端开发库和 libsodium 开发库。

在 server 目录执行：

```bash
g++ -std=c++17 -Wall -Wextra -pthread -Iinclude \\
&#x20;   src/main.cpp src/server.cpp \\
&#x20;   src/epoll\_server.cpp src/thread\_pool.cpp \\
&#x20;   src/dispatcher.cpp src/mysql\_pool.cpp \\
&#x20;   src/password\_hash.cpp src/session\_manager.cpp \\
&#x20;   -o im\_server -lmysqlclient -lsodium
```



## 数据库及启动配置

项目使用以下数据表：

- t\_user

- t\_friend

- t\_group

- t\_group\_member



数据库地址、端口、用户名、库名和服务端监听端口配置在：

```text
server/src/main.cpp
```



请根据自己的环境修改，并确保数据库结构已创建。

数据库密码通过环境变量提供。在启动服务端的同一个 Bash 终端执行：

```bash
read -s -p "MySQL password: " IM_DB_PASSWORD
printf '\n'
export IM_DB_PASSWORD
./im_server
```



默认服务端监听 TCP 14790 端口。

建表结构位于 database/schema.sql。
请先创建 im 数据库，再将该结构文件导入其中。
该文件不包含用户数据。



## 已验证场景

- 注册、登录、资料修改、密码修改

- 好友申请接受与拒绝、好友删除

- 群邀请接受与拒绝、成员退群

- 两人群聊、三人群聊

- 小文件、多块文件、空文件传输



## 当前限制

- 不支持离线消息、离线文件和聊天记录持久化

- 不支持断点续传、主动取消按钮和可视化文件传输进度

- 不支持群文件、群主转让和解散群

- 网络协议沿用固定结构体布局及小端格式，两端需保持一致

- 当前未实现 TLS，适合本地或受控网络中的学习测试

