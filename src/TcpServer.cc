#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

#include <functional>
#include <strings.h>

static EventLoop *CheckLoopNotNull(EventLoop *loop) {
  if (!loop)
    LOG_FATAL("%s:%s:%d mainLoop is null! \n", __FILE__, __FUNCTION__,
              __LINE__);
  return loop;
}

/*
 * 服务器的构造函数
 * 1. 创建一个 Acceptor 对象，用于监听新的连接
 * 2. 创建一个 EventLoopThreadPool 对象，用于管理 subloop
 */
TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr,
                     const std::string &nameArg, Option option)
    : loop_(CheckLoopNotNull(loop)), ipPort_(listenAddr.toIpPort()),
      name_(nameArg),
      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
      threadPool_(new EventLoopThreadPool(loop, name_)), connectionCallback_(),
      messageCallback_(), nextConnId_(1), started_(0) {
  // 当有先用户连接时，会执行 TcpServer::newConnection
  acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
                                                std::placeholders::_1,
                                                std::placeholders::_2));
}

TcpServer::~TcpServer() {
  for (auto &item : connections_) {
    // 这个局部的 shared_ptr 智能指针对象，出右括号，
    // 可以自动释放 new 出来的 TcpConnection 对象资源
    TcpConnectionPtr conn(item.second);
    item.second.reset(); // 不再使用这个 shared_ptr

    conn->getLoop()->runInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

// 设置 subloop 的个数
void TcpServer::setThreadNum(int numThreads) {
  threadPool_->setThreadNum(numThreads);
}

// 开启服务器监听
void TcpServer::start() {
  if (started_++ == 0) { // 防止一个 TcpServer 对象被 start 多次
    threadPool_->start(threadInitCallback_); // 启动底层的 loop 线程池
    loop_->runInLoop(/* bind() 依托于对象，所以需要 get() */
                     std::bind(&Acceptor::listen, acceptor_.get()));
  }
}

// 当有一个新的客户端连接时，acceptor 会调用这个回调函数
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr) {
  // 使用轮询算法，从线程池中选择一个事件循环（EventLoop）来管理新的 channel
  EventLoop *ioLoop = threadPool_->getNextLoop();

  // 生成一个新的连接名称，用于标识新的连接
  char buf[64] = {0};
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  std::string connName = name_ + buf;

  LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
           name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

  // 通过 sockfd 获取其绑定的本地 IP 地址和端口信息
  sockaddr_in local;
  ::bzero(&local, sizeof local);
  socklen_t addrlen = sizeof local;
  if (::getsockname(sockfd, (sockaddr *)&local, &addrlen) < 0)
    LOG_ERROR("sockets::getLocalAddr");
  InetAddress localAddr(local);

  // 根据连接成功的 sockfd，创建 TcpConnection 对象
  TcpConnectionPtr conn(
      new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
  connections_[connName] = conn;

  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);

  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

  /* 在 ioLoop 中执行的操作：
   *   1. 将 channel 和 TcpConnection 绑定
   *   2. 启用 channel 的读事件
   *   3. 调用 connectionCallback_ 回调函数
   */
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn) {
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn) {
  LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n",
           name_.c_str(), conn->name().c_str());

  connections_.erase(conn->name());
  EventLoop *ioLoop = conn->getLoop();
  ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}