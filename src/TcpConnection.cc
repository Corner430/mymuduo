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
  if (loop == nullptr)
    LOG_FATAL("%s:%s:%d TcpConnection Loop is null! \n", __FILE__, __FUNCTION__,
              __LINE__);
  return loop;
}

TcpConnection::TcpConnection(EventLoop *loop, const std::string &nameArg,
                             int sockfd, const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(CheckLoopNotNull(loop)), name_(nameArg), state_(kConnecting),
      reading_(true), socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)), localAddr_(localAddr),
      peerAddr_(peerAddr), highWaterMark_(64 * 1024 * 1024) { // 64M
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

void TcpConnection::send(const std::string &buf) {
  if (state_ == kConnected)
    if (loop_->isInLoopThread())
      sendInLoop(buf.c_str(), buf.size());
    else
      loop_->runInLoop(
          std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
}

void TcpConnection::sendInLoop(const void *data, size_t len) {
  ssize_t nwrote = 0;
  size_t remaining = len;
  bool faultError = false;

  if (state_ == kDisconnected) { // 已 shutdown
    LOG_ERROR("disconnected, give up writing!");
    return;
  }

  // channel_ 第一次写数据，且缓冲区没有待发送数据
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
    nwrote = ::write(channel_->fd(), data, len);
    if (nwrote >= 0) {
      remaining = len - nwrote;
      if (remaining == 0 && writeCompleteCallback_)
        // 既然在这里数据全部发送完成，就不用再给 channel 设置 epollout 事件了
        loop_->queueInLoop(
            std::bind(writeCompleteCallback_, shared_from_this()));
    } else { // nwrote < 0
      nwrote = 0;
      if (errno != EWOULDBLOCK) {
        LOG_ERROR("TcpConnection::sendInLoop");
        if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE RESET
          faultError = true;
      }
    }
  }

  /* 1. 说明本次 write 没有把数据全部发出，剩余的数据需要保存到缓冲区当中
   * 2. 给 channel 注册 epollout 事件，poller 发现 tcp 的发送缓冲区有空间，
   *    会由于水平触发通知相应的 sock-channel，调用 writeCallback_ 回调方法
   *    也就是调用 TcpConnection::handleWrite 方法，
   *    把发送缓冲区中的数据全部发送完成 */
  if (!faultError && remaining > 0) {
    // 目前发送缓冲区剩余的待发送数据的长度
    size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ &&
        highWaterMarkCallback_)
      /* 应用写的快，而内核发送数据慢，需要把待发送数据写入缓冲区，
      当缓冲区的数据超过一定的水位时，调用相应回调 */
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(),
                                   oldLen + remaining));
    outputBuffer_.append((char *)data + nwrote, remaining);
    if (!channel_->isWriting())
      channel_->enableWriting();
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
  if (!channel_->isWriting()) // 说明 outputBuffer 中的数据已发送完毕
    socket_->shutdownWrite(); // 关闭写端
}

// 连接建立，会在 TcpServer::newConnection() 中调用
void TcpConnection::connectEstablished() {
  setState(kConnected);
  channel_->tie(shared_from_this());
  channel_->enableReading();

  connectionCallback_(shared_from_this());
}

// 连接销毁
void TcpConnection::connectDestroyed() {
  if (state_ == kConnected) {
    setState(kDisconnected);
    channel_->disableAll();
    connectionCallback_(shared_from_this());
  }
  channel_->remove();
}

// 处理读事件的回调函数
void TcpConnection::handleRead(Timestamp receiveTime) {
  int savedErrno = 0;
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);

  if (n > 0) // 已建立连接的用户发生可读事件，调用用户传入的 onMessage 回调
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
  else if (n == 0) // 如果读取的数据长度为 0，表示客户端连接已关闭
    handleClose();
  else { // 如果读取的数据长度小于 0，表示发生了错误
    errno = savedErrno;
    LOG_ERROR("TcpConnection::handleRead");
    handleError();
  }
}

void TcpConnection::handleWrite() {
  if (channel_->isWriting()) {
    int savedErrno = 0;
    ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
    if (n > 0) {
      outputBuffer_.retrieve(n); // 从 outputBuffer_ 中移除已经发送的数据
      if (outputBuffer_.readableBytes() == 0) { // 发送完成
        channel_->disableWriting();             // 不再关注 POLLOUT 事件
        if (writeCompleteCallback_)
          loop_->queueInLoop(
              std::bind(writeCompleteCallback_, shared_from_this()));
        if (state_ == kDisconnecting)
          shutdownInLoop();
      }
    } else
      LOG_ERROR("TcpConnection::handleWrite");
  } else
    LOG_ERROR("TcpConnection fd=%d is down, no more writing \n",
              channel_->fd());
}

// poller => channel::closeCallback => TcpConnection::handleClose
void TcpConnection::handleClose() {
  LOG_INFO("TcpConnection::handleClose fd=%d state=%d \n", channel_->fd(),
           (int)state_);
  setState(kDisconnected);
  channel_->disableAll();

  TcpConnectionPtr connPtr(shared_from_this());
  connectionCallback_(connPtr); // 执行连接 建立/关闭 的回调
  // 关闭连接的回调，执行的是 TcpServer::removeConnection 回调方法
  closeCallback_(connPtr); // must be the last line
}

void TcpConnection::handleError() {
  int optval;
  socklen_t optlen = sizeof optval;
  int err = 0;
  if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    err = errno;
  else
    err = optval;
  LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n",
            name_.c_str(), err);
}