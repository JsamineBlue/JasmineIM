#include "dispatcher.h"
#include "mysql_pool.h"
#include "protocol.h"
#include "password_hash.h"
#include "group_protocol.h"

#include <cstdint>
#include <cstdio>
#include <mysql/field_types.h>
#include <mysql/mysql.h>
#include <optional>
#include <vector>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <cstring>
#include <mysql/mysqld_error.h>
#include <limits>

//线程资源管理辅助类
//对象创建时初始化，离开作用域时清理，即使发生异常也会清理
namespace{
    class MySqlThreadScope{
        public:
            MySqlThreadScope(){
                if(mysql_thread_init()!=0){
                    throw std::runtime_error("MySQL thread initialization failed");
                }
            }

            ~MySqlThreadScope(){
                mysql_thread_end();
            }

            MySqlThreadScope(const MySqlThreadScope&) = delete;
            MySqlThreadScope& operator=(const MySqlThreadScope&) = delete;
    };

    //添加固定数组字符串读取函数
    //只在数组范围内找结束符，避免客户端发来没有 '\0' 的数据时越界读取
    template<std::size_t N> std::string readFixedString(const char(&field)[N],const char* fieldName){
        const char* end = std::find(field,field+N,'\0');

        if(end == field+N){
            throw std::runtime_error(std::string(fieldName) + " is not null-terminated");
        }

        return std::string(field,end);
    }

    //负责生成注册回复消息体
    std::vector<char> makeRegisterReply(std::int32_t result){
        PROT_REGISTER_RS reply{};
        reply.result = result;

        std::vector<char> body(sizeof(reply));

        std::memcpy(body.data(),&reply,sizeof(reply));

        return body;
    }

    //负责生成登录回复消息体
    DispatchResult makeLoginReply(std::int32_t result,std::int32_t userId = 0){
        PROT_LOGIN_RS reply{};
        reply.result = result;
        reply.userid = userId;

        DispatchResult output;
        output.body.resize(sizeof(reply));

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        if(result == LOGIN_RESULT_SUCC){
            output.authenticatedUser = userId;
        }

        return output;
    }

    //使用参数化查询，检查电话或昵称是否存在
    bool userValueExists(MYSQL* mysql,const char* sql,const std::string& value){
        std::unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql),&mysql_stmt_close);

        if(!statement){
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if(mysql_stmt_prepare(statement.get(),sql,static_cast<unsigned long>(std::strlen(sql))) != 0){
            throw std::runtime_error(std::string("Prepare user lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        unsigned long length = static_cast<unsigned long>(value.size());

        MYSQL_BIND parameter{};
        parameter.buffer_type = MYSQL_TYPE_STRING;
        parameter.buffer = const_cast<char*>(value.data());
        parameter.buffer_length = length;
        parameter.length = &length;

        if(mysql_stmt_bind_param(statement.get(),&parameter) != 0){
            throw std::runtime_error(std::string("Bind user lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_execute(statement.get()) != 0){
            throw std::runtime_error(std::string("Execute user lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_store_result(statement.get()) != 0){
            throw std::runtime_error(std::string("Store user lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        return mysql_stmt_num_rows(statement.get()) > 0;
    }

    //查询个人信息辅助函数
    std::vector<char> loadUserInfo(MYSQL* mysql,std::int32_t userId){
        constexpr const char* SQL = "select nick,feeling,iconid from t_user where id = ? limit 1";

        std::unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql),&mysql_stmt_close);

        if(!statement){
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if(mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL)))!=0){
            throw std::runtime_error(std::string("Prepare self info failed: ")+ mysql_stmt_error(statement.get()));
        }

        MYSQL_BIND parameter{};
        parameter.buffer_type = MYSQL_TYPE_LONG;
        parameter.buffer = &userId;
        parameter.buffer_length = sizeof(userId);

        if(mysql_stmt_bind_param(statement.get(),&parameter)!= 0){
            throw std::runtime_error(std::string("Bind self info parameter failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_execute(statement.get())!= 0){
            throw std::runtime_error(std::string("Execute self info failed: ") + mysql_stmt_error(statement.get()));
        }

        PROT_FRIEND_INFO info{};
        info.friendid = userId;
        info.status = FRIEND_STATUS_ONLINE;

        unsigned long lengths[2]{};
        bool nullFields[3]{};

        MYSQL_BIND columns[3]{};

        columns[0].buffer_type = MYSQL_TYPE_STRING;
        columns[0].buffer = info.nick;
        columns[0].buffer_length = sizeof(info.nick);
        columns[0].length = &lengths[0];
        columns[0].is_null = &nullFields[0];

        columns[1].buffer_type = MYSQL_TYPE_STRING;
        columns[1].buffer = info.feeling;
        columns[1].buffer_length = sizeof(info.feeling);
        columns[1].length = &lengths[1];
        columns[1].is_null = &nullFields[1];

        columns[2].buffer_type = MYSQL_TYPE_LONG;
        columns[2].buffer = &info.iconid;
        columns[2].buffer_length = sizeof(info.iconid);
        columns[2].is_null = &nullFields[2];

        if(mysql_stmt_bind_result(statement.get(),columns) != 0){
            throw std::runtime_error(std::string("Bind self info result failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_store_result(statement.get()) != 0){
            throw std::runtime_error(std::string("Store self info failed: ")+ mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_num_rows(statement.get()) != 1){
            throw std::runtime_error("User disappeared during login");
        }

        if(mysql_stmt_fetch(statement.get()) != 0){
            throw std::runtime_error("Fetch self info failed or fields exceed buffer capacity");
        }

        if(nullFields[0] || lengths[0] >= sizeof(info.nick)){
            throw std::runtime_error("Invalid nickname length");
        }

        info.nick[lengths[0]] = '\0';

        if(nullFields[1]){
            std::memset(info.feeling,0,sizeof(info.feeling));
        }
        else{
            if(lengths[1] >= sizeof(info.feeling)){
                throw std::runtime_error("Feeling exceeds protocol capacity");
            }

            info.feeling[lengths[1]] = '\0';
        }

        if(nullFields[2]){
            info.iconid = 20;
        }

        std::vector<char> body(sizeof(info));

        std::memcpy(body.data(),&info,sizeof(info));

        return body;
    }

    //好友ID查询函数
    std::vector<std::int32_t> loadFriendIds(MYSQL* mysql,std::int32_t userId){
        constexpr const char* SQL = "select distinct f.idB from t_friend as f join t_user as u on u.id = f.idB where f.idA = ? and f.idB <> f.idA order by f.idB limit 257";

        std::unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql),&mysql_stmt_close);

        if(!statement){
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if(mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0){
            throw std::runtime_error(std::string("Prepare friend list failed: ") + mysql_stmt_error(statement.get()));
        }

        MYSQL_BIND parameter{};
        parameter.buffer_type = MYSQL_TYPE_LONG;
        parameter.buffer = &userId;
        parameter.buffer_length = sizeof(userId);

        if(mysql_stmt_bind_param(statement.get(),&parameter) != 0){
            throw std::runtime_error(std::string("Bind friend list parameter failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_execute(statement.get()) != 0){
            throw std::runtime_error(std::string("Excute friend list failed: ") + mysql_stmt_error(statement.get()));
        }

        long long friendId = 0;
        bool isNull = false;

        MYSQL_BIND column{};
        column.buffer_type = MYSQL_TYPE_LONGLONG;
        column.buffer = &friendId;
        column.buffer_length = sizeof(friendId);
        column.is_null = &isNull;

        if(mysql_stmt_bind_result(statement.get(),&column) != 0){
            throw std::runtime_error(std::string("Bind friend list result failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_store_result(statement.get()) != 0){
            throw std::runtime_error(std::string("Store friend list failed: ") + mysql_stmt_error(statement.get()));
        }

        //限制单次登录同步的好友数，超过时明确报错
        if(mysql_stmt_num_rows(statement.get()) > 256){
            throw std::runtime_error("Friend list exceeds current synchronization limit");
        }

        std::vector<std::int32_t> friendIds;

        while(true){
            int fetched = mysql_stmt_fetch(statement.get());

            if(fetched == MYSQL_NO_DATA){
                break;
            }

            if(fetched != 0){
                throw std::runtime_error(std::string("Fetch friend list failed: ") + mysql_stmt_error(statement.get()));
            }

            if(isNull || friendId <= 0 || friendId > std::numeric_limits<std::int32_t>::max()){
                throw std::runtime_error("Invalid friend ID");
            }

            friendIds.push_back(static_cast<std::int32_t>(friendId));
        }

        return friendIds;
    }

    //私聊回复函数
    DispatchResult makeChatReply(std::int32_t senderId,std::int32_t targetId,std::int32_t result){
        PROT_CHAT_INFO_RS reply{};

        //qt 那边用回复的 myid 查找聊天对象
        reply.myid = targetId;
        reply.friendid = senderId;
        reply.result = result;

        DispatchResult output;
        output.body.resize(sizeof(reply));

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        return output;
    }

    //好友关系查询函数
    bool friendshipExists(MYSQL* mysql,std::int32_t senderId,std::int32_t targetId){
        constexpr const char* SQL = "select 1 from t_friend where idA = ? and idB = ? limit 1";

        std::unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql),&mysql_stmt_close);

        if(!statement){
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if(mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0){
            throw std::runtime_error(std::string("Prepare friendship lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        MYSQL_BIND parameters[2]{};

        parameters[0].buffer_type = MYSQL_TYPE_LONG;
        parameters[0].buffer = &senderId;
        parameters[0].buffer_length = sizeof(senderId);

        parameters[1].buffer_type = MYSQL_TYPE_LONG;
        parameters[1].buffer = &targetId;
        parameters[1].buffer_length = sizeof(targetId);

        if(mysql_stmt_bind_param(statement.get(),parameters) != 0){
            throw std::runtime_error(std::string("Bind friendship lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_execute(statement.get()) != 0){
            throw std::runtime_error(std::string("Excute friendship lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_store_result(statement.get()) != 0){
            throw std::runtime_error(std::string("Store friendship lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        return mysql_stmt_num_rows(statement.get()) > 0;
    }

    //按昵称查询 ID 函数
    std::optional<std::int32_t> findUserIdByNick(MYSQL* mysql,const std::string& nick){
        constexpr const char* SQL = "select id from t_user where nick = ? limit 1";

        std::unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql),&mysql_stmt_close);

        if(!statement){
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if(mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0){
            throw std::runtime_error(std::string("Prepare nickname lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        unsigned long length = static_cast<unsigned long>(nick.size());

        MYSQL_BIND parameter{};
        parameter.buffer_type = MYSQL_TYPE_STRING;
        parameter.buffer = const_cast<char*>(nick.data());
        parameter.buffer_length = length;
        parameter.length = &length;

        if(mysql_stmt_bind_param(statement.get(),&parameter) != 0 || mysql_stmt_execute(statement.get()) != 0){
            throw std::runtime_error(std::string("Nickname lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        long long userId = 0;
        bool isNull = false;

        MYSQL_BIND column{};
        column.buffer_type = MYSQL_TYPE_LONGLONG;
        column.buffer = &userId;
        column.buffer_length = sizeof(userId);
        column.is_null = &isNull;

        if(mysql_stmt_bind_result(statement.get(),&column) != 0 || mysql_stmt_store_result(statement.get()) != 0){
            throw std::runtime_error(std::string("Read nickname lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_num_rows(statement.get()) == 0){
            return std::nullopt;
        }

        if(mysql_stmt_fetch(statement.get()) != 0){
            throw std::runtime_error(std::string("Fetch nickname lookup failed: ") + mysql_stmt_error(statement.get()));
        }

        if(isNull || userId <= 0 || userId > std::numeric_limits<std::int32_t>::max()){
            throw std::runtime_error("Invalid target user ID");
        }

        return static_cast<std::int32_t>(userId);
    }

    //添加好友数据库辅助函数
    void createFriendship(MYSQL* mysql,std::int32_t firstUserId,std::int32_t secondUserId){
        if(firstUserId <= 0 || secondUserId <= 0 || firstUserId == secondUserId){
            throw std::invalid_argument("Invalid friendship identities");
        }

        constexpr const char* SQL = "insert into t_friend (idA,idB) values (?,?),(?,?) on duplicate key update idA = idA";

        std::unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> statement (mysql_stmt_init(mysql),&mysql_stmt_close);

        if(!statement){
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if(mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0){
            throw std::runtime_error(std::string("Prepare friendship insert failed: ") + mysql_stmt_error(statement.get()));
        }

        //固定较小 ID 在前的顺序写入，减少并发时锁顺序不一致
        std::int32_t lowerId = std::min(firstUserId,secondUserId);
        std::int32_t higherId = std::max(firstUserId,secondUserId);

        std::int32_t values[4]{lowerId,higherId,higherId,lowerId};

        MYSQL_BIND parameters[4]{};

        for(std::size_t i = 0;i<4;i++){
            parameters[i].buffer_type = MYSQL_TYPE_LONG;
            parameters[i].buffer = &values[i];
            parameters[i].buffer_length = sizeof(values[i]);
        }

        if(mysql_stmt_bind_param(statement.get(),parameters) != 0){
            throw std::runtime_error(std::string("Bind friendship insert failed: ") + mysql_stmt_error(statement.get()));
        }

        if(mysql_stmt_execute(statement.get()) != 0){
            throw std::runtime_error(std::string("Execute friendship insert failed: ") + mysql_stmt_error(statement.get()));
        }
    }

    //校验群名函数
    bool isValidGroupName(const std::string& name){
        if (name.empty() || name.size() >= GROUP_NAME_BYTES) {
            return false;
        }

        std::size_t offset = 0;
        std::size_t characterCount = 0;
        bool hasVisibleCharacter = false;

        while (offset < name.size()) {
            unsigned char first = static_cast<unsigned char>(name[offset]);

            std::uint32_t codePoint = 0;
            std::uint32_t minimum = 0;
            std::size_t byteCount = 0;

            if (first <= 0x7f) {
                codePoint = first;
                minimum = 0;
                byteCount = 1;
            }
            else if (first >= 0xc2 && first <= 0xdf) {
                codePoint = first & 0x1f;
                minimum = 0x80;
                byteCount = 2;
            }
            else if (first >= 0xe0 && first <= 0xef) {
                codePoint = first & 0x0f;
                minimum = 0x800;
                byteCount = 3;
            }
            else if (first >= 0xf0 && first <= 0xf4) {
                codePoint = first & 0x07;
                minimum = 0x10000;
                byteCount = 4;
            }
            else {
                return false;
            }

            if (offset + byteCount > name.size()) {
                return false;
            }

            for (std::size_t i = 1; i < byteCount; ++i) {
                unsigned char next = static_cast<unsigned char>(name[offset + i]);

                if ((next & 0xc0) != 0x80) {
                    return false;
                }

                codePoint = (codePoint << 6) | (next & 0x3f);
            }

            // 拒绝过长编码、代理项和超出 Unicode 范围的值。
            if (codePoint < minimum || codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
                return false;
            }

            // 群名不允许控制字符。
            if (codePoint < 0x20 || (codePoint >= 0x7f && codePoint <= 0x9f)) {
                return false;
            }

            bool whitespace =
                codePoint == 0x20 ||
                codePoint == 0xa0 ||
                codePoint == 0x1680 ||
                (codePoint >= 0x2000 && codePoint <= 0x200a) ||
                codePoint == 0x2028 ||
                codePoint == 0x2029 ||
                codePoint == 0x202f ||
                codePoint == 0x205f ||
                codePoint == 0x3000;

            if (!whitespace) {
                hasVisibleCharacter = true;
            }

            ++characterCount;

            if (characterCount > 30) {
                return false;
            }

            offset += byteCount;
        }

        return hasVisibleCharacter;
    }

    //创建群回复函数
    DispatchResult makeGroupCreateReply(std::uint32_t requestId,std::int32_t result){
        PROT_GROUP_CREATE_RS reply{};
        reply.requestId = requestId;
        reply.result = result;

        DispatchResult output;
        output.body.resize(sizeof(reply));

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        return output;
    }

    //参数化执行辅助函数
    my_ulonglong executeGroupStatement(MYSQL* mysql,const char* sql,MYSQL_BIND* parameters){
        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

        if (!statement) {
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if (mysql_stmt_prepare(statement.get(),sql,static_cast<unsigned long>(std::strlen(sql))) != 0) {
            throw std::runtime_error(std::string("Prepare group statement failed: ") +mysql_stmt_error(statement.get()));
        }

        if (mysql_stmt_bind_param(statement.get(),parameters) != 0) {
            throw std::runtime_error(std::string("Bind group statement failed: ") + mysql_stmt_error(statement.get()));
        }

        if (mysql_stmt_execute(statement.get()) != 0) {
            throw std::runtime_error(std::string("Execute group statement failed: ") + mysql_stmt_error(statement.get()));
        }

        return mysql_stmt_insert_id(statement.get());
    }

    //群列表查询函数
    std::vector<std::vector<char>> loadJoinedGroups(MYSQL* mysql,std::int32_t userId){
        constexpr const char* SQL = "select g.id,g.name,g.owner_id from t_group_member as m join t_group as g on g.id = m.group_id where m.user_id = ? order by g.id limit 257";

        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

        if (!statement) {
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if (mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0) {
            throw std::runtime_error(std::string("Prepare group list failed: ") + mysql_stmt_error(statement.get()));
        }

        MYSQL_BIND parameter{};
        parameter.buffer_type = MYSQL_TYPE_LONG;
        parameter.buffer = &userId;
        parameter.buffer_length = sizeof(userId);

        if (mysql_stmt_bind_param(statement.get(), &parameter) != 0 || mysql_stmt_execute(statement.get()) != 0){
            throw std::runtime_error(std::string("Execute group list failed: ") + mysql_stmt_error(statement.get()));
        }

        PROT_GROUP_INFO info{};
        long long ownerId = 0;
        unsigned long nameLength = 0;
        bool nullFields[3]{};

        MYSQL_BIND columns[3]{};

        columns[0].buffer_type = MYSQL_TYPE_LONG;
        columns[0].buffer = &info.groupId;
        columns[0].buffer_length = sizeof(info.groupId);
        columns[0].is_null = &nullFields[0];

        columns[1].buffer_type = MYSQL_TYPE_STRING;
        columns[1].buffer = info.groupName;
        columns[1].buffer_length = sizeof(info.groupName);
        columns[1].length = &nameLength;
        columns[1].is_null = &nullFields[1];

        columns[2].buffer_type = MYSQL_TYPE_LONGLONG;
        columns[2].buffer = &ownerId;
        columns[2].buffer_length = sizeof(ownerId);
        columns[2].is_null = &nullFields[2];

        if (mysql_stmt_bind_result(statement.get(), columns) != 0 ||mysql_stmt_store_result(statement.get()) != 0) {
            throw std::runtime_error(std::string("Read group list failed: ") + mysql_stmt_error(statement.get()));
        }

        if (mysql_stmt_num_rows(statement.get()) > 256) {
            throw std::runtime_error("Group list exceeds current synchronization limit");
        }

        std::vector<std::vector<char>> groups;

        while (true) {
            int fetched = mysql_stmt_fetch(statement.get());

            if (fetched == MYSQL_NO_DATA) {
                break;
            }

            if (fetched != 0) {
                throw std::runtime_error(std::string("Fetch group list failed: ") + mysql_stmt_error(statement.get()));
            }

            if (nullFields[0] ||
                    nullFields[1] ||
                    nullFields[2] ||
                    info.groupId <= 0 ||
                    ownerId <= 0 ||
                    ownerId > std::numeric_limits<std::int32_t>::max() ||
                    nameLength == 0 ||
                    nameLength >= sizeof(info.groupName)) {
                throw std::runtime_error("Invalid stored group info");
            }

            info.ownerId = static_cast<std::int32_t>(ownerId);
            info.groupName[nameLength] = '\0';

            std::vector<char> body(sizeof(info));

            std::memcpy(body.data(),&info,sizeof(info));

            groups.push_back(std::move(body));
        }

        return groups;
    }

    //查询存在性辅助函数
    bool groupQueryHasRows(MYSQL* mysql,const char* sql,MYSQL_BIND* parameters){
        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

        if (!statement) {
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if (mysql_stmt_prepare(statement.get(),sql,static_cast<unsigned long>(std::strlen(sql))) != 0) {
            throw std::runtime_error(std::string("Prepare group check failed: ") + mysql_stmt_error(statement.get()));
        }

        if (mysql_stmt_bind_param(statement.get(), parameters) != 0 || mysql_stmt_execute(statement.get()) != 0 || mysql_stmt_store_result(statement.get()) != 0) {
            throw std::runtime_error(std::string("Execute group check failed: ") + mysql_stmt_error(statement.get()));
        }

        return mysql_stmt_num_rows(statement.get()) > 0;
    }

    // 检查数据库中是否允许发起这次群邀请。
    // 返回 SUCC 只表示数据库检查通过，在线状态稍后由事件线程检查。
    std::int32_t checkGroupInvite(MYSQL* mysql,std::int32_t groupId,std::int32_t inviterId,std::int32_t targetUserId) {
        constexpr const char* SQL =
            "SELECT g.owner_id, "
            "EXISTS("
            "  SELECT 1 FROM t_group_member AS target "
            "  WHERE target.group_id = g.id AND target.user_id = ?"
            "), "
            "("
            "  SELECT COUNT(*) FROM t_group_member AS member "
            "  WHERE member.group_id = g.id"
            ") "
            "FROM t_group AS g "
            "WHERE g.id = ?";

        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

        if (!statement) {
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if (mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0) {
            throw std::runtime_error(std::string("Prepare group invite check failed: ") + mysql_stmt_error(statement.get()));
        }

        // t_user.id 是 BIGINT，使用 64 位整数绑定。
        long long targetId = targetUserId;
        std::int32_t queryGroupId = groupId;

        MYSQL_BIND parameters[2]{};

        parameters[0].buffer_type = MYSQL_TYPE_LONGLONG;
        parameters[0].buffer = &targetId;
        parameters[0].buffer_length = sizeof(targetId);

        parameters[1].buffer_type = MYSQL_TYPE_LONG;
        parameters[1].buffer = &queryGroupId;
        parameters[1].buffer_length = sizeof(queryGroupId);

        if (mysql_stmt_bind_param(statement.get(), parameters) != 0 || mysql_stmt_execute(statement.get()) != 0) {
            throw std::runtime_error(std::string("Execute group invite check failed: ") + mysql_stmt_error(statement.get()));
        }

        long long ownerId = 0;
        long long alreadyMember = 0;
        long long memberCount = 0;

        MYSQL_BIND columns[3]{};

        columns[0].buffer_type = MYSQL_TYPE_LONGLONG;
        columns[0].buffer = &ownerId;
        columns[0].buffer_length = sizeof(ownerId);

        columns[1].buffer_type = MYSQL_TYPE_LONGLONG;
        columns[1].buffer = &alreadyMember;
        columns[1].buffer_length = sizeof(alreadyMember);

        columns[2].buffer_type = MYSQL_TYPE_LONGLONG;
        columns[2].buffer = &memberCount;
        columns[2].buffer_length = sizeof(memberCount);

        if (mysql_stmt_bind_result(statement.get(), columns) != 0 || mysql_stmt_store_result(statement.get()) != 0) {
            throw std::runtime_error(std::string("Read group invite check failed: ") + mysql_stmt_error(statement.get()));
        }

        // 没有查询到群，属于正常业务结果。
        if (mysql_stmt_num_rows(statement.get()) == 0) {
            return GROUP_INVITE_NOT_FOUND;
        }

        if (mysql_stmt_fetch(statement.get()) != 0) {
            throw std::runtime_error(std::string("Fetch group invite check failed: ") + mysql_stmt_error(statement.get()));
        }

        if (ownerId != inviterId) {
            return GROUP_INVITE_NOT_OWNER;
        }

        if (alreadyMember != 0) {
            return GROUP_INVITE_ALREADY_MEMBER;
        }

        if (memberCount >= static_cast<long long>(MAX_GROUP_MEMBERS)) {
            return GROUP_INVITE_FULL;
        }

        // 当前查询已经读取完毕，释放语句后再进行好友查询。
        statement.reset();

        if (!friendshipExists(mysql, inviterId, targetUserId)) {
            return GROUP_INVITE_NOT_FRIEND;
        }

        return GROUP_INVITE_SUCC;
    }

    // 为群邀请通知查询真实的群名称和群主昵称
    void loadGroupInvitationNames(MYSQL* mysql,PROT_GROUP_INVITE_NOTIFY& notification) {
        constexpr const char* SQL =
            "SELECT g.name, u.nick "
            "FROM t_group AS g "
            "JOIN t_user AS u ON u.id = g.owner_id "
            "WHERE g.id = ? AND g.owner_id = ?";

        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

        if (!statement) {
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if (mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0) {
            throw std::runtime_error(std::string("Prepare invitation names failed: ") + mysql_stmt_error(statement.get()));
        }

        std::int32_t groupId = notification.groupId;
        long long inviterId = notification.inviterId;

        MYSQL_BIND parameters[2]{};

        parameters[0].buffer_type = MYSQL_TYPE_LONG;
        parameters[0].buffer = &groupId;
        parameters[0].buffer_length = sizeof(groupId);

        parameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
        parameters[1].buffer = &inviterId;
        parameters[1].buffer_length = sizeof(inviterId);

        if (mysql_stmt_bind_param(statement.get(), parameters) != 0 || mysql_stmt_execute(statement.get()) != 0) {
            throw std::runtime_error(std::string("Query invitation names failed: ") + mysql_stmt_error(statement.get()));
        }

        // 清空数组，并保留最后一个字节作为字符串结束符。
        std::memset(notification.groupName,0,sizeof(notification.groupName));

        std::memset(notification.inviterNick,0,sizeof(notification.inviterNick));

        unsigned long groupNameLength = 0;
        unsigned long inviterNickLength = 0;

        MYSQL_BIND columns[2]{};

        columns[0].buffer_type = MYSQL_TYPE_STRING;
        columns[0].buffer = notification.groupName;
        columns[0].buffer_length = sizeof(notification.groupName) - 1;
        columns[0].length = &groupNameLength;

        columns[1].buffer_type = MYSQL_TYPE_STRING;
        columns[1].buffer = notification.inviterNick;
        columns[1].buffer_length = sizeof(notification.inviterNick) - 1;
        columns[1].length = &inviterNickLength;

        if (mysql_stmt_bind_result(statement.get(), columns) != 0 || mysql_stmt_store_result(statement.get()) != 0) {
            throw std::runtime_error(std::string("Read invitation names failed: ") + mysql_stmt_error(statement.get()));
        }

        if (mysql_stmt_num_rows(statement.get()) == 0) {
            throw std::runtime_error("Group or owner changed during invitation");
        }

        // 字段过长导致的 MYSQL_DATA_TRUNCATED 也会被拒绝。
        if (mysql_stmt_fetch(statement.get()) != 0) {
            throw std::runtime_error(std::string("Fetch invitation names failed: ") + mysql_stmt_error(statement.get()));
        }

        if (groupNameLength == 0 || groupNameLength >= sizeof(notification.groupName) || inviterNickLength == 0 || inviterNickLength >= sizeof(notification.inviterNick)) {
            throw std::runtime_error("Invalid invitation name length");
        }

        // 拒绝数据库字符串内部包含 NULL 的情况。
        if (std::memchr(notification.groupName,'\0',groupNameLength) != nullptr
                || std::memchr(notification.inviterNick,'\0',inviterNickLength) != nullptr) {
            throw std::runtime_error("Invitation name contains embedded null");
        }
    }

    // 必须在已经开启的事务中调用。
    // 找不到群时返回空值；找到后锁住该群记录，直到提交或回滚。
    std::optional<std::int32_t> lockGroupOwner(MYSQL* mysql,std::int32_t groupId) {
        constexpr const char* SQL ="SELECT owner_id FROM t_group WHERE id = ? FOR UPDATE";

        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

        if (!statement) {
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if (mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0) {
            throw std::runtime_error(std::string("Prepare group lock failed: ") + mysql_stmt_error(statement.get()));
        }

        MYSQL_BIND parameter{};
        parameter.buffer_type = MYSQL_TYPE_LONG;
        parameter.buffer = &groupId;
        parameter.buffer_length = sizeof(groupId);

        if (mysql_stmt_bind_param(statement.get(), &parameter) != 0 || mysql_stmt_execute(statement.get()) != 0) {
            throw std::runtime_error(std::string("Execute group lock failed: ") + mysql_stmt_error(statement.get()));
        }

        // 数据库 owner_id 是 BIGINT。
        long long ownerId = 0;
        bool isNull = false;

        MYSQL_BIND column{};
        column.buffer_type = MYSQL_TYPE_LONGLONG;
        column.buffer = &ownerId;
        column.buffer_length = sizeof(ownerId);
        column.is_null = &isNull;

        if (mysql_stmt_bind_result(statement.get(), &column) != 0 || mysql_stmt_store_result(statement.get()) != 0) {
            throw std::runtime_error(std::string("Read group lock failed: ") + mysql_stmt_error(statement.get()));
        }

        if (mysql_stmt_num_rows(statement.get()) == 0) {
            return std::nullopt;
        }

        if (mysql_stmt_fetch(statement.get()) != 0) {
            throw std::runtime_error(std::string("Fetch group owner failed: ") + mysql_stmt_error(statement.get()));
        }

        if (isNull || ownerId <= 0 || ownerId > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("Group owner ID is outside the protocol range");
        }

        return static_cast<std::int32_t>(ownerId);
    }

    // 查询群成员 ID，不查询在线状态
    std::vector<std::int32_t> loadGroupMemberIds(MYSQL* mysql,std::int32_t groupId) {
        constexpr const char* SQL =
            "SELECT user_id FROM t_group_member "
            "WHERE group_id = ? "
            "ORDER BY user_id "
            "LIMIT 65";

        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

        if (!statement) {
            throw std::runtime_error("mysql_stmt_init failed");
        }

        if (mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0) {
            throw std::runtime_error(std::string("Prepare group recipients failed: ") + mysql_stmt_error(statement.get()));
        }

        MYSQL_BIND parameter{};
        parameter.buffer_type = MYSQL_TYPE_LONG;
        parameter.buffer = &groupId;
        parameter.buffer_length = sizeof(groupId);

        if (mysql_stmt_bind_param(statement.get(), &parameter) != 0 || mysql_stmt_execute(statement.get()) != 0) {
            throw std::runtime_error(std::string("Query group recipients failed: ") + mysql_stmt_error(statement.get()));
        }

        // user_id 对应数据库 BIGINT
        long long databaseUserId = 0;
        bool isNull = false;

        MYSQL_BIND column{};
        column.buffer_type = MYSQL_TYPE_LONGLONG;
        column.buffer = &databaseUserId;
        column.buffer_length = sizeof(databaseUserId);
        column.is_null = &isNull;

        if (mysql_stmt_bind_result(statement.get(), &column) != 0 || mysql_stmt_store_result(statement.get()) != 0) {
            throw std::runtime_error(std::string("Read group recipients failed: ") + mysql_stmt_error(statement.get()));
        }

        const my_ulonglong rowCount =mysql_stmt_num_rows(statement.get());

        if (rowCount > static_cast<my_ulonglong>(MAX_GROUP_MEMBERS)) {
            throw std::runtime_error("Group member count exceeds the limit");
        }

        std::vector<std::int32_t> members;
        members.reserve(static_cast<std::size_t>(rowCount));

        while (true) {
            const int fetchResult = mysql_stmt_fetch(statement.get());

            if (fetchResult == MYSQL_NO_DATA) {
                break;
            }

            if (fetchResult != 0) {
                throw std::runtime_error(std::string("Fetch group recipient failed: ") + mysql_stmt_error(statement.get()));
            }

            if (isNull || databaseUserId <= 0 || databaseUserId > std::numeric_limits<std::int32_t>::max()) {
                throw std::runtime_error("Invalid group recipient ID");
            }

            members.push_back(static_cast<std::int32_t>(databaseUserId));
        }

        return members;
    }
}

DispatchResult Dispatcher::dispatch(std::uint64_t connectionId,const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const{

    if(body.size() <4){
        throw std::runtime_error("Message body is too short");
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(body.data());

    std::uint32_t protocolType = static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1])<<8)
        | (static_cast<std::uint32_t>(bytes[2])<<16)
        | (static_cast<std::uint32_t>(bytes[3])<<24);

    printf("Dispatcher: connection=%llu,protocol=%u, body size=%zu\n",static_cast<unsigned long long>(connectionId),static_cast<unsigned int>(protocolType),body.size());

    MySqlThreadScope mysqlThread;

    switch(protocolType){
        case DEF_PROT_REGISTER_RQ:
            {
                DispatchResult output;
                output.body = handleRegister(body);
                return output;
            }

        case DEF_PPROT_LOGIN_RQ:
            return handleLogin(body);

        case DEF_PROT_CHAT_INFO_RQ:
            return handleChat(body,authenticatedUser);

        case DEF_PROT_ADD_FRIEND_RQ:
            return handleAddFriendRequest(body,authenticatedUser);

        case DEF_PROT_ADD_FRIEND_RS:
            return handleFriendAcceptance(body,authenticatedUser);

        case DEF_PROT_GROUP_CREATE_RQ:
            return handleGroupCreate(body,authenticatedUser);

        case DEF_PROT_GROUP_MEMBERS_RQ:
            return handleGroupMembers(body, authenticatedUser);

        case DEF_PROT_GROUP_INVITE_RQ:
            return handleGroupInvite(body, authenticatedUser);

        case DEF_PROT_GROUP_INVITE_DECISION_RQ:
            return handleGroupAcceptance(body, authenticatedUser);

        case DEF_PROT_GROUP_CHAT_RQ:
            return handleGroupChat(body, authenticatedUser);

        case DEF_PROT_FILE_OFFER_RQ:
            return handleFileOffer(body, authenticatedUser);

        case DEF_PROT_DELETE_FRIEND_RQ:
            return handleDeleteFriend(body, authenticatedUser);

        case DEF_PROT_GROUP_LEAVE_RQ:
            return handleGroupLeave(body, authenticatedUser);

        case DEF_PROT_UPDATE_PROFILE_RQ:
            return handleUpdateProfile(body, authenticatedUser);

        case DEF_PROT_CHANGE_PASSWORD_RQ:
            return handleChangePassword(body, authenticatedUser);

        default:
            throw std::runtime_error("Unsupported request protocol");
    }
}

//注册请求函数
std::vector<char> Dispatcher::handleRegister(const std::vector<char>& body) const{
    if(body.size() != sizeof(PROT_REGISTER_RQ)){
        throw std::runtime_error("Invalid registration request size");
    }

    PROT_REGISTER_RQ request{};
    std::memcpy(&request,body.data(),sizeof(request));

    std::string nick = readFixedString(request.nick,"nick");
    std::string tel = readFixedString(request.tel,"tel");
    std::string password = readFixedString(request.pass,"password");

    if(nick.empty()){
        throw std::runtime_error("Nickname must not be empty");
    }

    bool validTel = tel.size() == 11 && std::all_of(tel.begin(),tel.end(),[](unsigned char ch){
            return ch >= '0' && ch <= '9';
            }
            );

    if(!validTel){
        throw std::runtime_error("Telphone must contain 11 digits");
    }

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();

    constexpr const char* FIND_TEL = "select id from t_user where tel = ? limit 1";
    constexpr const char* FIND_NICK = "select id from t_user where nick = ? limit 1";

    if(userValueExists(mysql,FIND_TEL,tel)){
        return makeRegisterReply(REGISTER_RESULT_FAIL_TEL);
    }

    if(userValueExists(mysql,FIND_NICK,nick)){
        return makeRegisterReply(REGISTER_RESULT_FAIL_NICK);
    }

    std::string encodedHash = hashPassword(password);

    constexpr const char* INSERT_USER = "insert into t_user (nick ,tel,pass,pass_hash,feeling,iconid) values (?,?,'',?,'',20)";

    std::unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql),&mysql_stmt_close);

    if(!statement){
        throw std::runtime_error("mysql_stmt_init failed");
    }

    if(mysql_stmt_prepare(statement.get(),INSERT_USER,static_cast<unsigned long>(std::strlen(INSERT_USER))) != 0){
        throw std::runtime_error(std::string("Prepare registration failed: ") + mysql_stmt_error(statement.get()));
    }

    MYSQL_BIND parameter[3]{};

    unsigned long lengths[3]{
        static_cast<unsigned long>(nick.size()),
            static_cast<unsigned long>(tel.size()),
            static_cast<unsigned long>(encodedHash.size())
    };

    const std::string* values[3]{&nick,&tel,&encodedHash};

    for(std::size_t i = 0;i<3;i++){
        parameter[i].buffer_type = MYSQL_TYPE_STRING;
        parameter[i].buffer = const_cast<char*>(values[i]->data());
        parameter[i].buffer_length = lengths[i];
        parameter[i].length = &lengths[i];
    }

    if(mysql_stmt_bind_param(statement.get(),parameter) != 0){
        throw std::runtime_error(std::string("Bind registration failed: ") + mysql_stmt_error(statement.get()));
    }

    if(mysql_stmt_execute(statement.get()) != 0){
        unsigned int errorCode = mysql_stmt_errno(statement.get());

        std::string errorMessage = mysql_stmt_error(statement.get());

        //先关闭插入语句，再使用这个连接查询
        statement.reset();

        if(errorCode == ER_DUP_ENTRY){
            //预检查后，其他客户端仍可能抢先注册相同值
            if(userValueExists(mysql,FIND_TEL,tel)){
                return makeRegisterReply(REGISTER_RESULT_FAIL_TEL);
            }

            if(userValueExists(mysql,FIND_NICK,nick)){
                return makeRegisterReply(REGISTER_RESULT_FAIL_NICK);
            }
        }

        throw std::runtime_error("Insert registration failed: " + errorMessage);
    }

    return makeRegisterReply(REGISTER_RESULT_SUCC);
}

//登录处理函数
DispatchResult Dispatcher::handleLogin(const std::vector<char>& body) const{
    if(body.size() != sizeof(PROT_LOGIN_RQ)){
        throw std::runtime_error("Invalid login request size");
    }

    PROT_LOGIN_RQ request{};

    std::memcpy(&request,body.data(),sizeof(request));

    std::string tel = readFixedString(request.tel,"tel");
    std::string password = readFixedString(request.pass,"password");

    bool validTel = tel.size() == 11 && std::all_of(tel.begin(),tel.end(),[](unsigned char ch){
            return ch >= '0' && ch <= '9';
            }
            );

    if(!validTel || password.empty()){
        throw std::runtime_error("Invalid login fields");
    }

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();

    constexpr const char* SQL = "select id, pass_hash from t_user where tel = ? limit 1";

    std::unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql),&mysql_stmt_close);

    if(!statement){
        throw std::runtime_error("mysql_stmt_init failed");
    }

    if(mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0){
        throw std::runtime_error(std::string("Prepare login failed: ") + mysql_stmt_error(statement.get()));
    }

    unsigned long telLength = static_cast<unsigned long>(tel.size());

    MYSQL_BIND parameter{};
    parameter.buffer_type = MYSQL_TYPE_STRING;
    parameter.buffer = const_cast<char*>(tel.data());
    parameter.buffer_length = telLength;
    parameter.length = &telLength;

    if(mysql_stmt_bind_param(statement.get(),&parameter) != 0){
        throw std::runtime_error(std::string("Bind login parameter failed: ") + mysql_stmt_error(statement.get()));
    }

    if(mysql_stmt_execute(statement.get()) != 0){
        throw std::runtime_error(std::string("Excute login failed: ") + mysql_stmt_error(statement.get()));
    }

    //数据库 id 是 BIGINT，先用64位整数接收
    long long databaseId = 0;

    char hashBuffer[256]{};
    unsigned long hashLength = 0;

    bool idIsNull = false;
    bool hashIsNull = false;

    MYSQL_BIND columns[2]{};

    columns[0].buffer_type = MYSQL_TYPE_LONGLONG;
    columns[0].buffer = &databaseId;
    columns[0].buffer_length = sizeof(databaseId);
    columns[0].is_null = &idIsNull;

    columns[1].buffer_type = MYSQL_TYPE_STRING;
    columns[1].buffer = hashBuffer;
    columns[1].buffer_length = sizeof(hashBuffer);
    columns[1].length = &hashLength;
    columns[1].is_null = &hashIsNull;

    if(mysql_stmt_bind_result(statement.get(),columns) != 0){
        throw std::runtime_error(std::string("Bind login result failed: ") + mysql_stmt_error(statement.get()));
    }

    if(mysql_stmt_store_result(statement.get()) != 0){
        throw std::runtime_error(std::string("Store login result failed: ") + mysql_stmt_error(statement.get()));
    }

    if(mysql_stmt_num_rows(statement.get()) == 0){
        return makeLoginReply(LOGIN_RESULT_NOEXIST);
    }

    if(mysql_stmt_fetch(statement.get()) != 0){
        throw std::runtime_error(std::string("Fetch login result failed: ") + mysql_stmt_error(statement.get()));
    }

    if(hashIsNull || hashLength == 0){
        return makeLoginReply(LOGIN_RESULT_PASSERR);
    }

    if(hashLength > sizeof(hashBuffer)){
        throw std::runtime_error("Stored password hash is too long");
    }

    std::string encodedHash(hashBuffer,hashLength);

    if(!verifyPassword(password,encodedHash)){
        return makeLoginReply(LOGIN_RESULT_PASSERR);
    }

    if(idIsNull || databaseId <= 0 || databaseId > std::numeric_limits<std::int32_t>::max()){
        throw std::runtime_error("User ID is outside the protocol range");
    }

    std::int32_t userId = static_cast<std::int32_t>(databaseId);

    //登录查询已经完成，先关闭它的语句及结果
    statement.reset();

    DispatchResult output = makeLoginReply(LOGIN_RESULT_SUCC,userId);

    //本人资料：也用于通知好友自己上线
    std::vector<char> selfInfo = loadUserInfo(mysql,userId);
    output.followingBodies.push_back(selfInfo);

    //查找好友ID
    auto friendIds = loadFriendIds(mysql,userId);

    //逐个查询好友资料
    for(std::int32_t friendId:friendIds){
        //发给自己：好友的资料
        output.followingBodies.push_back(loadUserInfo(mysql,friendId));

        //发给好友：我的上线资料
        output.notifications.push_back(UserNotification{friendId,selfInfo});
    }

    //查找群聊列表资料
    auto groups = loadJoinedGroups(mysql, userId);

    for (auto& groupBody : groups) {
        output.followingBodies.push_back(std::move(groupBody));
    }

    return output;
}

//聊天处理函数
DispatchResult Dispatcher::handleChat(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const{
    if(!authenticatedUser){
        throw std::runtime_error("Private chat requires login");
    }

    if(body.size() != sizeof(PROT_CHAT_INFO_RQ)){
        throw std::runtime_error("Invalid private char request size");
    }

    PROT_CHAT_INFO_RQ request{};

    std::memcpy(&request,body.data(),sizeof(request));

    std::int32_t senderId = *authenticatedUser;

    if(request.myid != senderId){
        throw std::runtime_error("Private chat sender identity mismatch");
    }

    if(request.friendid <= 0 || request.friendid == senderId){
        throw std::runtime_error("Invalid private chat target");
    }

    std::string message = readFixedString(request.msg,"chat message");

    DispatchResult output = makeChatReply(senderId,request.friendid,CHAT_RESULT_FAIL);

    if(message.empty()){
        return output;
    }

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    if(!friendshipExists(connection.get(),senderId,request.friendid)){
        return output;
    }

    output.privateChat = PendingPrivateChat{request.friendid,body};

    return output;
}

//好友申请处理函数
DispatchResult Dispatcher::handleAddFriendRequest(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const{
    if(!authenticatedUser){
        throw std::runtime_error("Friend request requires login");
    }

    if(body.size() != sizeof(PROT_ADD_FRIEND_RQ)){
        throw std::runtime_error("Invalid friend request size");
    }

    PROT_ADD_FRIEND_RQ request{};

    std::memcpy(&request,body.data(),sizeof(request));

    if(request.myid != *authenticatedUser){
        throw std::runtime_error("Friend request identity mismatch");
    }

    std::string targetNick = readFixedString(request.frinick,"target nickname");

    if(targetNick.empty()){
        throw std::runtime_error("Target nickname is empty");
    }

    //生成发给申请者的结果
    auto makeReply = [&](std::int32_t result){
        PROT_ADD_FRIEND_RS reply{};
        reply.result = result;
        reply.destid = *authenticatedUser;

        //客户端从 mynick 读取被添加者的昵称
        std::memcpy(reply.mynick,request.frinick,sizeof(reply.mynick));

        DispatchResult output;
        output.body.resize(sizeof(reply));

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        return output;
    };

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();

    auto targetId = findUserIdByNick(mysql,targetNick);

    if(!targetId){
        return makeReply(ADDFRI_RESULT_NOEXIST);
    }

    if(*targetId == *authenticatedUser){
        return makeReply(ADDFRI_RESULT_SELF);
    }

    if(friendshipExists(mysql,*authenticatedUser,*targetId)){
        return makeReply(ADDFRI_RESULT_ALREADY_FRIEND);
    }

    //申请者昵称以数据库为准，不相信客户端提供的 mynick
    auto applicantBody = loadUserInfo(mysql,*authenticatedUser);

    PROT_FRIEND_INFO applicant{};

    std::memcpy(&applicant,applicantBody.data(),sizeof(applicant));

    std::memcpy(request.mynick,applicant.nick,sizeof(request.mynick));

    std::vector<char> forwardedBody(sizeof(request));

    std::memcpy(forwardedBody.data(),&request,sizeof(request));

    //暂时准备离线结果，事件线程下一步会决定是否转发
    DispatchResult output = makeReply(ADDFRI_RESULT_OFFLINE);

    output.friendRequest = PendingFriendRequest{*authenticatedUser,*targetId,std::move(forwardedBody)};

    return output;
}

DispatchResult Dispatcher::handleFriendAcceptance(const std::vector<char>& body,std::optional<std::int32_t> authentiactedUser) const{
    if(!authentiactedUser){
        throw std::runtime_error("Friend acceptance requires login");
    }

    if(body.size() != sizeof(PROT_ADD_FRIEND_RS)){
        throw std::runtime_error("Invalid friend acceptance size");
    }

    PROT_ADD_FRIEND_RS request{};

    std::memcpy(&request,body.data(),sizeof(request));

    const std::int32_t responderId = *authentiactedUser;
    const std::int32_t applicantId = request.destid;

    if(request.result != ADDFRI_RESULT_ACCEPT || request.myid != responderId || applicantId <= 0 || applicantId == responderId){
        throw std::runtime_error("Invalid friend acceptance identities");
    }

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();

    //先准备双方资料，避免入库后才发现资料无法编码
    auto applicantBody = loadUserInfo(mysql,applicantId);
    auto responderBody = loadUserInfo(mysql,responderId);

    PROT_FRIEND_INFO applicantInfo{};
    PROT_FRIEND_INFO responderInfo{};

    std::memcpy(&applicantInfo,applicantBody.data(),sizeof(applicantInfo));
    std::memcpy(&responderInfo,responderBody.data(),sizeof(responderInfo));

    //一条原子SQL保存双向关系
    createFriendship(mysql,applicantId,responderId);

    PROT_ADD_FRIEND_RS accepted{};
    accepted.result = ADDFRI_RESULT_ACCEPT;

    accepted.destid = applicantId;
    accepted.myid = responderId;

    //昵称全部源语数据库，不相信回复包里面的昵称
    std::memcpy(accepted.destnick,applicantInfo.nick,sizeof(accepted.destnick));
    std::memcpy(accepted.mynick,responderInfo.nick,sizeof(accepted.mynick));

    std::vector<char> acceptedBody(sizeof(accepted));

    std::memcpy(acceptedBody.data(),&accepted,sizeof(accepted));

    DispatchResult output;

    //发送给当前回复者：新增好友，也就是原申请者的资料
    output.body = std::move(applicantBody);

    //发送给申请者：先通知申请被接受，再更新好友资料
    output.notifications.push_back(UserNotification{applicantId,std::move(acceptedBody)});

    output.notifications.push_back(UserNotification{applicantId,std::move(responderBody)});

    output.establishedFriendship = EstablishedFriendship{applicantId,responderId};

    return output;
}

//创建群请求处理
DispatchResult Dispatcher::handleGroupCreate(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const {
    if (!authenticatedUser) {
        throw std::runtime_error("Group creation requires login");
    }

    if (body.size() != sizeof(PROT_GROUP_CREATE_RQ)) {
        throw std::runtime_error("Invalid group creation request size");
    }

    PROT_GROUP_CREATE_RQ request{};

    std::memcpy(&request,body.data(),sizeof(request));

    if (request.requestId == 0) {
        throw std::runtime_error("Invalid group creation request ID");
    }

    // 固定数组必须包含字符串结束符。
    const char* end = std::find(request.groupName,request.groupName + sizeof(request.groupName),'\0');

    if (end == request.groupName + sizeof(request.groupName)) {
        return makeGroupCreateReply(request.requestId,GROUP_CREATE_INVALID_NAME);
    }

    std::string groupName(request.groupName, static_cast<std::size_t>(end - request.groupName));

    if (!isValidGroupName(groupName)) {
        return makeGroupCreateReply(request.requestId,GROUP_CREATE_INVALID_NAME);
    }

    // 先分配回复空间，避免提交之后才分配回复内存。
    DispatchResult output = makeGroupCreateReply(request.requestId,GROUP_CREATE_FAIL);

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();
    bool transactionStarted = false;

    try {
        if (mysql_query(mysql, "START TRANSACTION") != 0) {
            throw std::runtime_error(std::string("Start group transaction failed: ") +mysql_error(mysql));
        }

        transactionStarted = true;

        constexpr const char* INSERT_GROUP ="insert into t_group (name, owner_id) values (?, ?)";

        unsigned long nameLength = static_cast<unsigned long>(groupName.size());

        long long ownerId = *authenticatedUser;

        MYSQL_BIND groupParameters[2]{};

        groupParameters[0].buffer_type = MYSQL_TYPE_STRING;
        groupParameters[0].buffer = groupName.data();
        groupParameters[0].buffer_length = nameLength;
        groupParameters[0].length = &nameLength;

        groupParameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
        groupParameters[1].buffer = &ownerId;
        groupParameters[1].buffer_length = sizeof(ownerId);

        my_ulonglong insertedId = executeGroupStatement(mysql,INSERT_GROUP,groupParameters);

        if (insertedId == 0 || insertedId > static_cast<my_ulonglong>( std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("Created group ID exceeds protocol range");
        }

        std::int32_t groupId =static_cast<std::int32_t>(insertedId);

        constexpr const char* INSERT_OWNER ="insert into t_group_member (group_id, user_id) values (?,?)";

        MYSQL_BIND memberParameters[2]{};

        memberParameters[0].buffer_type = MYSQL_TYPE_LONG;
        memberParameters[0].buffer = &groupId;
        memberParameters[0].buffer_length = sizeof(groupId);

        memberParameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
        memberParameters[1].buffer = &ownerId;
        memberParameters[1].buffer_length = sizeof(ownerId);

        executeGroupStatement(mysql,INSERT_OWNER,memberParameters);

        if (mysql_commit(mysql) != 0) {
            throw std::runtime_error(std::string("Commit group transaction failed: ") + mysql_error(mysql));
        }

        transactionStarted = false;

        PROT_GROUP_CREATE_RS reply{};
        reply.requestId = request.requestId;
        reply.result = GROUP_CREATE_SUCC;
        reply.groupId = groupId;
        reply.ownerId = *authenticatedUser;

        std::memcpy(reply.groupName,groupName.data(),groupName.size());

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        printf("Group created: group=%d, owner=%d\n",static_cast<int>(groupId),static_cast<int>(*authenticatedUser));

        return output;
    }
    catch (const std::exception& error) {
        fprintf(stderr,"Group creation failed: %s\n",error.what());

        if (transactionStarted) {
            if (mysql_rollback(mysql) != 0) {
                fprintf(stderr,"Group rollback failed; discarding connection\n");

                m_mysqlPool.discard(connection);
            }
        }
        else {
            // START TRANSACTION 失败，保守地不复用此连接。
            m_mysqlPool.discard(connection);
        }

        return output;
    }
    catch (...) {
        if (transactionStarted) {
            if (mysql_rollback(mysql) != 0) {
                m_mysqlPool.discard(connection);
            }
        }
        else {
            m_mysqlPool.discard(connection);
        }

        fprintf(stderr, "Unknown group creation failure\n");
        return output;
    }
}

//群成员查询函数
DispatchResult Dispatcher::handleGroupMembers(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const {
    if (!authenticatedUser) {
        throw std::runtime_error("Group member query requires login");
    }

    if (body.size() != sizeof(PROT_GROUP_MEMBERS_RQ)) {
        throw std::runtime_error("Invalid group member query size");
    }

    PROT_GROUP_MEMBERS_RQ request{};

    std::memcpy(&request,body.data(),sizeof(request));

    if (request.requestId == 0 || request.groupId <= 0) {
        throw std::runtime_error("Invalid group member query fields");
    }

    PROT_GROUP_MEMBERS_RS reply{};
    reply.requestId = request.requestId;
    reply.groupId = request.groupId;

    auto finishReply = [&reply]() {DispatchResult output;output.body.resize(sizeof(reply));

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        return output;
    };

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();

    std::int32_t groupId = request.groupId;
    long long requesterId = *authenticatedUser;

    MYSQL_BIND parameters[2]{};

    parameters[0].buffer_type = MYSQL_TYPE_LONG;
    parameters[0].buffer = &groupId;
    parameters[0].buffer_length = sizeof(groupId);

    parameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
    parameters[1].buffer = &requesterId;
    parameters[1].buffer_length = sizeof(requesterId);

    if (!groupQueryHasRows(mysql,"select 1 from t_group where id = ? limit 1",parameters)) {
        reply.result = GROUP_MEMBERS_NOT_FOUND;
        return finishReply();
    }

    if (!groupQueryHasRows(mysql,"select 1 from t_group_member where group_id = ? and user_id = ? limit 1", parameters)) {
        reply.result = GROUP_MEMBERS_NOT_MEMBER;
        return finishReply();
    }

    // 实际查询也带成员资格条件，避免仅依赖前一次检查。
    constexpr const char* SQL = 
        "SELECT u.id, u.iconid, u.nick "
        "FROM t_group_member AS m "
        "JOIN t_user AS u ON u.id = m.user_id "
        "WHERE m.group_id = ? "
        "AND EXISTS ("
        "SELECT 1 FROM t_group_member AS me "
        "WHERE me.group_id = m.group_id AND me.user_id = ?"
        ") "
        "ORDER BY u.id LIMIT 65";

    std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

    if (!statement) {
        throw std::runtime_error("mysql_stmt_init failed");
    }

    if (mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0) {
        throw std::runtime_error(std::string("Prepare group members failed: ") + mysql_stmt_error(statement.get()));
    }

    if (mysql_stmt_bind_param(statement.get(), parameters) != 0 || mysql_stmt_execute(statement.get()) != 0){
        throw std::runtime_error(std::string("Execute group members failed: ") + mysql_stmt_error(statement.get()));
    }

    long long memberId = 0;
    GROUP_MEMBER_INFO member{};
    unsigned long nickLength = 0;
    bool nullFields[3]{};

    MYSQL_BIND columns[3]{};

    columns[0].buffer_type = MYSQL_TYPE_LONGLONG;
    columns[0].buffer = &memberId;
    columns[0].buffer_length = sizeof(memberId);
    columns[0].is_null = &nullFields[0];

    columns[1].buffer_type = MYSQL_TYPE_LONG;
    columns[1].buffer = &member.iconId;
    columns[1].buffer_length = sizeof(member.iconId);
    columns[1].is_null = &nullFields[1];

    columns[2].buffer_type = MYSQL_TYPE_STRING;
    columns[2].buffer = member.nick;
    columns[2].buffer_length = sizeof(member.nick);
    columns[2].length = &nickLength;
    columns[2].is_null = &nullFields[2];

    if (mysql_stmt_bind_result(statement.get(), columns) != 0 || mysql_stmt_store_result(statement.get()) != 0) {
        throw std::runtime_error(std::string("Read group members failed: ") + mysql_stmt_error(statement.get()));
    }

    my_ulonglong rowCount = mysql_stmt_num_rows(statement.get());

    if (rowCount > MAX_GROUP_MEMBERS) {
        reply.result = GROUP_MEMBERS_TOO_MANY;
        return finishReply();
    }

    if (rowCount == 0) {
        // 成员资格可能已发生变化。
        reply.result = GROUP_MEMBERS_NOT_MEMBER;
        return finishReply();
    }

    while (true) {
        member = GROUP_MEMBER_INFO{};
        nickLength = 0;

        int fetched = mysql_stmt_fetch(statement.get());

        if (fetched == MYSQL_NO_DATA) {
            break;
        }

        if (fetched != 0) {
            throw std::runtime_error(std::string("Fetch group members failed: ") + mysql_stmt_error(statement.get()));
        }

        if (nullFields[0] || nullFields[2] || memberId <= 0 ||
                memberId > std::numeric_limits<std::int32_t>::max() ||
                nickLength == 0 ||
                nickLength >= sizeof(member.nick)) {
            throw std::runtime_error("Invalid stored group member");
        }

        member.userId = static_cast<std::int32_t>(memberId);
        member.nick[nickLength] = '\0';

        if (nullFields[1]) {
            member.iconId = 20;
        }

        reply.members[reply.memberCount] = member;
        ++reply.memberCount;
    }

    reply.result = GROUP_MEMBERS_SUCC;

    return finishReply();
}

DispatchResult Dispatcher::handleGroupInvite(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const{
    if (!authenticatedUser || *authenticatedUser <= 0) {
        throw std::runtime_error("Group invitation requires login");
    }

    if (body.size() != sizeof(PROT_GROUP_INVITE_RQ)) {
        throw std::runtime_error("Invalid group invitation request size");
    }

    PROT_GROUP_INVITE_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.requestId == 0 || request.groupId <= 0 || request.targetUserId <= 0) {
        throw std::runtime_error("Invalid group invitation fields");
    }

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    const std::int32_t checkResult = checkGroupInvite(connection.get(),request.groupId,*authenticatedUser,request.targetUserId);

    DispatchResult output;

    // 数据库检查未通过，直接返回对应业务结果。
    if (checkResult != GROUP_INVITE_SUCC) {
        PROT_GROUP_INVITE_RS reply{};
        reply.requestId = request.requestId;
        reply.invitationId = 0;
        reply.groupId = request.groupId;
        reply.targetUserId = request.targetUserId;
        reply.result = checkResult;

        output.body.resize(sizeof(reply));

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        return output;
    }

    // 数据库检查通过，交给事件线程继续检查在线状态。
    PendingGroupInvitation invitation{};
    invitation.requestId = request.requestId;
    invitation.groupId = request.groupId;

    // 邀请人身份取自登录会话，不接受客户端自行指定。
    invitation.inviterId = *authenticatedUser;
    invitation.targetUserId = request.targetUserId;

    invitation.notification.groupId = invitation.groupId;
    invitation.notification.inviterId = invitation.inviterId;

    // 仍然在工作线程中使用当前借到的数据库连接。
    loadGroupInvitationNames(connection.get(),invitation.notification);

    output.groupInvitation = std::move(invitation);

    // 此时不生成成功回复。
    // 等事件线程确认可以转发邀请后，再回复客户端。
    return output;
}

DispatchResult Dispatcher::handleGroupAcceptance(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const {
    if (!authenticatedUser || body.size() != sizeof(GroupInvitationAcceptanceTask)) {
        throw std::runtime_error("Invalid group acceptance task");
    }

    GroupInvitationAcceptanceTask task{};
    std::memcpy(&task, body.data(), sizeof(task));

    if (task.requestId == 0 || task.invitationId == 0 || task.groupId <= 0 || task.inviterId <= 0 || task.targetUserId != *authenticatedUser || task.targetUserId <= 0 || task.targetUserId == task.inviterId) {
        throw std::runtime_error("Invalid group acceptance identity");
    }

    PROT_GROUP_INVITE_DECISION_RS reply{};
    reply.requestId = task.requestId;
    reply.invitationId = task.invitationId;
    reply.groupId = task.groupId;
    reply.result = GROUP_INVITE_FAIL;

    //提前分配主回复空间
    DispatchResult output;
    output.body.resize(sizeof(reply));

    auto writeReply = [&](std::int32_t code) {
        reply.result = code;
        std::memcpy(output.body.data(),&reply,sizeof(reply));
    };

    writeReply(GROUP_INVITE_FAIL);

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();

    bool transactionStarted = false;
    bool commitAttempted = false;

    try {
        if (mysql_query(mysql, "START TRANSACTION") != 0) {
            throw std::runtime_error(std::string("Start acceptance failed: ") +mysql_error(mysql));
        }

        transactionStarted = true;

        //同一群的入群事务先锁住同一条群记录
        auto ownerId = lockGroupOwner(mysql, task.groupId);

        std::int32_t checkResult = GROUP_INVITE_NOT_FOUND;

        if (ownerId) {
            if (*ownerId != task.inviterId) {
                checkResult = GROUP_INVITE_NOT_OWNER;
            } 
            else {
                // 不能使用发出邀请时的旧检查结果。
                checkResult = checkGroupInvite(mysql,task.groupId,task.inviterId,task.targetUserId);
            }
        }

        if (checkResult != GROUP_INVITE_SUCC) {
            if (mysql_rollback(mysql) != 0) {
                throw std::runtime_error(std::string("Rollback acceptance failed: ") +mysql_error(mysql));
            }

            transactionStarted = false;
            output.groupMembershipCommitted = true;
            writeReply(checkResult);
            return output;
        }

        //在提交前准备群资料和通知，避免提交以后才分配内存
        PROT_GROUP_INVITE_NOTIFY names{};
        names.groupId = task.groupId;
        names.inviterId = task.inviterId;

        loadGroupInvitationNames(mysql, names);

        PROT_GROUP_INFO groupInfo{};
        groupInfo.groupId = task.groupId;
        groupInfo.ownerId = task.inviterId;

        std::memcpy(groupInfo.groupName,names.groupName,sizeof(groupInfo.groupName));

        std::vector<char> groupBody(sizeof(groupInfo));
        std::memcpy(groupBody.data(),&groupInfo,sizeof(groupInfo));

        //主回复之后，把群资料发给新成员
        output.followingBodies.push_back(std::move(groupBody));

        PROT_GROUP_INVITE_RESULT_NOTIFY notification{};
        notification.invitationId = task.invitationId;
        notification.groupId = task.groupId;
        notification.targetUserId = task.targetUserId;
        notification.result = GROUP_INVITE_SUCC;

        std::vector<char> notificationBody(sizeof(notification));

        std::memcpy(notificationBody.data(),&notification,sizeof(notification));


        //告知群主，该好友已入群
        output.notifications.push_back(
                UserNotification{
                task.inviterId,
                std::move(notificationBody)
                }
                );

        std::int32_t groupId = task.groupId;
        long long targetUserId = task.targetUserId;

        MYSQL_BIND parameters[2]{};

        parameters[0].buffer_type = MYSQL_TYPE_LONG;
        parameters[0].buffer = &groupId;
        parameters[0].buffer_length = sizeof(groupId);

        parameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
        parameters[1].buffer = &targetUserId;
        parameters[1].buffer_length = sizeof(targetUserId);

        executeGroupStatement(mysql,"INSERT INTO t_group_member (group_id, user_id) VALUES (?, ?)",parameters);

        commitAttempted = true;

        if (mysql_commit(mysql) != 0) {
            throw std::runtime_error(std::string("Commit acceptance failed: ") + mysql_error(mysql));
        }

        transactionStarted = false;
        writeReply(GROUP_INVITE_SUCC);
    }
    catch (const std::exception& error) {
        fprintf(stderr,"Group acceptance failed: %s\n",error.what());

        bool discardConnection = !transactionStarted;

        if (transactionStarted) {
            if (mysql_rollback(mysql) != 0) {
                discardConnection = true;
            }
        }

        // 提交报错时，结果可能不确定，不自动重试，
        // 同时丢弃这条数据库连接。
        if (commitAttempted) {
            discardConnection = true;
        }

        if (discardConnection) {
            m_mysqlPool.discard(connection);
        }

        output.followingBodies.clear();
        output.notifications.clear();
        writeReply(GROUP_INVITE_FAIL);
    }

    return output;
}

DispatchResult Dispatcher::handleGroupChat(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const {
    if (!authenticatedUser || *authenticatedUser <= 0) {
        throw std::runtime_error("Group chat requires login");
    }

    if (body.size() != sizeof(PROT_GROUP_CHAT_RQ)) {
        throw std::runtime_error("Invalid group chat request size");
    }

    PROT_GROUP_CHAT_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.requestId == 0 || request.groupId <= 0) {
        throw std::runtime_error("Invalid group chat request fields");
    }

    PROT_GROUP_CHAT_RS reply{};
    reply.requestId = request.requestId;
    reply.groupId = request.groupId;

    auto makeReply = [&](std::int32_t code) {
        reply.result = code;

        DispatchResult output;
        output.body.resize(sizeof(reply));

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        return output;
    };

    // 长度不包含结束符，必须给 '\0' 留一个字节
    if (request.contentLength == 0 || request.contentLength >= GROUP_MESSAGE_BYTES) {
        return makeReply(GROUP_CHAT_INVALID_MESSAGE);
    }

    if (request.content[request.contentLength] != '\0' || 
            std::memchr(request.content,'\0',request.contentLength) != nullptr) {
        return makeReply(GROUP_CHAT_INVALID_MESSAGE);
    }

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();

    std::int32_t groupId = request.groupId;

    MYSQL_BIND parameter{};
    parameter.buffer_type = MYSQL_TYPE_LONG;
    parameter.buffer = &groupId;
    parameter.buffer_length = sizeof(groupId);

    if (!groupQueryHasRows(mysql,"SELECT 1 FROM t_group WHERE id = ?",&parameter)) {
        return makeReply(GROUP_CHAT_NOT_FOUND);
    }

    auto members = loadGroupMemberIds(mysql, groupId);

    if (std::find(members.begin(),members.end(),*authenticatedUser) == members.end()) {
        return makeReply(GROUP_CHAT_NOT_MEMBER);
    }

    // 昵称从数据库获取，不能由发送者自行指定
    auto senderBody = loadUserInfo(mysql,*authenticatedUser);

    if (senderBody.size() != sizeof(PROT_FRIEND_INFO)) {
        throw std::runtime_error("Invalid group sender information");
    }

    PROT_FRIEND_INFO sender{};
    std::memcpy(&sender,senderBody.data(),sizeof(sender));

    const char* nickEnd =static_cast<const char*>(std::memchr(sender.nick,'\0',sizeof(sender.nick)));

    if (nickEnd == nullptr || nickEnd == sender.nick) {
        throw std::runtime_error("Invalid group sender nickname");
    }

    PROT_GROUP_CHAT_NOTIFY notification{};
    notification.requestId = request.requestId;
    notification.groupId = groupId;
    notification.senderId = *authenticatedUser;
    notification.contentLength = request.contentLength;

    std::memcpy(notification.senderNick,sender.nick,static_cast<std::size_t>(nickEnd - sender.nick));
    std::memcpy(notification.content,request.content,request.contentLength);

    PendingGroupChat pending{};
    pending.requestId = request.requestId;
    pending.groupId = groupId;
    pending.senderId = *authenticatedUser;

    // 不向发送者再转发一份，发送者使用主回复确认。
    for (std::int32_t memberId : members) {
        if (memberId != *authenticatedUser) {
            pending.recipients.push_back(memberId);
        }
    }

    pending.body.resize(sizeof(notification));

    std::memcpy(pending.body.data(),&notification,sizeof(notification));

    // 先准备失败回复，事件线程完成转发后再填写成功及人数。
    DispatchResult output = makeReply(GROUP_CHAT_FAIL);
    output.groupChat = std::move(pending);

    return output;
}

DispatchResult Dispatcher::handleFileOffer(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const {
    if (!authenticatedUser || *authenticatedUser <= 0) {
        throw std::runtime_error("File offer requires login");
    }

    if (body.size() != sizeof(PROT_FILE_OFFER_RQ)) {
        throw std::runtime_error("Invalid file offer size");
    }

    PROT_FILE_OFFER_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.requestId == 0) {
        throw std::runtime_error("Invalid file offer request ID");
    }

    auto makeReply = [&](std::int32_t code) {
        PROT_FILE_OFFER_RS reply{};
        reply.requestId = request.requestId;
        reply.result = code;

        DispatchResult output;
        output.body.resize(sizeof(reply));

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        return output;
    };

    if (request.targetUserId <= 0 || request.targetUserId == *authenticatedUser || request.fileSize > MAX_TRANSFER_FILE_BYTES) {
        return makeReply(FILE_RESULT_INVALID);
    }

    const char* nameEnd =static_cast<const char*>(std::memchr(request.fileName,'\0',sizeof(request.fileName)));

    if (nameEnd == nullptr || nameEnd == request.fileName) {
        return makeReply(FILE_RESULT_INVALID);
    }

    const std::string fileName(request.fileName,static_cast<std::size_t>(nameEnd - request.fileName));

    // 申请只允许携带文件名，不能携带目录或路径
    if (fileName == "." || fileName == ".." ||
            fileName.back() == '.' || fileName.back() == ' ' ||
            fileName.find_first_of("<>:\"/\\|?*") != std::string::npos) {
        return makeReply(FILE_RESULT_INVALID);
    }

    for (unsigned char ch : fileName) {
        if (ch < 32 || ch == 127) {
            return makeReply(FILE_RESULT_INVALID);
        }
    }

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();

    if (!friendshipExists(mysql,*authenticatedUser,request.targetUserId)) {
        return makeReply(FILE_RESULT_NOT_FRIEND);
    }

    auto senderBody = loadUserInfo(mysql,*authenticatedUser);

    if (senderBody.size() != sizeof(PROT_FRIEND_INFO)) {
        throw std::runtime_error("Invalid file sender information");
    }

    PROT_FRIEND_INFO sender{};
    std::memcpy(&sender,senderBody.data(),sizeof(sender));

    const char* nickEnd = static_cast<const char*>(std::memchr(sender.nick,'\0',sizeof(sender.nick)));

    if (nickEnd == nullptr || nickEnd == sender.nick) {
        throw std::runtime_error("Invalid file sender nickname");
    }

    PendingFileOffer pending{};
    pending.requestId = request.requestId;
    pending.senderId = *authenticatedUser;
    pending.targetUserId = request.targetUserId;

    auto& notification = pending.notification;
    notification.fileSize = request.fileSize;
    notification.senderId = *authenticatedUser;

    std::memcpy(notification.senderNick,sender.nick,static_cast<std::size_t>(nickEnd - sender.nick));
    std::memcpy(notification.fileName,fileName.data(),fileName.size());
    std::memcpy(notification.sha256,request.sha256,FILE_SHA256_BYTES);

    // 等事件线程确认目标在线并转发后，再填写成功回复
    DispatchResult output = makeReply(FILE_RESULT_FAIL);
    output.fileOffer = std::move(pending);

    return output;
}

DispatchResult Dispatcher::handleDeleteFriend(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const {
    if (!authenticatedUser || *authenticatedUser <= 0 || body.size() != sizeof(PROT_DELETE_FRIEND_RQ)) {
        throw std::runtime_error("Invalid delete friend request");
    }

    PROT_DELETE_FRIEND_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.requestId == 0 || request.friendId <= 0 || request.friendId == *authenticatedUser) {
        throw std::runtime_error("Invalid delete friend identity");
    }

    PROT_DELETE_FRIEND_RS reply{};
    reply.requestId = request.requestId;
    reply.friendId = request.friendId;

    // 提前准备回复和通知，避免删除成功后才分配内存
    DispatchResult output;
    output.body.resize(sizeof(reply));

    PROT_DELETE_FRIEND_NOTIFY notification{};
    notification.friendId = *authenticatedUser;

    std::vector<char> notificationBody(sizeof(notification));
    std::memcpy(notificationBody.data(),&notification,sizeof(notification));
    output.notifications.push_back(UserNotification{request.friendId,std::move(notificationBody)});

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();

    constexpr const char* SQL =
        "DELETE FROM t_friend "
        "WHERE (idA = ? AND idB = ?) "
        "OR (idA = ? AND idB = ?)";

    std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

    if (!statement) {
        throw std::runtime_error("mysql_stmt_init failed");
    }

    if (mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0) {
        throw std::runtime_error(std::string("Prepare delete friend failed: ") + mysql_stmt_error(statement.get()));
    }

    long long firstId = *authenticatedUser;
    long long secondId = request.friendId;

    MYSQL_BIND parameters[4]{};

    for (auto& parameter : parameters) {
        parameter.buffer_type = MYSQL_TYPE_LONGLONG;
        parameter.buffer_length = sizeof(long long);
    }

    parameters[0].buffer = &firstId;
    parameters[1].buffer = &secondId;
    parameters[2].buffer = &secondId;
    parameters[3].buffer = &firstId;

    if (mysql_stmt_bind_param(statement.get(), parameters) != 0 || mysql_stmt_execute(statement.get()) != 0) {
        throw std::runtime_error(std::string("Delete friend failed: ") + mysql_stmt_error(statement.get()));
    }

    const my_ulonglong affected = mysql_stmt_affected_rows(statement.get());

    if (affected == static_cast<my_ulonglong>(-1)) {
        throw std::runtime_error("Read deleted friendship count failed");
    }

    reply.result = affected == 0 ? DELETE_FRIEND_NOT_FRIEND : DELETE_FRIEND_SUCC;

    if (affected == 0) {
        // 原本就没有关系，不给陌生用户发送删除通知
        output.notifications.clear();
    }

    // 即使数据库已不存在关系，也同步清理旧的在线缓存
    output.removedFriendship = RemovedFriendship{
        *authenticatedUser,
            request.friendId
    };

    std::memcpy(output.body.data(),&reply,sizeof(reply));
    return output;
}

DispatchResult Dispatcher::handleGroupLeave(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const {
    if (!authenticatedUser || *authenticatedUser <= 0 || body.size() != sizeof(PROT_GROUP_LEAVE_RQ)) {
        throw std::runtime_error("Invalid group leave request");
    }

    PROT_GROUP_LEAVE_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.requestId == 0 || request.groupId <= 0) {
        throw std::runtime_error("Invalid group leave fields");
    }

    PROT_GROUP_LEAVE_RS reply{};
    reply.requestId = request.requestId;
    reply.groupId = request.groupId;

    DispatchResult output;
    output.body.resize(sizeof(reply));

    auto writeReply = [&](std::int32_t code) {
        reply.result = code;
        std::memcpy(output.body.data(),&reply,sizeof(reply));
    };

    writeReply(GROUP_LEAVE_FAIL);

    auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

    MYSQL* mysql = connection.get();
    bool transactionStarted = false;
    bool commitAttempted = false;

    try {
        if (mysql_query(mysql, "START TRANSACTION") != 0) {
            throw std::runtime_error(
                    std::string("Start group leave failed: ") +
                    mysql_error(mysql)
                    );
        }

        transactionStarted = true;

        auto reject = [&](std::int32_t code) {
            if (mysql_rollback(mysql) != 0) {
                throw std::runtime_error(std::string("Rollback group leave failed: ") + mysql_error(mysql));
            }

            transactionStarted = false;
            writeReply(code);
        };

        // 与入群事务使用相同的群记录锁
        auto ownerId = lockGroupOwner(mysql,request.groupId);

        if (!ownerId) {
            reject(GROUP_LEAVE_NOT_FOUND);
            return output;
        }

        if (*ownerId == *authenticatedUser) {
            reject(GROUP_LEAVE_OWNER);
            return output;
        }

        auto members = loadGroupMemberIds(mysql,request.groupId);

        if (std::find(members.begin(),members.end(),*authenticatedUser) == members.end()) {
            reject(GROUP_LEAVE_NOT_MEMBER);
            return output;
        }

        // 提交前准备通知内存
        PROT_GROUP_MEMBER_LEFT notification{};
        notification.groupId = request.groupId;
        notification.userId = *authenticatedUser;

        std::vector<char> notificationBody(sizeof(notification));
        std::memcpy(notificationBody.data(),&notification,sizeof(notification));

        for (std::int32_t memberId : members) {
            if (memberId != *authenticatedUser) {
                output.notifications.push_back(UserNotification{memberId,notificationBody});
            }
        }

        std::int32_t groupId = request.groupId;
        long long userId = *authenticatedUser;

        MYSQL_BIND parameters[2]{};

        parameters[0].buffer_type = MYSQL_TYPE_LONG;
        parameters[0].buffer = &groupId;
        parameters[0].buffer_length = sizeof(groupId);

        parameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
        parameters[1].buffer = &userId;
        parameters[1].buffer_length = sizeof(userId);

        executeGroupStatement(mysql,"DELETE FROM t_group_member WHERE group_id = ? AND user_id = ?",parameters);

        commitAttempted = true;

        if (mysql_commit(mysql) != 0) {
            throw std::runtime_error(std::string("Commit group leave failed: ") + mysql_error(mysql));
        }

        transactionStarted = false;

        // 复用已有标记：来源断线后仍发送成员变更通知
        output.groupMembershipCommitted = true;
        writeReply(GROUP_LEAVE_SUCC);
    }
    catch (const std::exception& error) {
        fprintf(stderr,"Group leave failed: %s\n",error.what());

        bool discardConnection = !transactionStarted;

        if (transactionStarted && mysql_rollback(mysql) != 0) {
            discardConnection = true;
        }

        if (commitAttempted) {
            discardConnection = true;
        }

        if (discardConnection) {
            m_mysqlPool.discard(connection);
        }

        output.notifications.clear();
        output.groupMembershipCommitted = false;
        writeReply(GROUP_LEAVE_FAIL);
    }

    return output;
}

DispatchResult Dispatcher::handleUpdateProfile(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const {
    if (!authenticatedUser || *authenticatedUser <= 0 || body.size() != sizeof(PROT_UPDATE_PROFILE_RQ)) {
        throw std::runtime_error("Invalid profile update request");
    }

    PROT_UPDATE_PROFILE_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.requestId == 0) {
        throw std::runtime_error("Invalid profile update request ID");
    }

    PROT_UPDATE_PROFILE_RS reply{};
    reply.requestId = request.requestId;

    auto makeReply = [&](std::int32_t code) {
        reply.result = code;
        DispatchResult output;
        output.body.resize(sizeof(reply));
        std::memcpy(output.body.data(),&reply,sizeof(reply));

        return output;
    };

    const char* nickEnd = static_cast<const char*>(std::memchr(request.nick, '\0', sizeof(request.nick)));

    const char* feelingEnd = static_cast<const char*>(std::memchr(request.feeling, '\0',sizeof(request.feeling)));

    if (!nickEnd || nickEnd == request.nick || !feelingEnd || request.iconId < 0 || request.iconId > 36) {
        return makeReply(UPDATE_PROFILE_INVALID);
    }

    std::string nick(request.nick,static_cast<std::size_t>(nickEnd - request.nick));

    std::string feeling(request.feeling,static_cast<std::size_t>(feelingEnd - request.feeling));

    // 复用已有 UTF-8 名称检查：拒绝控制字符、无效编码和全空白昵称
    if (!isValidGroupName(nick)) {
        return makeReply(UPDATE_PROFILE_INVALID);
    }

    auto connection = m_mysqlPool.acquire(
            std::chrono::milliseconds(2000)
            );

    MYSQL* mysql = connection.get();

    // 同一个用户保留自己的昵称是允许的
    auto existingId = findUserIdByNick(mysql, nick);

    if (existingId && *existingId != *authenticatedUser) {
        return makeReply(UPDATE_PROFILE_NICK_EXISTS);
    }

    // 同时确认用户仍然存在
    auto oldInfo = loadUserInfo(mysql, *authenticatedUser);

    if (oldInfo.size() != sizeof(PROT_FRIEND_INFO)) {
        throw std::runtime_error("Invalid current user information");
    }

    reply.iconId = request.iconId;
    std::memcpy(reply.nick,request.nick,sizeof(reply.nick));
    std::memcpy(reply.feeling,request.feeling,sizeof(reply.feeling));

    // 更新前先分配通知空间
    DispatchResult output = makeReply(UPDATE_PROFILE_FAIL);

    PROT_FRIEND_INFO info{};
    info.friendid = *authenticatedUser;
    info.iconid = request.iconId;
    info.status = FRIEND_STATUS_ONLINE;

    std::memcpy(info.nick, request.nick, sizeof(info.nick));
    std::memcpy(info.feeling,request.feeling,sizeof(info.feeling));
    std::vector<char> infoBody(sizeof(info));
    std::memcpy(infoBody.data(), &info, sizeof(info));

    auto friendIds = loadFriendIds(mysql,*authenticatedUser);

    for (std::int32_t friendId : friendIds) {
        output.notifications.push_back(UserNotification{friendId, infoBody});
    }

    constexpr const char* SQL =
        "UPDATE t_user "
        "SET nick = ?, iconid = ?, feeling = ? "
        "WHERE id = ?";

    std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql), &mysql_stmt_close);

    if (!statement) {
        throw std::runtime_error("mysql_stmt_init failed");
    }

    if (mysql_stmt_prepare(statement.get(),SQL,static_cast<unsigned long>(std::strlen(SQL))) != 0) {
        throw std::runtime_error(std::string("Prepare profile update failed: ") + mysql_stmt_error(statement.get()));
    }

    unsigned long nickLength = static_cast<unsigned long>(nick.size());

    unsigned long feelingLength = static_cast<unsigned long>(feeling.size());

    std::int32_t iconId = request.iconId;
    long long userId = *authenticatedUser;

    MYSQL_BIND parameters[4]{};

    parameters[0].buffer_type = MYSQL_TYPE_STRING;
    parameters[0].buffer = nick.data();
    parameters[0].buffer_length = nickLength;
    parameters[0].length = &nickLength;

    parameters[1].buffer_type = MYSQL_TYPE_LONG;
    parameters[1].buffer = &iconId;
    parameters[1].buffer_length = sizeof(iconId);

    parameters[2].buffer_type = MYSQL_TYPE_STRING;
    parameters[2].buffer = feeling.data();
    parameters[2].buffer_length = feelingLength;
    parameters[2].length = &feelingLength;

    parameters[3].buffer_type = MYSQL_TYPE_LONGLONG;
    parameters[3].buffer = &userId;
    parameters[3].buffer_length = sizeof(userId);

    if (mysql_stmt_bind_param(statement.get(), parameters) != 0) {
        throw std::runtime_error(std::string("Bind profile update failed: ") + mysql_stmt_error(statement.get()));
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        // 1062：唯一键冲突，处理并发修改昵称的情况
        reply.result = mysql_stmt_errno(statement.get()) == 1062 ? UPDATE_PROFILE_NICK_EXISTS : UPDATE_PROFILE_FAIL;
        fprintf(stderr,"Update profile failed: %s\n",mysql_stmt_error(statement.get()));
        output.notifications.clear();
    } 
    else {
        reply.result = UPDATE_PROFILE_SUCC;
        output.profileUpdated = true;
    }

    std::memcpy(output.body.data(),&reply,sizeof(reply));
    return output;
}

DispatchResult Dispatcher::handleChangePassword(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const {
    if (!authenticatedUser || *authenticatedUser <= 0 ||
            body.size() != sizeof(PROT_CHANGE_PASSWORD_RQ)) {
        throw std::runtime_error("Invalid password change request");
    }

    PROT_CHANGE_PASSWORD_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.requestId == 0) {
        throw std::runtime_error("Invalid password change request ID");
    }

    auto makeReply = [&](std::int32_t code) {
        PROT_CHANGE_PASSWORD_RS reply{};
        reply.requestId = request.requestId;
        reply.result = code;

        DispatchResult output;
        output.body.resize(sizeof(reply));

        std::memcpy(output.body.data(),&reply,sizeof(reply));

        return output;
    };

    const char* oldEnd = static_cast<const char*>(std::memchr(request.oldPassword,'\0',sizeof(request.oldPassword)));

    const char* newEnd = static_cast<const char*>(std::memchr(request.newPassword,'\0',sizeof(request.newPassword)));

    if (!oldEnd || oldEnd == request.oldPassword || !newEnd) {
        return makeReply(CHANGE_PASSWORD_INVALID);
    }

    const std::string oldPassword(request.oldPassword,static_cast<std::size_t>(oldEnd - request.oldPassword));

    const std::string newPassword(request.newPassword,static_cast<std::size_t>(newEnd - request.newPassword));

    if (newPassword.size() < 6 || newPassword.size() > 14) {
        return makeReply(CHANGE_PASSWORD_INVALID);
    }

    try {
        auto connection = m_mysqlPool.acquire(std::chrono::milliseconds(2000));

        MYSQL* mysql = connection.get();
        long long userId = *authenticatedUser;

        std::unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> statement(mysql_stmt_init(mysql),&mysql_stmt_close);

        if (!statement) {
            throw std::runtime_error("Initialize password query failed");
        }

        constexpr const char* SELECT_SQL =
            "SELECT pass_hash FROM t_user "
            "WHERE id = ? LIMIT 1";

        if (mysql_stmt_prepare(statement.get(),SELECT_SQL,static_cast<unsigned long>(std::strlen(SELECT_SQL))) != 0) {
            throw std::runtime_error("Prepare password query failed");
        }

        MYSQL_BIND parameter{};
        parameter.buffer_type = MYSQL_TYPE_LONGLONG;
        parameter.buffer = &userId;
        parameter.buffer_length = sizeof(userId);

        if (mysql_stmt_bind_param(statement.get(), &parameter) != 0 || mysql_stmt_execute(statement.get()) != 0) {
            throw std::runtime_error("Execute password query failed");
        }

        char hashBuffer[256]{};
        unsigned long hashLength = 0;
        bool hashIsNull = false;

        MYSQL_BIND column{};
        column.buffer_type = MYSQL_TYPE_STRING;
        column.buffer = hashBuffer;
        column.buffer_length = sizeof(hashBuffer);
        column.length = &hashLength;
        column.is_null = &hashIsNull;

        if (mysql_stmt_bind_result(statement.get(), &column) != 0 || mysql_stmt_store_result(statement.get()) != 0) {
            throw std::runtime_error("Read password query failed");
        }

        if (mysql_stmt_num_rows(statement.get()) == 0) {
            return makeReply(CHANGE_PASSWORD_FAIL);
        }

        if (mysql_stmt_fetch(statement.get()) != 0) {
            throw std::runtime_error("Fetch password hash failed");
        }

        if (hashIsNull || hashLength == 0 || hashLength >= sizeof(hashBuffer)) {
            return makeReply(CHANGE_PASSWORD_FAIL);
        }

        std::string oldHash(hashBuffer, hashLength);

        // 先释放查询结果，避免下一条语句出现 commands out of sync
        statement.reset();

        if (!verifyPassword(oldPassword, oldHash)) {
            return makeReply(CHANGE_PASSWORD_OLD_WRONG);
        }

        if (oldPassword == newPassword) {
            return makeReply(CHANGE_PASSWORD_SAME);
        }

        std::string newHash = hashPassword(newPassword);
        statement.reset(mysql_stmt_init(mysql));

        if (!statement) {
            throw std::runtime_error("Initialize password update failed");
        }

        constexpr const char* UPDATE_SQL =
            "UPDATE t_user SET pass_hash = ?, pass = '' "
            "WHERE id = ? AND pass_hash = ?";

        if (mysql_stmt_prepare(statement.get(),UPDATE_SQL,static_cast<unsigned long>(std::strlen(UPDATE_SQL))) != 0) {
            throw std::runtime_error("Prepare password update failed");
        }

        unsigned long newHashLength = static_cast<unsigned long>(newHash.size());

        unsigned long oldHashLength = static_cast<unsigned long>(oldHash.size());

        MYSQL_BIND parameters[3]{};

        parameters[0].buffer_type = MYSQL_TYPE_STRING;
        parameters[0].buffer = newHash.data();
        parameters[0].buffer_length = newHashLength;
        parameters[0].length = &newHashLength;

        parameters[1].buffer_type = MYSQL_TYPE_LONGLONG;
        parameters[1].buffer = &userId;
        parameters[1].buffer_length = sizeof(userId);

        parameters[2].buffer_type = MYSQL_TYPE_STRING;
        parameters[2].buffer = oldHash.data();
        parameters[2].buffer_length = oldHashLength;
        parameters[2].length = &oldHashLength;

        if (mysql_stmt_bind_param(statement.get(), parameters) != 0 || mysql_stmt_execute(statement.get()) != 0) {
            throw std::runtime_error("Execute password update failed");
        }

        const my_ulonglong affected = mysql_stmt_affected_rows(statement.get());

        if (affected == 1) {
            return makeReply(CHANGE_PASSWORD_SUCC);
        }

        if (affected == 0) {
            // 查询后密码已被另一个请求修改
            return makeReply(CHANGE_PASSWORD_OLD_WRONG);
        }

        return makeReply(CHANGE_PASSWORD_FAIL);
    }
    catch (const std::exception&) {
        // 不输出密码、哈希或可能包含敏感内容的异常文本
        fprintf(stderr, "Password change operation failed\n");

        return makeReply(CHANGE_PASSWORD_FAIL);
    }
}
