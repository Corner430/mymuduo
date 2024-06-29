#include "Acceptor.h"
#include "InetAddress.h"
#include "Logger.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int createNonblocking() {
  int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (sockfd < 0) {
    LOG_FATAL("%s:%s:%d listen socket create err:%d \n", __FILE__, __FUNCTION__,
              __LINE__, errno);
  }
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr,
                   bool reuseport)
    : loop_(loop),                        // 所属的 EventLoop 对象
      acceptSocket_(createNonblocking()), // 创建非阻塞的 accept socket
      acceptChannel_(
          loop, acceptSocket_.fd()), // 将 accept socket 注册到 EventLoop 中
      listenning_(false)             // 初始状态为未监听
{
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
  acceptChannel_.enableReading(); // acceptChannel_ => Poller
}

// listenfd有事件发生了，就是有新用户连接了
void Acceptor::handleRead() {
  InetAddress peerAddr;
  int connfd = acceptSocket_.accept(&peerAddr);
  if (connfd >= 0) {
    if (newConnectionCallback_) {
      newConnectionCallback_(
          connfd,
          peerAddr); // 轮询找到subLoop，唤醒，分发当前的新客户端的Channel
    } else {
      ::close(connfd);
    }
  } else {
    LOG_ERROR("%s:%s:%d accept err:%d \n", __FILE__, __FUNCTION__, __LINE__,
              errno);
    if (errno == EMFILE) {
      LOG_ERROR("%s:%s:%d sockfd reached limit! \n", __FILE__, __FUNCTION__,
                __LINE__);
    }
  }
}