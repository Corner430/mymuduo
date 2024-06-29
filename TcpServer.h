#pragma once

/**
 * 用户使用muduo编写服务器程序
 * 此类提供了一个使用Muduo库创建TCP服务器的框架。
 * 它处理传入连接，管理连接回调，并支持多线程。
 */
#include "Acceptor.h"
#include "Buffer.h"
#include "Callbacks.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "InetAddress.h"
#include "TcpConnection.h"
#include "noncopyable.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// 对外的服务器编程使用的类
class TcpServer : noncopyable {
public:
  // 用于初始化线程的回调函数类型别名，传入EventLoop指针。
  using ThreadInitCallback = std::function<void(EventLoop *)>;

  // 端口重用选项的枚举。
  enum Option {
    kNoReusePort, // 不重用端口。
    kReusePort,   // 重用端口。
  };

  // 构造函数，用主EventLoop、监听地址、服务器名称和端口重用选项初始化TcpServer。
  TcpServer(EventLoop *loop, const InetAddress &listenAddr,
            const std::string &nameArg, Option option = kNoReusePort);

  // 析构函数。
  ~TcpServer();

  // 设置线程初始化回调函数。
  void setThreadInitCallback(const ThreadInitCallback &cb) {
    threadInitCallback_ = cb;
  }

  // 设置新连接的回调函数。
  void setConnectionCallback(const ConnectionCallback &cb) {
    connectionCallback_ = cb;
  }

  // 设置消息接收的回调函数。
  void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }

  // 设置写完成的回调函数。
  void setWriteCompleteCallback(const WriteCompleteCallback &cb) {
    writeCompleteCallback_ = cb;
  }

  // 设置底层subloop的个数
  void setThreadNum(int numThreads);

  // 开启服务器监听
  void start();

private:
  // 处理一个新的连接，传入socket文件描述符和对端地址。
  void newConnection(int sockfd, const InetAddress &peerAddr);

  // 移除一个连接。
  void removeConnection(const TcpConnectionPtr &conn);

  // 在EventLoop中移除一个连接。
  void removeConnectionInLoop(const TcpConnectionPtr &conn);

  // 连接映射表的类型别名，键为字符串标识符。
  using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

  EventLoop *loop_; // the acceptor loop

  const std::string ipPort_; // 服务器监听的IP和端口。
  const std::string name_;   // 服务器的名称。

  std::unique_ptr<Acceptor> acceptor_; // 运行在mainLoop，任务就是监听新连接事件

  std::shared_ptr<EventLoopThreadPool> threadPool_; // one loop per thread
  // EventLoopThreadPool的共享指针，它管理多个EventLoop以在不同线程中处理连接。

  ConnectionCallback connectionCallback_; // 有新连接时的回调
  // 新连接的回调函数。
  MessageCallback messageCallback_; // 有读写消息时的回调
  // 消息接收的回调函数。
  WriteCompleteCallback writeCompleteCallback_; // 消息发送完成以后的回调
  // 写完成的回调函数。

  ThreadInitCallback threadInitCallback_; // loop线程初始化的回调
  // 线程初始化的回调函数。

  std::atomic_int started_; // 标记服务器是否已启动的原子整数。

  int nextConnId_;            // 下一个连接的ID。
  ConnectionMap connections_; // 保存所有的连接
};
