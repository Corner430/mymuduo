#pragma once
#include "noncopyable.h"

/*
 * 事件循环类，包含两个大模块：
 * 1. Channel : 封装了 fd 和 fd 上感兴趣以及发生的事件
 * 2. Poller : epoll 的抽象类
 */
class EventLoop : noncopyable {
public:
private:
};