#pragma once

#include <cstddef>
#include <vector>
#include <cstdint>
#include <queue>
#include <utility>

class Connection{
public:
    explicit Connection(int fd,std::uint64_t id):m_fd(fd),m_id(id){}

    int fd() const {
        return m_fd;
    }

    //将本次收到的数据追加到连接自己的缓冲区
    void appendReceived(const char* data,std::size_t length){
        m_recvBuffer.insert(m_recvBuffer.end(),data,data+length);
    }

    const std::vector<char>& recvBuffer() const{
        return m_recvBuffer;
    }

    //移除缓冲区开头已经处理完的数据，调用前必须确保length不超过缓冲区大小
    void consumeReceived(std::size_t length){
        m_recvBuffer.erase(m_recvBuffer.begin(),m_recvBuffer.begin()+static_cast<std::vector<char>::difference_type>(length));
    }

    std::uint64_t id()const{
        return m_id;
    }

    //将新数据追加到发送缓冲区
    //待发送数据超过限制时，返回 false
    bool appendToSend(const char* data,std::size_t length){
        constexpr std::size_t MAX_PENDING_BYTES = 1024*1024;

        std::size_t pending = pendingSendBytes();

        if(length > MAX_PENDING_BYTES - pending){
            return false;
        }

        //追加前，清理已经发送的前缀
        if(m_sendOffset != 0){
            using Difference = std::vector<char>::difference_type;

            m_sendBuffer.erase(m_sendBuffer.begin(),m_sendBuffer.begin() + static_cast<Difference>(m_sendOffset));

            m_sendOffset = 0;
        }

        if(length!=0){
            m_sendBuffer.insert(m_sendBuffer.end(),data,data+length);
        }
        return true;
    }

    //当前还有多少个字节没有发送
    std::size_t pendingSendBytes() const{
        return m_sendBuffer.size() - m_sendOffset;
    }

    //获取下一次发送的起始位置，只在 pendingSendBytes > 0 时调用
    const char* pendingSendData() const{
        return m_sendBuffer.data() + m_sendOffset;
    }

    //标记实际已经发送的字节数，调用时保证 length <= pendingSendBytes()
    void consumeSent(std::size_t length){
        m_sendOffset += length;

        if(m_sendOffset == m_sendBuffer.size()){
            m_sendBuffer.clear();
            m_sendOffset = 0;
        }
    }

    //进入任务队列接口
    bool enqueMessage(std::vector<char> body){
        constexpr std::size_t MAX_PENDING_MESSAGES = 64;

        if(m_pendingMessages.size() >= MAX_PENDING_MESSAGES){
            return false;
        }

        m_pendingMessages.push(std::move(body));
        return true;
    }
    
    //领取下一个任务接口
    bool beginNextMessage(std::vector<char>& body){
        //当前没有任务正在处理且任务队列非空，取出下一条消息
        if(m_businessBusy || m_pendingMessages.empty()){
            return false;
        }

        body = std::move(m_pendingMessages.front());
        m_pendingMessages.pop();

        m_businessBusy = true;
        return true;
    }

    //任务完成接口
    void finishMessage(){
        m_businessBusy = false;
    }

private:
    int m_fd;
    std::uint64_t m_id;
    std::vector<char> m_recvBuffer;

    //待发送数据
    std::vector<char> m_sendBuffer;
    //已成功发送字节数
    std::size_t m_sendOffset = 0;

    //已拆包、等待业务处理的消息体
    std::queue<std::vector<char>> m_pendingMessages;
    //当前连接是否已有业务任务在执行或等待处理结果
    bool m_businessBusy = false;
};
