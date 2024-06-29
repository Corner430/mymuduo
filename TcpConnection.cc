#include "TcpConnection.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include "Socket.h"

#include <errno.h>
#include <functional>
#include <netinet/tcp.h>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>

static EventLoop *CheckLoopNotNull(EventLoop *loop) {
  if (loop == nullptr) {
    LOG_FATAL("%s:%s:%d TcpConnection Loop is null! \n", __FILE__, __FUNCTION__,
              __LINE__);
  }
  return loop;
}

TcpConnection::TcpConnection(EventLoop *loop, const std::string &nameArg,
                             int sockfd, const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(CheckLoopNotNull(loop)), name_(nameArg), state_(kConnecting),
      reading_(true), socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)), localAddr_(localAddr),
      peerAddr_(peerAddr), highWaterMark_(64 * 1024 * 1024) // 64M
{
  // 下面给channel设置相应的回调函数，poller给channel通知感兴趣的事件发生了，channel会回调相应的操作函数
  channel_->setReadCallback(
      std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
  channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
  channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
  channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

  LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
  socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection() {
  LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d \n", name_.c_str(),
           channel_->fd(), (int)state_);
}

// 发送数据的函数
void TcpConnection::send(const std::string &buf) {
  // 只有当连接状态为已连接时才进行发送操作
  if (state_ == kConnected) {
    // 如果当前线程是事件循环所属的线程
    if (loop_->isInLoopThread()) {
      // 直接调用sendInLoop函数发送数据
      sendInLoop(buf.c_str(), buf.size());
    } else {
      // 否则，将发送操作投递到事件循环中执行
      loop_->runInLoop(
          std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
      // 使用std::bind将sendInLoop函数绑定到当前对象，并传递数据指针和大小参数
    }
  }
}

/**
 * 发送数据  应用写的快， 而内核发送数据慢， 需要把待发送数据写入缓冲区，
 * 而且设置了水位回调
 */
void TcpConnection::sendInLoop(const void *data, size_t len) {
  ssize_t nwrote = 0;
  size_t remaining = len;
  bool faultError = false;

  // 之前调用过该connection的shutdown，不能再进行发送了
  if (state_ == kDisconnected) {
    LOG_ERROR("disconnected, give up writing!");
    return;
  }

  // 表示channel_第一次开始写数据，而且缓冲区没有待发送数据
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
    nwrote = ::write(channel_->fd(), data, len);
    if (nwrote >= 0) {
      remaining = len - nwrote;
      if (remaining == 0 && writeCompleteCallback_) {
        // 既然在这里数据全部发送完成，就不用再给channel设置epollout事件了
        loop_->queueInLoop(
            std::bind(writeCompleteCallback_, shared_from_this()));
      }
    } else // nwrote < 0
    {
      nwrote = 0;
      if (errno != EWOULDBLOCK) {
        LOG_ERROR("TcpConnection::sendInLoop");
        if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE  RESET
        {
          faultError = true;
        }
      }
    }
  }

  // 说明当前这一次write，并没有把数据全部发送出去，剩余的数据需要保存到缓冲区当中，然后给channel
  // 注册epollout事件，poller发现tcp的发送缓冲区有空间，会通知相应的sock-channel，调用writeCallback_回调方法
  // 也就是调用TcpConnection::handleWrite方法，把发送缓冲区中的数据全部发送完成
  if (!faultError && remaining > 0) {
    // 目前发送缓冲区剩余的待发送数据的长度
    size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ &&
        highWaterMarkCallback_) {
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(),
                                   oldLen + remaining));
    }
    outputBuffer_.append((char *)data + nwrote, remaining);
    if (!channel_->isWriting()) {
      channel_
          ->enableWriting(); // 这里一定要注册channel的写事件，否则poller不会给channel通知epollout
    }
  }
}

// 关闭连接
void TcpConnection::shutdown() {
  if (state_ == kConnected) {
    setState(kDisconnecting);
    loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
  }
}

void TcpConnection::shutdownInLoop() {
  if (!channel_->isWriting()) // 说明outputBuffer中的数据已经全部发送完成
  {
    socket_->shutdownWrite(); // 关闭写端
  }
}

// 连接建立
void TcpConnection::connectEstablished() {
  setState(kConnected);
  channel_->tie(shared_from_this());
  channel_->enableReading(); // 向 poller 注册 channel 的 epollin 事件

  // 新连接建立，执行回调
  connectionCallback_(shared_from_this());
}

// 连接销毁
void TcpConnection::connectDestroyed() {
  if (state_ == kConnected) {
    setState(kDisconnected);
    channel_->disableAll(); // 把channel的所有感兴趣的事件，从poller中del掉
    connectionCallback_(shared_from_this());
  }
  channel_->remove(); // 把channel从poller中删除掉
}

// 处理读事件的回调函数
void TcpConnection::handleRead(Timestamp receiveTime) {
  int savedErrno = 0; // 用于保存读取过程中可能发生的错误码
  ssize_t n = inputBuffer_.readFd(
      channel_->fd(),
      &savedErrno); // 从channel对应的文件描述符中读取数据到inputBuffer_

  if (n > 0) // 如果读取的数据长度大于0
  {
    // 已建立连接的用户，有可读事件发生了，调用用户传入的回调操作onMessage
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    // messageCallback_ 是用户定义的回调函数，用于处理接收到的数据
    // shared_from_this() 返回一个指向当前TcpConnection对象的shared_ptr
    // &inputBuffer_ 是指向输入缓冲区的指针，receiveTime 是数据接收的时间戳
  } else if (n == 0) // 如果读取的数据长度为0，表示连接已关闭
  {
    handleClose(); // 调用关闭连接的处理函数
  } else           // 如果读取的数据长度小于0，表示发生了错误
  {
    errno = savedErrno;                     // 恢复保存的错误码
    LOG_ERROR("TcpConnection::handleRead"); // 记录错误日志
    handleError();                          // 调用错误处理函数
  }
}

void TcpConnection::handleWrite() {
  if (channel_->isWriting()) {
    int savedErrno = 0;
    ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
    if (n > 0) {
      outputBuffer_.retrieve(n);
      if (outputBuffer_.readableBytes() == 0) {
        channel_->disableWriting();
        if (writeCompleteCallback_) {
          // 唤醒loop_对应的thread线程，执行回调
          loop_->queueInLoop(
              std::bind(writeCompleteCallback_, shared_from_this()));
        }
        if (state_ == kDisconnecting) {
          shutdownInLoop();
        }
      }
    } else {
      LOG_ERROR("TcpConnection::handleWrite");
    }
  } else {
    LOG_ERROR("TcpConnection fd=%d is down, no more writing \n",
              channel_->fd());
  }
}

// poller => channel::closeCallback => TcpConnection::handleClose
void TcpConnection::handleClose() {
  LOG_INFO("TcpConnection::handleClose fd=%d state=%d \n", channel_->fd(),
           (int)state_);
  setState(kDisconnected);
  channel_->disableAll();

  TcpConnectionPtr connPtr(shared_from_this());
  connectionCallback_(connPtr); // 执行连接关闭的回调
  closeCallback_(
      connPtr); // 关闭连接的回调  执行的是TcpServer::removeConnection回调方法
}

void TcpConnection::handleError() {
  int optval;
  socklen_t optlen = sizeof optval;
  int err = 0;
  if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) <
      0) {
    err = errno;
  } else {
    err = optval;
  }
  LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n",
            name_.c_str(), err);
}