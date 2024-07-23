#include "EPollPoller.h"
#include "Channel.h"
#include "Logger.h"

#include <errno.h>
#include <strings.h>
#include <unistd.h>

// 对应于 channel 的 index_ 成员
const int kNew = -1;    // channel 未添加到 EPollPoller 中，初始值
const int kAdded = 1;   // channel 已添加到 EPollPoller 中
const int kDeleted = 2; // channel 从 EPollPoller 中删除

/*
 * 1. epoll_create1(EPOLL_CLOEXEC) 表示 epoll 实例在创建时设置 close-on-exec，
 *    可以确保在调用 exec 函数时关闭 epoll 实例，避免子进程继承 epoll 实例
 * 2. exec 是一个系统调用，用于执行一个新的程序替换当前进程的映像，
 *    常和 fork 搭配使用
 */
EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop), epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize) {
  if (epollfd_ < 0) // 如果 epoll_create1 函数调用失败，记录致命错误
    LOG_FATAL("epoll_create error:%d \n", errno);
}

EPollPoller::~EPollPoller() { ::close(epollfd_); }

/*
 * 1. 调用 epoll_wait()，等待事件发生
 * 2. 将 epoll_wait() 返回的事件保存到 events_ 中
 * 3. 遍历 events_，调用封装的 fillActiveChannels() 方法，
 *    将发生事件的 channel 添加到 activeChannels 中，并设定 channel 的 revents
 */
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels) {
  // 当前管理的文件描述符总数
  LOG_INFO("func=%s => fd total count:%lu \n", __FUNCTION__, channels_.size());

  // 调用 epoll_wait 函数监听事件，将事件存放在 events_ 数组中
  int numEvents = ::epoll_wait(epollfd_, &*events_.begin(),
                               static_cast<int>(events_.size()), timeoutMs);
  int saveErrno = errno; // errno 是全局变量，所以要保存当前的错误码
  Timestamp now(Timestamp::now());

  if (numEvents > 0) {
    LOG_INFO("%d events happened \n", numEvents);  // 发生的事件数量
    fillActiveChannels(numEvents, activeChannels); // 填充活跃通道列表
    if (numEvents == events_.size()) // 如果 events_ 数组不够大，扩展数组大小
      events_.resize(events_.size() * 2);
  } else if (numEvents == 0)
    LOG_DEBUG("%s timeout! \n", __FUNCTION__); // 记录调试日志，表示超时
  else                        // error happens, log uncommon ones
    if (saveErrno != EINTR) { // 如果发生错误并且不是被信号中断，记录错误日志
      errno = saveErrno; // 恢复错误码
      LOG_ERROR("EPollPoller::poll() err!");
    }
  return now; // 返回当前时间戳
}

/*
 * 1. 更改 channel 的状态
 * 2. 将 channel 添加到 channels_ 中，也就是当前的 EPollPoller 对象中
 * 3. 调用封装的 update() 方法，添加、修改、删除 channel 所关注的事件
 */
void EPollPoller::updateChannel(Channel *channel) {
  const int index = channel->index(); // 获取 channel 当前的状态索引
  LOG_INFO("func=%s => fd=%d events=%d index=%d \n", __FUNCTION__,
           channel->fd(), channel->events(), index);

  if (index == kNew || index == kDeleted) {
    if (index == kNew) { // a new one, add with EPOLL_CTL_ADD
      int fd = channel->fd();
      channels_[fd] = channel; // 将 channel 添加到 channels_ 中，也就是添加到
                               // EPollPoller 中
    }

    channel->set_index(kAdded); // 设置 channel 状态为 kAdded
    update(EPOLL_CTL_ADD, channel);
  } else // update existing one with EPOLL_CTL_MOD/DEL
    if (channel->isNoneEvent()) {
      update(EPOLL_CTL_DEL, channel);
      channel->set_index(kDeleted);
    } else
      update(EPOLL_CTL_MOD, channel);
}

// 从 EPollPoller 中删除 channel
void EPollPoller::removeChannel(Channel *channel) {
  int fd = channel->fd();
  channels_.erase(
      fd); // 从 channels_ 中删除 channel，也就是从 EPollPoller 中删除

  LOG_INFO("func=%s => fd=%d\n", __FUNCTION__, fd);

  int index = channel->index();
  if (index == kAdded)
    update(EPOLL_CTL_DEL, channel);
  channel->set_index(kNew); // 设置 channel 状态为 kNew
}

void EPollPoller::fillActiveChannels(int numEvents,
                                     ChannelList *activeChannels) const {
  for (int i = 0; i < numEvents; ++i) {
    // 获取当前事件对应的 Channel 对象，并设置其 revents 字段为发生的事件类型
    Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
    channel->set_revents(events_[i].events);

    // 将该 Channel 添加到活跃通道列表中
    // 如此一来，EventLoop 就可以通过 activeChannels 获取到发生事件的 Channel
    activeChannels->push_back(channel);
  }
}

// epoll_ctl() 操作，同时让 event.data.ptr 指向对应的 channel
void EPollPoller::update(int operation, Channel *channel) {
  epoll_event event;
  bzero(&event, sizeof event);

  int fd = channel->fd();

  event.events = channel->events();
  event.data.fd = fd; // 无所谓，源码中并没有使用
  event.data.ptr = channel;

  if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    if (operation == EPOLL_CTL_DEL)
      LOG_ERROR("epoll_ctl del error:%d\n", errno);
    else
      LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
}