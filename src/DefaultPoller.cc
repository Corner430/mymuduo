#include "EPollPoller.h"
#include "Poller.h"

#include <stdlib.h>

/* 静态成员函数，根据环境变量决定使用的 Poller 实例
 * 默认使用 EPollPoller，即 epoll
 */
Poller *Poller::newDefaultPoller(EventLoop *loop) {
  return ::getenv("MUDUO_USE_POLL") ? nullptr : new EPollPoller(loop);
}