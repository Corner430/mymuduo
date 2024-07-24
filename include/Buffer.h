#pragma once

#include <algorithm>
#include <string>
#include <vector>

/*
 * A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
 *
 * @code
 * +-------------------+------------------+------------------+
 * | prependable bytes |  readable bytes  |  writable bytes  |
 * |                   |     (CONTENT)    |                  |
 * +-------------------+------------------+------------------+
 * |                   |                  |                  |
 * 0      <=      readerIndex   <=   writerIndex    <=     size
 * @endcode */
class Buffer {
public:
  static const size_t kCheapPrepend = 8;
  static const size_t kInitialSize = 1024;

  explicit Buffer(size_t initialSize = kInitialSize)
      : buffer_(kCheapPrepend + initialSize), readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend) {}

  size_t readableBytes() const { return writerIndex_ - readerIndex_; }

  size_t writableBytes() const { return buffer_.size() - writerIndex_; }

  size_t prependableBytes() const { return readerIndex_; }

  // 返回缓冲区中可读数据的起始地址
  const char *peek() const { return begin() + readerIndex_; }

  /* retrieve returns void, to prevent
   * string str(retrieve(readableBytes()), readableBytes());
   * the evaluation of two functions are unspecified */
  void retrieve(size_t len) {
    if (len < readableBytes())
      readerIndex_ += len; // 仅读取 len 长度的数据，未读完
    else
      retrieveAll();
  }

  void retrieveAll() { readerIndex_ = writerIndex_ = kCheapPrepend; }

  // 把 onMessage 函数上报的 Buffer 数据，转成 string 类型返回
  std::string retrieveAllAsString() {
    return retrieveAsString(readableBytes());
  }

  std::string retrieveAsString(size_t len) {
    std::string result(peek(), len);
    retrieve(len); // 已读，对缓冲区进行复位操作
    return result;
  }

  void ensureWriteableBytes(size_t len) {
    if (writableBytes() < len)
      makeSpace(len);
  }

  // 把 [data, data + len] 内存上的数据，添加到 writable 缓冲区当中
  void append(const char *data, size_t len) {
    ensureWriteableBytes(len);
    std::copy(data, data + len, beginWrite());
    writerIndex_ += len;
  }

  char *beginWrite() { return begin() + writerIndex_; }

  const char *beginWrite() const { return begin() + writerIndex_; }

  ssize_t readFd(int fd, int *saveErrno);  // 从 fd 上读取数据
  ssize_t writeFd(int fd, int *saveErrno); // 通过 fd 发送数据

private:
  char *begin() { return &*buffer_.begin(); } // 裸指针
  const char *begin() const { return &*buffer_.begin(); }
  void makeSpace(size_t len) { // 整理空间或者扩容
    if (writableBytes() + prependableBytes() < len + kCheapPrepend)
      // 加上挂起的数据空间，仍然不够，需要扩容
      buffer_.resize(writerIndex_ + len);
    else { // move readable data to the front, make space inside buffer
      size_t readalbe = readableBytes();
      std::copy(begin() + readerIndex_, begin() + writerIndex_,
                begin() + kCheapPrepend);
      readerIndex_ = kCheapPrepend;
      writerIndex_ = readerIndex_ + readalbe;
    }
  }

  std::vector<char> buffer_;
  size_t readerIndex_;
  size_t writerIndex_;
};