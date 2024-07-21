#pragma once
#include "noncopyable.h"

/*
 * 事件循环类，包含两个大模块：
 * - Channel
 *    1. 封装了 sockfd 和 sockfd 上感兴趣以及发生的事件
 *    2. 绑定了 Poller 返回的具体事件
 *    3. 负责调用具体事件的回调操作(因为它可以获知 fd 最终发生的具体事件revents)
 * - Poller : epoll 的抽象类
 */
class EventLoop : noncopyable {
public:
private:
};