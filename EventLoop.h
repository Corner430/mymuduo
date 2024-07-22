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

/*
 * Reactor, at most one per thread.
 * This is an interface class, so don't expose too much details.
 *
 * 事件循环类，包含两个大模块：
 * - Channel
 *    1. 封装了 sockfd 和 sockfd 上感兴趣以及发生的事件
 *    2. 绑定了 Poller 返回的具体事件
 *    3. 负责调用具体事件的回调操作(因为它可以获知 fd 最终发生的具体事件revents)
 * - Poller : epoll 的抽象类
 */
class EventLoop : noncopyable {
public:
  using Functor = std::function<void()>; // 定义回调函数类型别名

  EventLoop();
  ~EventLoop(); // force out-line dtor, for std::unique_ptr members.

  /* Loops forever.
   *
   * Must be called in the same thread as creation of the object. */
  void loop();

  /* Quits loop.
   *
   * This is not 100% thread safe, if you call through a raw pointer,
   * better to call through shared_ptr<EventLoop> for 100% safety. */
  void quit();

  /* Time when poll returns, usually means data arrival. */
  Timestamp pollReturnTime() const { return pollReturnTime_; }

  /* Runs callback immediately in the loop thread.
   * It wakes up the loop, and run the cb.
   * If in the same loop thread, cb is run within the function.
   * Safe to call from other threads. */
  void runInLoop(Functor cb); // 在当前 loop 线程中执行 cb
  /* Queues callback in the loop thread.
   * Runs after finish pooling.
   * Safe to call from other threads.
   * 把 cb 放入 pendingFunctors_ 中，等待 loop 线程处理 */
  void queueInLoop(Functor cb);

  /* internal usage */
  void wakeup(); // mainLoop 唤醒 subLoop，即唤醒 loop 所在线程
  void updateChannel(Channel *channel); // 更新 Poller 中的 channel
  void removeChannel(Channel *channel); // 从 Poller 中移除 channel
  bool hasChannel(Channel *channel);    // 检查 Poller 是否有该 channel

  // 判断 EventLoop 对象是否在当前线程内
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

private:
  void handleRead();        // waked up
  void doPendingFunctors(); // 执行所有待处理的回调函数

  using ChannelList = std::vector<Channel *>; // 定义 channel 列表的类型别名

  std::atomic_bool looping_;
  std::atomic_bool quit_;
  const pid_t threadId_;     // 记录当前 loop 所在线程的 ID
  Timestamp pollReturnTime_; // Poller 返回发生事件的 channels 的时间点
  std::unique_ptr<Poller> poller_; // Poller 的智能指针

  /* 当 mainLoop 获取一个新用户的 channel，通过轮询算法选择一个 subloop，
   * 通过该成员唤醒 subloop 处理 channel
   * 本质上是一个 eventfd，用于唤醒 subloop */
  int wakeupFd_;
  /* unlike in TimerQueue, which is an internal class,
   * we don't expose Channel to client.
   *
   * 对 wakeupFd_ 的封装 */
  std::unique_ptr<Channel> wakeupChannel_;

  // scratch variables
  ChannelList activeChannels_; // 当前活跃的 channel 列表
  // Channel* currentActiveChannel_;

  std::atomic_bool
      callingPendingFunctors_; // 标识当前 loop 是否有需要执行的回调
  std::vector<Functor> pendingFunctors_; // 存储 loop 需要执行的所有回调
  std::mutex mutex_; // 互斥锁，用来保证 pendingFunctors_ 的线程安全
};