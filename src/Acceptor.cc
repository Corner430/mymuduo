#include "Acceptor.h"
#include "InetAddress.h"
#include "Logger.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int createNonblocking() {
  int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (sockfd < 0)
    LOG_FATAL("%s:%s:%d listen socket create err:%d \n", __FILE__, __FUNCTION__,
              __LINE__, errno);
  return sockfd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr,
                   bool reuseport)
    : loop_(loop), acceptSocket_(createNonblocking()),
      acceptChannel_(loop /*baseloop*/, acceptSocket_.fd()) /*注册到 Poller */,
      listenning_(false) {
  acceptSocket_.setReuseAddr(true);      // 设置地址重用
  acceptSocket_.setReusePort(reuseport); // 设置端口重用
  acceptSocket_.bindAddress(listenAddr); // 绑定监听地址和端口

  // 设置 acceptChannel 的读事件回调函数为 handleRead，处理新连接事件
  acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor() {
  acceptChannel_.disableAll();
  acceptChannel_.remove();
}

void Acceptor::listen() {
  listenning_ = true;
  acceptSocket_.listen();         // listen
  acceptChannel_.enableReading(); // 开启 acceptChannel 的读事件，并注册到
                                  // EventLoop(Poller) 中
}

/* 1. 从 listenfd 上 accept 新的连接
 * 2. 将新连接的 fd 设置为非阻塞模式
 * 3. 将新连接加入到 SubReactor 的 EventLoop 中 */
void Acceptor::handleRead() {
  InetAddress peerAddr;
  int connfd = acceptSocket_.accept(&peerAddr);
  if (connfd >= 0)
    if (newConnectionCallback_)
      newConnectionCallback_(connfd, peerAddr);
    else
      ::close(connfd);
  else {
    LOG_ERROR("%s:%s:%d accept err:%d \n", __FILE__, __FUNCTION__, __LINE__,
              errno);
    if (errno == EMFILE)
      LOG_ERROR("%s:%s:%d sockfd reached limit! \n", __FILE__, __FUNCTION__,
                __LINE__);
  }
}