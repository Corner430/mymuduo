#pragma once

#include <functional>
#include <memory>

class Buffer;
class TcpConnection;
class Timestamp;

// 定义TcpConnection的智能指针类型。
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

// 定义连接回调函数类型，参数是TcpConnection的智能指针。
using ConnectionCallback = std::function<void(const TcpConnectionPtr &)>;

// 定义关闭回调函数类型，参数是TcpConnection的智能指针。
using CloseCallback = std::function<void(const TcpConnectionPtr &)>;

// 定义写完成回调函数类型，参数是TcpConnection的智能指针。
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr &)>;

// 定义消息回调函数类型，参数是TcpConnection的智能指针、Buffer指针和Timestamp。
using MessageCallback =
    std::function<void(const TcpConnectionPtr &, Buffer *, Timestamp)>;

// 定义高水位标记回调函数类型，参数是TcpConnection的智能指针和大小（size_t）。
using HighWaterMarkCallback =
    std::function<void(const TcpConnectionPtr &, size_t)>;
