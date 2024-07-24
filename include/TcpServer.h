#pragma once

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

/*
 * TCP server, supports single-threaded and thread-pool models.
 *
 * This is an interface class, so don't expose too much details.
 * 供外界服务器编程使用 */
class TcpServer : noncopyable {
public:
  using ThreadInitCallback = std::function<void(EventLoop *)>;

  enum Option {
    kNoReusePort,
    kReusePort,
  };

  TcpServer(EventLoop *loop, const InetAddress &listenAddr,
            const std::string &nameArg, Option option = kNoReusePort);

  ~TcpServer(); // force out-line dtor, for std::unique_ptr members.

  // 设置线程初始化回调
  void setThreadInitCallback(const ThreadInitCallback &cb) {
    threadInitCallback_ = cb;
  }

  /* Set connection callback. Not thread safe. */
  // 设置新连接的回调
  void setConnectionCallback(const ConnectionCallback &cb) {
    connectionCallback_ = cb;
  }

  /* Set message callback. Not thread safe. */
  // 设置消息接收的回调
  void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }

  /* Set write complete callback. Not thread safe. */
  // 设置写完成的回调函数
  void setWriteCompleteCallback(const WriteCompleteCallback &cb) {
    writeCompleteCallback_ = cb;
  }

  /* Set the number of threads for handling input.
   *
   * Always accepts new connection in loop's thread.
   * Must be called before @c start
   * @param numThreads
   * - 0 means all I/O in loop's thread, no thread will created.
   *   this is the default value.
   * - 1 means all I/O in another thread.
   * - N means a thread pool with N threads, new connections
   *   are assigned on a round-robin basis.
   * 设置底层 subloop 的个数 */
  void setThreadNum(int numThreads);

  /* Starts the server if it's not listening.
   *
   * It's harmless to call it multiple times.
   * Thread safe.
   * 开启服务器监听 */
  void start();

private:
  /* Not thread safe, but in loop */
  void newConnection(int sockfd, const InetAddress &peerAddr);

  /* Thread safe. */
  void removeConnection(const TcpConnectionPtr &conn);

  /* Not thread safe, but in loop */
  void removeConnectionInLoop(const TcpConnectionPtr &conn);

  using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

  EventLoop *loop_; // the acceptor loop(即 mainLoop)

  const std::string ipPort_; // 服务器监听的 IP 和端口
  const std::string name_;   // 服务器的名称

  /* avoid revealing Acceptor */
  std::unique_ptr<Acceptor> acceptor_; // 运行在 mainLoop，监听新连接事件

  // EventLoopThreadPool 的共享指针，它管理多个 EventLoop 以在不同线程中处理连接
  std::shared_ptr<EventLoopThreadPool> threadPool_; // one loop per thread

  ConnectionCallback connectionCallback_;       // 新连接回调
  MessageCallback messageCallback_;             // 读写消息回调
  WriteCompleteCallback writeCompleteCallback_; // 写完成回调

  ThreadInitCallback threadInitCallback_; // loop 线程初始化回调

  std::atomic_int started_; // 标记服务器是否已启动

  int nextConnId_;            // 下一个连接的 ID
  ConnectionMap connections_; // 保存所有的连接
};