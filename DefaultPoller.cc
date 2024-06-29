#include "EPollPoller.h"
#include "Poller.h"

#include <stdlib.h>

// 静态成员函数，根据环境变量决定返回哪种Poller实例
Poller *Poller::newDefaultPoller(EventLoop *loop) {
  if (::getenv("MUDUO_USE_POLL")) {
    // 如果环境变量"MUDUO_USE_POLL"被设置，则返回nullptr，表示使用poll
    return nullptr; // 生成poll的实例（假设Poller的其他实现）
  } else {
    // 否则，返回EPollPoller的实例，表示使用epoll
    return new EPollPoller(loop); // 生成epoll的实例
  }
}
