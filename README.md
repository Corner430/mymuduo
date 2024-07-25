# 重构 [muduo 网络库](https://github.com/chenshuo/muduo)

> 一言以蔽之，`muduo` 是一个基于 `Reactor` 模式的 C++ 网络库，可以是单线程单 Reactor，也可以是多线程多 Reactor。

使用 C++11 重构，摆脱 `boost` 依赖

[Docker 开发环境](https://github.com/Corner430/Docker/tree/main/mymuduo)

[开发日志](https://github.com/Corner430/mymuduo/blob/main/logs.md)

> 建议结合 commit 记录和开发日志阅读，**务必重视开发日志**

涉及技术栈：

- I/O 多路复用、TCP/IP 网络编程、Reactor 模型
- 智能指针、基于宏定义的日志系统、Buffer 缓冲区、四大类型转换
- 多线程编程、通过 `eventfd` 进行线程间通信、信号量
- 系统调用：`getenv`, `syscall(SYS_gettid)`，分支预测：`__builtin_expect`
- 连接半关闭、`move` 语义、`explicit`、`__thread` 等

> 详细技术涉及参见[开发日志](https://github.com/Corner430/mymuduo/blob/main/logs.md)和源码注释

## 1 先修知识

- muduo 网络库的使用 参见[chatserver-muduo](https://github.com/Corner430/chatserver/tree/main?tab=readme-ov-file#5-muduo-网络库)

- I/O 多路复用 参见[小林 coding](https://xiaolincoding.com/os/8_network_system/selete_poll_epoll.html#最基本的-socket-模型)和[blog I/O 多路复用](https://blog.corner430.eu.org/2024/07/21/I-O-多路复用/#more)

- [4 种 I/O 模式：阻塞、非阻塞、同步、异步](https://blog.corner430.eu.org/2024/07/21/4-种-I-O-模式：阻塞、非阻塞、同步、异步/#more)

- [Unix/Linux 上的五种 IO 模型](https://blog.corner430.eu.org/2024/07/21/Unix-Linux-上的五种-IO-模型/#more)

- Reactor 模型 参见[小林 coding](https://xiaolincoding.com/os/8_network_system/reactor.html#reactor)

- [优秀的网络服务器设计](https://blog.corner430.eu.org/2024/07/24/优秀的网络服务器设计思路/#more)

## 2 源码剖析

### 2.1 `muduo` 网络库的核心模块

- `TcpServer`：对于 Tcp 服务器的封装，负责调度管理整个 Reactor 模型的工作流程
- `EventLoop`：事件循环，等价于 Reactor 模型中的 Reactor
- `Acceptor`：运行在 `mainLoop` 中，负责监听新连接，并回调 `TcpServer::newConnection()` 方法将新连接分发到 `subReactor` 中
- `Poller`：`epoll` 的抽象类，含有 `ChannelMap`，保存了 `Channel` 和 `fd` 的映射关系，每个 `EventLoop` 对象都有一个 `Poller` 对象，每个 `Poller` 对象都有一个 `ChannelMap` 对象。负责监听 `fd` 的可读写事件

### 2.2 事件处理

muduo 作者认为，TCP 网络编程的本质是处理**三个半事件**，即：

1. 连接的建立
2. 连接的断开：包括主动断开和被动断开
3. 消息到达，文件描述符可读。
4. 消息发送完毕，这个算半个事件。

#### 2.2.1 连接的建立

当单纯使用 linux 的 API，编写一个简单的 Tcp 服务器时，建立一个新的连接通常需要四步：`socket()`、`bind()`、`listen()`、`accept()`。

1. `TcpServer` 对象构建时，`TcpServer` 的属性 `acceptor` 同时也被建立。
2. `Acceptor` 的构造函数中分别调用了 `socket` 函数和 `bind` 函数完成了**步骤 1 和步骤 2**。
   > 即，当 `TcpServer server(&loop, listenAddr)` 执行结束时，监听 `socket` 已经建立好，并已绑定到对应地址和端口了。
3. 当执行 `server.start()` 时，主要做了两个工作：
   1. 在监听 `socket` 上启动 `listen` 函数，也就是**步骤 3**；
   2. 将监听 `socket` 的可读事件注册到 `EventLoop` 中。

此时，程序已完成对 `socket` 的监听，但还不够，因为此时程序的主角 `EventLoop` 尚未启动。当调用 `loop.loop()` 时，程序开始循环监听该 `socket` 的可读事件。

---

当新连接请求建立时，可读事件触发，此时该事件对应的 `callback` 在 `EventLoop::loop()` 中被调用。该事件的 `callback` 实际上就是 `Acceptor::handleRead()` 方法。

在 `Acceptor::handleRead()` 方法中，做了三件事：

1. 调用了 `accept` 函数，完成了**步骤 4**，实现了连接的建立。得到一个已连接 `socket` 的 `fd`。
2. 将新连接的 fd 设置为非阻塞模式
3. 调用 `TcpServer::newConnection()` 方法
   1. 使用轮询算法，从线程池中选择一个事件循环（EventLoop）来管理新的 channel
   2. 根据连接成功的 sockfd，创建 TcpConnection 对象
   3. 设置 TcpConnection 的各种回调函数
   4. 在对应的 loop 中执行 `TcpConnection::connectEstablished()` 方法
      1. 将 channel 和 TcpConnection 绑定
      2. 启用 channel 的读事件
      3. 调用 connectionCallback\_ 回调函数

这里还有一个需要注意的点，创建的 `TcpConnnection` 对象是个 `shared_ptr`，该对象会被保存在 `TcpServer` 的 `connections` 中。这样才能保证引用计数大于 `0`，对象不被释放。

至此，一个新的连接已完全建立好，该连接的 `socket` **可读事件**也已注册到对应的 `EventLoop` 中了

#### 2.2.2 消息的读取

假如客户端发送消息，导致已连接 `socket` 的可读事件触发，该事件对应的 `callback` 同样也会在 `EventLoop::loop()` 中被调用。

该事件的 `callback` 实际上就是 `TcpConnection::handleRead` 方法。在 `TcpConnection::handleRead` 方法中，主要做了两件事：

1. 从 `socket` 中读取数据，并将其放入 `inputbuffer`
2. 调用 `messageCallback`，执行业务逻辑。

#### 2.2.3 消息的发送

用户通过调用 `TcpConnection::send()` 向客户端回复消息。由于 `muduo` 中使用了 `OutputBuffer`，因此消息的发送过程比较复杂。

首先需要注意的是线程安全问题, 上文说到对于消息的读写必须都在 `EventLoop` 的同一个线程 (通常称为 IO 线程) 中进行：
因此，`TcpConnection::send` 必须要保证线程安全性，它是这么做的：

```cpp
void TcpConnection::send(const std::string &buf) {
  if (state_ == kConnected)
    if (loop_->isInLoopThread())
      sendInLoop(buf.c_str(), buf.size());
    else
      loop_->runInLoop(
          std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
}
```

检测 `send` 的时候，是否在当前 IO 线程，如果是的话，直接进行写相关操作 `sendInLoop`。如果不在一个线程的话，需要将该任务抛给 IO 线程执行 `runInloop`, 以保证 `write` 动作是在 IO 线程中执行的。

在 `sendInloop` 中，做了下面几件事：

1. 假如 `OutputBuffer` 为空，则直接向 `socket` 写数据
2. 如果向 `socket` 写数据没有写完，则统计剩余的字节个数，并进行下一步。没有写完可能是因为此时 `socket` 的 `TCP` 缓冲区已满了。
3. 如果此时 `OutputBuffer` 中的旧数据的个数和未写完字节个数之和大于 `highWaterMark`，则将 `highWaterMarkCallback` 放入待执行队列中
4. 将对应 `socket` 的可写事件注册到 `EventLoop` 中

> **注意：直到发送消息的时候，`muduo` 才会把 `socket` 的可写事件注册到了 `EventLoop` 中。在此之前只注册了可读事件。**

连接 `socket` 的可写事件对应的 `callback` 是 `TcpConnection::handleWrite()`。当某个 `socket` 的可写事件触发时，`TcpConnection::handleWrite` 会做两个工作：

1. 尽可能将数据从 `OutputBuffer` 中向 `socket` 中 `write` 数据
1. 如果 `OutputBuffer` 没有剩余的，**则将该 `socket` 的可写事件移除**，并调用 `writeCompleteCallback`

**为什么要移除可写事件**

因为当 `OutputBuffer` 中没数据时，我们不需要向 `socket` 中写入数据。但是此时 `socket` 一直是处于可写状态的， 这将会导致 `TcpConnection::handleWrite()` 一直被触发。然而这个触发毫无意义，因为并没有什么可以写的。

所以 `muduo` 的处理方式是，当 `OutputBuffer` 还有数据时，`socket` 可写事件是注册状态。当 `OutputBuffer` 为空时，则将 `socket` 的可写事件移除。

此外，`highWaterMarkCallback` 和 `writeCompleteCallback` 一般配合使用，起到限流的作用。

#### 2.2.4 连接的断开

连接的断开分为被动断开和主动断开。主动断开和被动断开的处理方式基本一致。

**被动断开**即客户端断开了连接，`server` 端需要感知到这个断开的过程，然后进行的相关的处理。

其中感知远程断开这一步是在 Tcp 连接的可读事件处理函数 `handleRead` 中进行的：当对 `socket` 进行 `read` 操作时，返回值为 `0`，则说明此时连接已断开。

接下来会做四件事情：

1. 将该 TCP 连接对应的事件从 `EventLoop` 移除
2. 调用用户的 `ConnectionCallback`
3. 将对应的 `TcpConnection` 对象从 `Server` 移除。
4. `close` 对应的 `fd`。此步骤是在析构函数中自动触发的，当 `TcpConnection` 对象被移除后，引用计数为 0，对象析构时会调用 `close`。

### 2.3 `runInLoop` 的实现

在消息的发送过程，为保证对 `buffer` 和 `socket` 的写动作是在 IO 线程中进行，使用了一个 `runInLoop` 函数，将该写任务抛给了 IO 线程处理。此处看下 `runInLoop` 的实现。

```cpp
void EventLoop::runInLoop(Functor cb) {
  isInLoopThread() ? cb() : queueInLoop(cb);
}
```

这里可以看到，做了一层判断。如果调用时是此 `EventLoop` 的运行线程，则直接执行此函数。否则调用 `queueInLoop` 函数。`queueInLoop` 的实现如下：

```cpp
// 把 cb 放入 pendingFunctors_，唤醒 loop 所在的线程，执行 cb
void EventLoop::queueInLoop(Functor cb) {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    pendingFunctors_.emplace_back(cb);
  }

  /* 1. 如果调用 queueInLoop() 和 EventLoop 不在同一个线程，或者
   *    callingPendingFunctors_ 为 true 时（此时正在执行
   *    doPendingFunctors()，即正在执行回调），则唤醒 loop 所在的线程
   * 2. 如果调用 queueInLoop() 和 EventLoop 在同一个线程，但是
   *    callingPendingFunctors_ 为 false 时，则说明：此时尚未执行到
   *    doPendingFunctors()。
   *    不必唤醒，这个优雅的设计可以减少对 eventfd 的 I/O 读写 */
  if (!isInLoopThread() || callingPendingFunctors_)
    wakeup();
}
```

这里有两个动作：

1. 加锁，然后将该函数放到该 `EventLoop` 的 `pendingFunctors_` 队列中。
2. 判断是否要唤醒 `EventLoop`，如果是则调用 `wakeup()` 唤醒该 `EventLoop`。
   这里有几个问题：

- 为什么要唤醒 `EventLoop`？
- `wakeup` 是怎么实现的?
- `pendingFunctors_` 是如何被消费的?

### 2.4 为什么要唤醒 `EventLoop`

我们首先调用了 `pendingFunctors_.push_back(cb)`, 将该函数放在 `pendingFunctors_` 中。`EventLoop` 的每一轮循环在最后会调用 `doPendingFunctors` 依次执行这些函数。

而 `EventLoop` 的唤醒是通过 `epoll_wait` 实现的，如果此时该 `EventLoop` 中迟迟没有事件触发，那么 `epoll_wait` 一直就会阻塞。 这样会导致 `pendingFunctors_` 中的任务迟迟不能被执行。

所以必须要唤醒 `EventLoop` ，从而让 `pendingFunctors_` 中的任务尽快被执行。

### 2.5 `wakeup` 是怎么实现的

`muduo` 这里采用了对 `eventfd` 的读写来实现对 `EventLoop` 的唤醒。

在 `EventLoop` 建立之后，就创建一个 `eventfd`，并将其可读事件注册到 `EventLoop` 中。

`wakeup()` 的过程本质上是对这个 `eventfd` 进行写操作，以触发该 `eventfd` 的可读事件。这样就起到了唤醒 `EventLoop` 的作用。

```cpp
/* 向 wakeupfd_ 写一个数据，wakeupChannel 就发生读事件
 * 对应的 loop 线程就会被唤醒 */
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
    LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
}
```

很多库为了兼容 `macOS`，往往使用 `pipe` 来实现这个功能。`muduo` 采用了 `eventfd`，性能更好些，但代价是不能支持 `macOS` 了。

### 2.6 `doPendingFunctors` 的实现

本部分为 `doPendingFunctors` 的实现，`muduo` 是如何处理这些待处理的函数的，以及中间用了哪些优化操作。

```cpp
void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  { // 通过 swap 减小临界区长度
    std::unique_lock<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  for (const Functor &functor : functors)
    functor(); // 执行当前 loop 需要执行的回调操作

  callingPendingFunctors_ = false;
}
```

从代码可以看到，函数非常简单。大概只有十行代码，但是这十行代码中却有两个非常巧妙的地方。

1. `callingPendingFunctors_` 的作用

从代码可以看出，如果 `callingPendingFunctors_` 为 `false`，则说明此时尚未开始执行 `doPendingFunctors` 函数。

这个有什么作用呢？需要结合下 `queueInLoop` 中对是否执行 `wakeup()` 的判断

```cpp
if (!isInLoopThread() || callingPendingFunctors_)
  wakeup();
```

这里还需要结合下 `EventLoop` 循环的实现，其中 `doPendingFunctors()` 是**每轮循环的最后一步处理**。

如果调用 `queueInLoop` 和 `EventLoop` 在同一个线程，且 `callingPendingFunctors_` 为 `false` 时，则说明：**此时尚未执行到 `doPendingFunctors()`**。

那么此时即使不用 `wakeup`，也可以在之后照旧执行 `doPendingFunctors()` 了。

这么做的好处非常明显，可以减少对 `eventfd` 的 IO 读写。

2. 锁范围的减少

在此函数中，有一段特别的代码：

```cpp
std::vector<Functor> functors;
{
  MutexLockGuard lock(mutex_);
  functors.swap(pendingFunctors_);
}
```

这个作用是 `pendingFunctors_` 和 `functors` 的内容进行交换，实际上就是此时 `functors` 持有了 `pendingFunctors_` 的内容，而 `pendingFunctors_` 被清空了。

这个好处是什么呢？

如果不这么做，直接遍历 `pendingFunctors_`, 然后处理对应的函数。这样的话，锁会一直等到所有函数处理完才会被释放。在此期间，`queueInLoop` 将不可用。

而以上的写法，可以极大减小锁范围，整个锁的持有时间就是 `swap` 那一下的时间。待处理函数执行的时候，其他线程还是可以继续调用 `queueInLoop`。
