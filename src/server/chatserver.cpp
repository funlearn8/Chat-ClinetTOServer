#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <functional>
#include <string>
#include <muduo/base/Logging.h>
using namespace std;
using namespace placeholders;
using json = nlohmann::json;

// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册连接事件的回调函数
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册消息事件的回调函数
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置subLoop线程数量
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

// 连接事件相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开连接
    if (!conn->connected())
    {
        // 处理客户端异常退出事件
        ChatService::instance()->clientCloseExceptionHandler(conn);
        // 半关闭
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
        // 1. 检查最小长度
    if (buffer->readableBytes() < 3) {
        LOG_WARN << "Data too short";
        return;
    }

    // 2. 查找消息边界
    const char *data = buffer->peek();
    size_t len = 0;
    while (len < buffer->readableBytes() && data[len] != '\0') {
        ++len;
    }

    // 3. 验证消息完整性
    if (len >= buffer->readableBytes() || data[len] != '\0') {
        LOG_WARN << "Invalid message terminator";
        conn->shutdown();
        return;
    }

    // 4. 提取数据
    std::string buf(data, len);
    buffer->retrieve(len + 1);

    try {
        // 5. 安全解析
        json js = json::parse(buf, nullptr, false);
        if (js.is_discarded()) {
            printf("-------");
        }
        
        // 6. 强制字段校验
        if (!js.contains("msgid") || !js["msgid"].is_number_integer()) {
            throw json::type_error::create(302, "field must be string");
        }

        // 7. 业务处理
        auto handler = ChatService::instance()->getHandler(js["msgid"].get<int>());
        handler(conn, js, time);

    } catch (const json::exception &e) {
        LOG_ERROR << "JSON error: " << e.what() 
                 << "\nRaw data: " << buf;
        conn->send("{\"error\":\"invalid message\"}");
    }
}