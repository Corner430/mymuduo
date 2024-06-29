#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "CurrentThread.h"
#include "Timestamp.h"
#include "noncopyable.h"

class Channel;
class Poller;

// 事件循环类，负责管理和调度事件。
class EventLoop : noncopyable {
public:
  using Functor = std::function<void()>; // 定义回调函数类型别名

  EventLoop();  // 构造函数，初始化事件循环
  ~EventLoop(); // 析构函数

  // 开启事件循环
  void loop();

  // 退出事件循环
  void quit();

  // 获取Poller返回的时间戳
  Timestamp pollReturnTime() const { return pollReturnTime_; }

  // 在当前loop中执行cb
  void runInLoop(Functor cb);

  // 把cb放入队列中，唤醒loop所在的线程，执行cb
  void queueInLoop(Functor cb);

  // 唤醒loop所在的线程
  void wakeup();

  // EventLoop的方法 => Poller的方法
  // 更新Poller中的Channel
  void updateChannel(Channel *channel);

  // 从Poller中移除Channel
  void removeChannel(Channel *channel);

  // 检查Poller是否有该Channel
  bool hasChannel(Channel *channel);

  // 判断EventLoop对象是否在自己的线程里面
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

private:
  // 处理唤醒事件
  void handleRead();

  // 执行所有待处理的回调函数
  void doPendingFunctors();

  using ChannelList = std::vector<Channel *>; // 定义Channel列表的类型别名

  std::atomic_bool looping_; // 原子操作，标识是否正在循环，通过CAS实现的
  std::atomic_bool quit_; // 标识退出loop循环

  const pid_t threadId_; // 记录当前loop所在线程的ID

  Timestamp pollReturnTime_; // poller返回发生事件的channels的时间点
  std::unique_ptr<Poller> poller_; // Poller的智能指针

  int wakeupFd_; // 当mainLoop获取一个新用户的channel，通过轮询算法选择一个subloop，通过该成员唤醒subloop处理channel
  std::unique_ptr<Channel> wakeupChannel_; // 唤醒channel的智能指针

  ChannelList activeChannels_; // 当前活跃的Channel列表

  std::atomic_bool
      callingPendingFunctors_; // 标识当前loop是否有需要执行的回调操作
  std::vector<Functor> pendingFunctors_; // 存储loop需要执行的所有回调操作
  std::mutex mutex_; // 互斥锁，用来保护pendingFunctors_的线程安全操作
};
