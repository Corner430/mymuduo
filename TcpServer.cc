#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

#include <functional>
#include <strings.h>

static EventLoop *CheckLoopNotNull(EventLoop *loop) {
  if (loop == nullptr) {
    LOG_FATAL("%s:%s:%d mainLoop is null! \n", __FILE__, __FUNCTION__,
              __LINE__);
  }
  return loop;
}

/*
 * 服务器的构造函数
 * 1. 创建一个Acceptor对象，用于监听新的连接
 * 2. 创建一个EventLoopThreadPool对象，用于管理subloop
 */
TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr,
                     const std::string &nameArg, Option option)
    : loop_(CheckLoopNotNull(loop)), ipPort_(listenAddr.toIpPort()),
      name_(nameArg),
      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
      threadPool_(new EventLoopThreadPool(loop, name_)), connectionCallback_(),
      messageCallback_(), nextConnId_(1), started_(0) {
  // 当有先用户连接时，会执行TcpServer::newConnection回调
  acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
                                                std::placeholders::_1,
                                                std::placeholders::_2));
}

TcpServer::~TcpServer() {
  for (auto &item : connections_) {
    // 这个局部的shared_ptr智能指针对象，出右括号，可以自动释放new出来的TcpConnection对象资源了
    TcpConnectionPtr conn(item.second);
    item.second.reset();

    // 销毁连接
    conn->getLoop()->runInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

// 设置底层subloop的个数
void TcpServer::setThreadNum(int numThreads) {
  threadPool_->setThreadNum(numThreads);
}

// 开启服务器监听   loop.loop()
void TcpServer::start() {
  if (started_++ == 0) // 防止一个TcpServer对象被start多次
  {
    threadPool_->start(threadInitCallback_); // 启动底层的loop线程池
    loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
  }
}

// 当有一个新的客户端连接时，acceptor会调用这个回调函数
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr) {
  // 使用轮询算法，从线程池中选择一个事件循环（EventLoop）来管理新的channel
  EventLoop *ioLoop = threadPool_->getNextLoop();

  // 生成一个新的连接名称，用于标识新的连接
  char buf[64] = {0};
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  std::string connName = name_ + buf;

  // 记录日志，表示有新的连接到达
  LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
           name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

  // 通过sockfd获取其绑定的本地IP地址和端口信息
  sockaddr_in local;
  ::bzero(&local, sizeof local);    // 将本地地址结构清零
  socklen_t addrlen = sizeof local; // 设置地址长度
  if (::getsockname(sockfd, (sockaddr *)&local, &addrlen) < 0) { // 获取本地地址
    LOG_ERROR("sockets::getLocalAddr"); // 如果获取失败，记录错误日志
  }
  InetAddress localAddr(local); // 将本地地址转换为InetAddress对象

  // 根据连接成功的sockfd，创建TcpConnection连接对象
  TcpConnectionPtr conn(new TcpConnection(ioLoop, connName,
                                          sockfd, // 传入socket文件描述符
                                          localAddr,
                                          peerAddr)); // 传入本地和远端地址
  connections_[connName] = conn; // 将新建的连接对象存储在connections_映射中

  // 设置各种回调函数，这些回调函数会在特定事件发生时调用
  conn->setConnectionCallback(connectionCallback_); // 设置连接建立回调
  conn->setMessageCallback(messageCallback_);       // 设置消息接收回调
  conn->setWriteCompleteCallback(
      writeCompleteCallback_); // 设置消息发送完成回调

  // 设置关闭连接的回调函数，当连接关闭时会调用TcpServer::removeConnection
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

  // 在ioLoop中运行TcpConnection::connectEstablished方法，通知连接已建立
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