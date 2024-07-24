#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

/**
 * 1. 从 fd 上读取数据  Poller 工作在 LT 模式
 * 2. Buffer 缓冲区是有大小的！但是从 fd 上读数据时，却不知道 tcp 数据的最终大小
 */
ssize_t Buffer::readFd(int fd, int *saveErrno) {
  // saved an ioctl()/FIONREAD call to tell how much to read
  char extrabuf[65536] = {0}; // 64K 的缓冲区
  struct iovec vec[2];
  const size_t writable = writableBytes();
  vec[0].iov_base = begin() + writerIndex_;
  vec[0].iov_len = writable;

  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof extrabuf;

  // when there is enough space in this buffer, don't read into extrabuf.
  // when extrabuf is used, we read 128k-1 bytes at most.
  const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
  const ssize_t n = ::readv(fd, vec, iovcnt);
  if (n < 0)
    *saveErrno = errno;
  else if (n <= writable) // Buffer 的可写缓冲区已经够存储读出来的数据
    writerIndex_ += n;
  else { // extrabuf 里面也写入了数据
    writerIndex_ = buffer_.size();
    append(extrabuf,
           n - writable); // 从 writerIndex_ 开始，写 n - writable 大小的数据
  }

  return n;
}

ssize_t Buffer::writeFd(int fd, int *saveErrno) {
  ssize_t n = ::write(fd, peek(), readableBytes());
  if (n < 0)
    *saveErrno = errno;
  return n;
}