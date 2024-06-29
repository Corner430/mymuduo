#include "EPollPoller.h"
#include "Channel.h"
#include "Logger.h"

#include <errno.h>
#include <strings.h>
#include <unistd.h>

// channel未添加到poller中
const int kNew = -1; // channel的成员index_ = -1
// channel已添加到poller中
const int kAdded = 1;
// channel从poller中删除
const int kDeleted = 2;

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop), // 调用基类 Poller 的构造函数，传入 EventLoop 指针
      epollfd_(::epoll_create1(
          EPOLL_CLOEXEC)), // 创建 epoll 实例，并设置 close-on-exec 标志
      events_(kInitEventListSize) // 初始化 events_，用于存放 epoll_event
                                  // 结构体的 vector
{
  if (epollfd_ < 0) {
    // 如果 epoll_create1 函数调用失败，记录致命错误
    LOG_FATAL("epoll_create error:%d \n", errno);
  }
}

EPollPoller::~EPollPoller() { ::close(epollfd_); }

Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels) {
  LOG_INFO("func=%s => fd total count:%lu \n", __FUNCTION__, channels_.size());
  // 记录信息日志，输出当前管理的文件描述符总数

  // 调用 epoll_wait 函数监听事件，将事件存放在 events_ 数组中
  int numEvents = ::epoll_wait(epollfd_, &*events_.begin(),
                               static_cast<int>(events_.size()), timeoutMs);
  int saveErrno = errno;           // 保存当前的 errno 值
  Timestamp now(Timestamp::now()); // 获取当前时间戳

  if (numEvents > 0) {
    LOG_INFO("%d events happened \n", numEvents);
    // 记录信息日志，输出发生的事件数量
    fillActiveChannels(numEvents, activeChannels); // 填充活跃通道列表
    if (numEvents == events_.size()) {
      events_.resize(events_.size() *
                     2); // 如果 events_ 数组不够大，扩展数组大小
    }
  } else if (numEvents == 0) {
    LOG_DEBUG("%s timeout! \n", __FUNCTION__);
    // 如果没有事件发生，记录调试日志，表示超时
  } else {
    if (saveErrno != EINTR) {
      errno = saveErrno;
      LOG_ERROR("EPollPoller::poll() err!");
      // 如果发生错误并且不是被信号中断，记录错误日志
    }
  }
  return now; // 返回当前时间戳
}

/*
 * 1. 更改 channel 的状态
 * 2. 调用 EPollPoller::update 方法，向 epoll 实例中添加或修改通道
 */
void EPollPoller::updateChannel(Channel *channel) {
  const int index = channel->index(); // 获取通道当前的状态索引
  LOG_INFO("func=%s => fd=%d events=%d index=%d \n", __FUNCTION__,
           channel->fd(), channel->events(), index);

  if (index == kNew || index == kDeleted) {
    if (index == kNew) {
      // 如果通道状态为 kNew，表示通道是新添加的，未在 poller 中注册
      int fd = channel->fd();
      channels_[fd] = channel; // 将通道添加到 channels_ 中，以 fd 作为键
    }

    channel->set_index(kAdded); // 设置通道状态为 kAdded
    update(EPOLL_CTL_ADD,
           channel); // 调用 update 函数，向 epoll 实例中添加或修改通道
  } else {
    // 如果通道状态不是 kNew 或 kDeleted，说明通道已经在 epoll 实例中注册过了
    if (channel->isNoneEvent()) {
      // 如果通道的事件为 None，表示不关注任何事件，需要从 epoll
      // 实例中删除该通道
      update(EPOLL_CTL_DEL,
             channel); // 调用 update 函数，从 epoll 实例中删除通道
      channel->set_index(kDeleted); // 设置通道状态为 kDeleted
    } else {
      // 否则，修改通道在 epoll 实例中的事件关注状态
      update(EPOLL_CTL_MOD,
             channel); // 调用 update 函数，修改 epoll 实例中的通道事件关注状态
    }
  }
}

// 从poller中删除channel
void EPollPoller::removeChannel(Channel *channel) {
  int fd = channel->fd();
  channels_.erase(fd);

  LOG_INFO("func=%s => fd=%d\n", __FUNCTION__, fd);

  int index = channel->index();
  if (index == kAdded) {
    update(EPOLL_CTL_DEL, channel);
  }
  channel->set_index(kNew);
}

// 填写活跃的连接
void EPollPoller::fillActiveChannels(int numEvents,
                                     ChannelList *activeChannels) const {
  // 遍历每一个发生的事件
  for (int i = 0; i < numEvents; ++i) {
    Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
    // 获取当前事件对应的 Channel 对象，并设置其 revents 字段为发生的事件类型
    channel->set_revents(events_[i].events);
    // 将该 Channel 添加到活跃通道列表中
    activeChannels->push_back(channel);
  }
}

// 更新channel通道 epoll_ctl add/mod/del
void EPollPoller::update(int operation, Channel *channel) {
  epoll_event event;
  bzero(&event, sizeof event);

  int fd = channel->fd();

  event.events = channel->events();
  event.data.fd = fd;
  event.data.ptr = channel;

  if (::epoll_ctl(epollfd_, operation, fd, &event) < 0) {
    if (operation == EPOLL_CTL_DEL) {
      LOG_ERROR("epoll_ctl del error:%d\n", errno);
    } else {
      LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
    }
  }
}