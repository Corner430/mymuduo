重构 `muduo` 网络库，学习网络编程的基础知识，包括 IO 多路复用、Reactor 模型、`muduo` 网络库的核心代码模块等。

*muduo: one loop per thread + threadpool*

`muduo` 配置多个线程，每个线程跑一个 `eventloop`，一个线程只负责接收连接并分发给其他线程，之后该连接的所有 IO 操作都由该线程处理，`threadpool` 可以用来处理耗时的任务。

## 1 IO 复用

> `select`，`poll`，`epoll` 都是 IO 多路复用的机制。**I/O 多路复用就通过一种机制，可以监视多个描述符**，一旦某个描述符就绪（一般是读就绪或者写就绪），能够通知程序进行相应的读写操作。
>
> **但 `select`，`poll`，`epoll` 本质上都是同步 I/O**，**因为他们都需要在读写事件就绪后自己负责进行读写，也就是说这个读写过程是阻塞的**，而异步 I/O 则无需自己负责进行读写，异步 I/O 的实现会负责把数据从内核拷贝到用户空间。

### 1.1 IO 多路复用之 `select`

使用 `select` 可以实现同时处理多个网络连接的 IO 请求。其基本原理是程序调用 `select` 后，**程序进入阻塞状态**，此时 `kernel`（内核）会轮询检查所有 `select` 负责的文件描述符 `fd`。当找到其中某个文件描述符的数据已准备好时，`select` 返回该文件描述符并通知系统调用，**将数据从内核复制到进程的缓存区**。

**缺点：**

- 进程可以打开的 `fd` 有限制（32 位机 1024 个，64 位 2048 个），因为 `fd` 存储在一个固定大小的数组中。
- 对 `socket` 进行扫描是线性扫描，即采用轮询方法，效率较低。
- 用户空间和内核空间之间复制数据非常消耗资源。

### 1.2 IO 多路复用之 `poll`

`poll` 的基本原理与 `select` 非常类似，但 `poll` 使用链表来存储文件描述符（`fd`），并且不会修改文件描述符。相比于 `select`，`poll` 提供了更多的事件类型，对文件描述符的重复利用率也更高。与 `select` 不同，`poll` 没有最大文件描述符数量的限制。

虽然 `poll` 和 `select` 的机制类似，都是通过轮询来管理多个描述符，根据描述符的状态进行处理，但它们都存在一个缺点：**包含大量文件描述符的数组会整体复制于用户态和内核的地址空间之间，无论这些文件描述符是否就绪**，这样的开销会随着文件描述符数量的增加而线性增大。

### 1.3 IO 多路复用之 `epoll`

#### 1.3.1 `epoll` 提供的三个主要函数

1. `int epoll_create1(int flags)`：

   - 用于创建一个 `epoll` 实例，并返回一个文件描述符。
   - `flags` 参数是一个位掩码，可以是 `0` 或者`EPOLL_CLOEXEC`。如果使用 `EPOLL_CLOEXEC` 标志，则 `epoll` 实例会在 `exec` 调用时自动关闭。
   - 如果成功，返回一个非负整数作为 `epoll` 实例的文件描述符；失败时返回 `-1`，并设置 `errno`。

2. `int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)`：

   - 用于向 `epoll` 实例中添加、修改或删除事件。
   - `epfd` 是 `epoll` 实例的文件描述符；`op` 是操作类型，可以是 `EPOLL_CTL_ADD`（添加事件）、`EPOLL_CTL_MOD`（修改事件）或 `EPOLL_CTL_DEL`（删除事件）；`fd` 是要操作的文件描述符；`event` 是指向 `epoll_event` 结构体的指针，包含要关注的事件类型和相关数据。
   - 如果成功，返回 `0`；失败时返回 `-1`，并设置 `errno`。

3. `int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)`：
   - 用于等待事件的发生。
   - `epfd` 是`epoll`实例的文件描述符；`events` 是一个数组，用于存储发生的事件；`maxevents` 是 `events` 数组的大小，表示最多可以存储多少个事件；`timeout` 是超时时间，单位为毫秒，传入 `-1` 表示阻塞直到有事件发生。
   - 函数会阻塞，直到有事件发生或超时。如果有事件发生，则将事件存储在 `events` 数组中，并返回发生事件的数量；如果超时，则返回 `0`；如果出错，则返回 `-1`，并设置`errno`。

#### 1.3.2 `epoll` 的优势

1. **文件描述符数量没有限制**

   - `epoll` 对文件描述符的数量没有限制，因此最大数量与系统能够打开的文件描述符数量有关。

2. **不需要每次调用都从用户空间复制到内核**

   - `epoll` 不再需要每次调用都将 `fd_set` 复制到内核，这减少了内核与用户空间之间的数据传输开销。

3. **被动触发机制**

   - 与 `select` 和 `poll` 的主动轮询不同，`epoll` 采用被动触发的方式。为每个文件描述符注册相应的事件和回调函数，当数据准备好后，就会将就绪的文件描述符加入到就绪队列中。`epoll_wait` 的工作方式是查看这个就绪队列中是否有就绪的文件描述符，如果有，就唤醒等待者并调用回调函数。

4. **精准的文件描述符就绪通知**
   - `select` 和 `poll` 只能通知有文件描述符已经就绪，但无法知道具体哪个文件描述符就绪，因此需要主动轮询找到就绪的文件描述符。而 `epoll` 可以直接知道就绪的文件描述符编号，避免了不必要的轮询，提高了效率。

### 1.4 `select`、`poll` 和 `epoll` 的比较

**`select` 和 `poll` 的缺点**：

1. 单个进程能够监视的文件描述符的数量存在最大限制，通常是 `1024`，当然可以更改数量，但由于 `select` 采用轮询的方式扫描文件描述符，文件描述符数量越多，性能越差；在 linux 内核头文件中，有这样的定义：`#define __FD_SETSIZE 1024`
2. 内核 `/` 用户空间内存拷贝问题，`select` 需要复制大量的句柄数据结构，产生巨大的开销
3. `select` 返回的是含有整个句柄的数组，应用程序需要遍历整个数组才能发现哪些句柄发生了事件
4. `select` 的触发方式是水平触发，应用程序如果没有完成对一个已经就绪的文件描述符进行 IO 操作，那么之后每次 `select` 调用还是会将这些文件描述符通知进程

相比 `select` 模型，`poll` 使用链表保存文件描述符，因此没有了监视文件数量的限制，但其他三个缺点依然存在。

以 `select` 模型为例，假设服务器需要支持 100 万的并发连接，则在 `__FD_SETSIZE` 为 1024 的情况 下，至少需要开辟 1k 个进程才能实现 100 万的并发连接。除了进程间上下文切换的时间消耗外，从 内核/用户 空间大量的句柄结构内存拷贝、数组轮询等，是系统难以承受的。因此，基于 `select` 模型的服务器程序，要达到 100 万级别的并发访问，是一个很难完成的任务。

**`epoll` 原理以及优势**

`epoll` 的实现机制与 `select/poll` 机制完全不同，它们的缺点在 `epoll` 上不复存在。

**设想一下如下场景**：有 100 万个客户端同时与一个服务器进程保持着 TCP 连接。而每一时刻，通常只有几百上千个 TCP 连接是活跃的(事实上大部分场景都是这种情况)。如何实现这样的高并发?

在 `select/poll` 时代，服务器进程每次都把这 100 万个连接告诉操作系统(从用户态复制句柄数据结构到内核态)，让操作系统内核去查询这些套接字上是否有事件发生，轮询完成后，再将句柄数据复制到用户态，让服务器应用程序轮询处理已发生的网络事件，这一过程资源消耗较大，因此，`select/poll` 一般只能处理几千的并发连接。

`epoll` 的设计和实现与 `select` 完全不同。`epoll` 通过在 Linux 内核中申请一个简易的文件系统(文件系统一般用 B+ 树实现，磁盘 IO 消耗低，效率很高)。把原先的 `select/poll` 调用分成以下 3 个部分：

1. 调用 `epoll_create()` 建立一个 `epoll` 对象（在 `epoll` 文件系统中为这个句柄对象分配资源）
2. 调用 `epoll_ctl` 向 `epoll` 对象中添加这 100 万个连接的套接字
3. 调用 `epoll_wait` 收集发生的事件的 `fd` 资源

如此一来，要实现上面说是的场景，只需要在进程启动时建立一个 `epoll` 对象，然后在需要的时候向这个 `epoll` 对象中添加或者删除事件。同时，`epoll_wait` 的效率也非常高，因为调用 `epoll_wait` 时，并没有向操作系统复制这 100 万个连接的句柄数据，内核也不需要去遍历全部的连接。

`epoll_create` 在内核上创建的 `eventpoll` 结构如下:

```cpp
struct eventpoll {
  ....
  /*红黑树的根节点，这颗树中存储着所有添加到 epoll 中的需要监控的事件*/
  struct rb_root rbr;

  /*双链表中则存放着将要通过epoll_wait返回给用户的满足条件的事件*/
  struct list_head rdlist;
  ....
};
```

## 2 阻塞、非阻塞、同步、异步

一个典型的网络 IO 接口调用可以分为两个阶段：**数据准备和数据读写**

### 2.1 数据准备

#### 2.1.1 阻塞

在阻塞 IO 中，调用 IO 方法的线程会一直等待，直到数据准备就绪。这意味着线程会被阻塞，不能执行其他任务。常见的阻塞 IO 函数调用如下：

```cpp
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
int size = recv(fd, buf, 1024, 0);
```

#### 2.1.2 非阻塞

在非阻塞 IO 中，调用 IO 方法的线程不会等待数据准备就绪，而是立即返回。线程可以继续执行其他任务，**并通过返回值判断数据是否已经准备好**。如果数据未准备好，通常会返回一个特定的错误码（例如 `EAGAIN` 或 `EWOULDBLOCK`）。

### 2.2 数据读写

#### 2.2.1 同步

> **通过 `man recv` 查看相关函数的文档**

同步 IO 表示数据的读写操作是由调用者（请求方）自己来完成的，无论是阻塞还是非阻塞。在同步 IO 中，调用者必须等待数据读写操作的完成。

#### 2.2.2 异步

> **通过 `man aio_read` 和 `man air_write` 查看相关函数的文档**

异步 IO 则是调用者发出请求后，可以立即继续执行其他任务，不需要等待数据读写操作完成。当数据准备好或读写操作完成后，会通过回调函数、信号等方式通知调用者。异步 IO 通常需要特殊的 API 支持，例如`aio_read`和`aio_write`。

**在处理 IO 的时候，阻塞和非阻塞都是同步 IO。只有使用了特殊的 API 才是异步 IO**

![20240616171557](https://cdn.jsdelivr.net/gh/Corner430/Picture/images/20240616171557.png)

### 2.3 四种 IO 模型

- 同步阻塞 IO
- 同步非阻塞 IO
- 异步阻塞 IO
- 异步非阻塞 IO

```cpp
/****************** 同步阻塞 *****************/

// 在同步阻塞模型中，线程会等待数据准备好，然后进行数据读写操作。
// 这是最简单的模型，也是最常见的 IO 操作方式。
int size = recv(fd, buf, 1024, 0); // 阻塞等待数据


/****************** 同步非阻塞 *****************/

// 在同步非阻塞模型中，线程不会等待数据准备好，而是立即返回。
// 并通过轮询或其他机制检查数据是否准备好。
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);

int size = recv(fd, buf, 1024, 0);
if (size == -1 && errno == EAGAIN) {
    // 数据未准备好，稍后再试
}


/****************** 异步阻塞 *****************/

// 虽然概念上存在，但实际上异步阻塞组合是矛盾的，因为异步操作本质上应该是非阻塞的。
// 异步 IO 意味着调用者不会等待数据准备或读写操作的完成。


/****************** 异步非阻塞 *****************/

// 在异步非阻塞模型中，调用者发出 IO 请求后立即返回，不等待数据读写操作的完成。
// 当操作完成后，通过回调或信号通知调用者。
struct aiocb cb;
cb.aio_fildes = fd;
cb.aio_buf = buf;
cb.aio_nbytes = 1024;
cb.aio_offset = 0;
cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
cb.aio_sigevent.sigev_signo = SIGUSR1;

// 发出异步读请求
aio_read(&cb);

// 处理其他逻辑，等待读写操作完成的通知
```

## 3 Unix/Linux 上的五种 IO 模型

### 3.1 阻塞 blocking

![20240616171924](https://cdn.jsdelivr.net/gh/Corner430/Picture/images/20240616171924.png)

### 3.2 非阻塞 non-blocking

```cpp
int opt = 1;
setsockopt(sockfd, SOL_SOCKET, SO_NONBLOCK, &opt, sizeof(opt));
```

![20240616171956](https://cdn.jsdelivr.net/gh/Corner430/Picture/images/20240616171956.png)

### 3.3 IO 复用（IO multiplexing）

在 IO 复用模型中，使用 `select`、`poll` 或 `epoll` 等系统调用，可以同时监视多个文件描述符，当其中一个或多个文件描述符准备好时，通知应用程序进行处理。这种模型常用于处理高并发连接。

![20240616172046](https://cdn.jsdelivr.net/gh/Corner430/Picture/images/20240616172046.png)

### 3.4 信号驱动(signal-driven)

在信号驱动 IO 模型中，应用程序为文件描述符设置信号驱动模式，当数据准备好时，内核会向应用程序发送信号，通知其进行数据处理。应用程序不需要频繁轮询。

设置信号驱动模式：

```cpp
fcntl(sockfd, F_SETFL, O_ASYNC);
signal(SIGIO, signal_handler);
```

![20240616172109](https://cdn.jsdelivr.net/gh/Corner430/Picture/images/20240616172109.png)

内核在第一个阶段是异步，在第二个阶段是同步；与非阻塞 IO 的区别在于它提供了消息通知机制，不需要用户进程不断的轮询检查，减少了系统 API 的调用次数，提高了效率。

### 3.5 异步(asynchronous)

在异步 IO 模型中，应用程序发起 IO 请求后立即返回，内核会在数据准备好并完成读写操作后，**通过回调函数或信号通知应用程序**。应用程序可以在等待 IO 完成的同时执行其他任务。

定义异步 IO 控制块：

```cpp
struct aiocb {
  int aio_fildes;             // 文件描述符
  off_t aio_offset;           // 偏移量
  volatile void *aio_buf;     // 缓冲区
  size_t aio_nbytes;          // 传输字节数
  int aio_reqprio;            // 请求优先级
  struct sigevent aio_sigevent; // 信号事件
  int aio_lio_opcode;         // 操作码
};
```

![20240616172210](https://cdn.jsdelivr.net/gh/Corner430/Picture/images/20240616172210.png)

## 4 优秀的网络服务器设计

_libev_ 作者的观点：_one loop per thread is usually a good model_，这样多线程服务端编程的问题就转换为如何设计一个高效且易于使用的 `event loop`，然后每个线程 **_run_** 一个 `event loop` 就行了(当然线程间的同步、互斥少不了，还有其它的耗时事件需要起另外的线程来做)。

`event loop` 是 `non-blocking` 网络编程的核心，**`non-blocking` 几乎总是和 `IO-multiplexing` 一起使用**，原因有两点:

- 没有人真的会用轮询 (`busy-pooling`) 来检查某个 `non-blocking` IO 操作是否完成，这样太浪费 CPU 资源了。
- `IO-multiplex` 一般不能和 `blocking` IO 用在一起，因为 `blocking` IO 中 `read()/write()/accept()/connect()` 都有可能阻塞当前线程，这样线程就没办法处理其他 socket 上的 IO 事件了。

所以，当我们提到 `non-blocking` 的时候，实际上指的是 `non-blocking` + `IO-multiplexing`，单用其中任何一个都没有办法很好的实现功能。

## 5 Reactor 模型

参见[小林coding](https://xiaolincoding.com/os/8_network_system/reactor.html#reactor)

## 6 源码剖析

### 6.1 `muduo` 网络库的核心模块

所谓 `Reactor` 模式，是指有一个循环的过程，不断监听对应事件是否触发，事件触发时调用对应的 `callback` 进行处理。

这里的事件在 `muduo` 中包括 `Socket` 可读写事件、定时器事件。

- 负责事件循环的部分在 `muduo` 中被命名为 `EventLoop`
- 负责监听事件是否触发的部分在 `muduo` 中被命名为 `Poller`。`muduo` 提供了 `epoll` 和 `poll` 两种来实现，默认是 `epoll` 实现。
    - 通过环境变量 `MUDUO_USE_POLL` 来决定是否使用 `poll`:
        ```cpp
        // 静态成员函数，根据环境变量决定返回哪种Poller实例
        Poller *Poller::newDefaultPoller(EventLoop *loop) {
        if (::getenv("MUDUO_USE_POLL")) {
            // 如果环境变量"MUDUO_USE_POLL"被设置，则返回nullptr，表示使用poll
            return new PollPoller(loop); // 生成poll的实例
        } else {
            // 否则，返回EPollPoller的实例，表示使用epoll
            return new EPollPoller(loop); // 生成epoll的实例
        }
        }
        ```
- `Acceptor` 负责 `accept` 新连接，并将新连接分发到 `subReactor` 中

### 6.2 简单示例

一个典型的 `muduo` 的 TcpServer 工作流程如下：

1. 创建一个事件循环 `EventLoop` 对象
2. 建立对应的业务服务器 `TcpServer` 对象，将 `EventLoop` 对象传入
3. 设置 `TcpServer` 对象的 `Callback` 函数
4. 启动 `TcpServer` 对象
5. 开启事件循环

```cpp
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <string>
using namespace muduo;
using namespace muduo::net;

void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time) {
  // std::string msg(buf->retrieveAllAsString());
  // conn->send(msg);
  conn->send(buf);
}

int main() {
  EventLoop loop;
  InetAddress listenAddr(9981);
  TcpServer server(&loop, listenAddr, "EchoServer");
  server.setMessageCallback(onMessage);
  server.start();
  loop.loop();
  return 0;
}
```

编译：`g++ -g -o echo_server echo_server.cc -lmuduo_net -lmuduo_base -lpthread`

### 6.3 事件处理

陈硕认为，TCP 网络编程的本质是处理三个半事件，即：

1. 连接的建立
2. 连接的断开：包括主动断开和被动断开
3. 消息到达，文件描述符可读。
4. 消息发送完毕。这个算半个事件。

#### 6.3.1 连接的建立

当单纯使用 linux 的 API，编写一个简单的 Tcp 服务器时，建立一个新的连接通常需要四步：

> 步骤 1. `socket()` // 调用 `socket` 函数建立监听 `socket`
> 
> 步骤 2. `bind()` // 绑定地址和端口
> 
> 步骤 3. `listen()` // 开始监听端口
> 
> 步骤 4. `accept()` // 返回新建立连接的 `fd`

-----

首先在 `TcpServer` 对象构建时，`TcpServer` 的属性 `acceptor` 同时也被建立。

在 `Acceptor` 的构造函数中分别调用了 `socket` 函数和 `bind` 函数完成了**步骤 1 和步骤 2**。

即，当 `TcpServer server(&loop, listenAddr)` 执行结束时，监听 `socket` 已经建立好，并已绑定到对应地址和端口了。

----

而当执行 `server.start()` 时，主要做了两个工作：

1. 在监听 `socket` 上启动 `listen` 函数，也就是**步骤 3**；
2. 将监听 `socket` 的可读事件注册到 `EventLoop` 中。

此时，程序已完成对 `socket` 的监听，但还不够，因为此时程序的主角 `EventLoop` 尚未启动。当调用 `loop.loop()` 时，程序开始循环监听该 `socket` 的可读事件。

当新连接请求建立时，可读事件触发，此时该事件对应的 `callback` 在 `EventLoop::loop()` 中被调用。该事件的 `callback` 实际上就是 `Acceptor::handleRead()` 方法。

在 `Acceptor::handleRead()` 方法中，做了三件事：

1. 调用了 `accept` 函数，完成了**步骤 4**，实现了连接的建立。得到一个已连接 `socket` 的 `fd`。
2. 创建 `TcpConnection` 对象。
3. 将已连接 `socket` 的可读事件注册到 `EventLoop` 中。

这里还有一个需要注意的点，创建的 `TcpConnnection` 对象是个 `shared_ptr`，该对象会被保存在 `TcpServer` 的 `connections` 中。这样才能保证引用计数大于 `0`，对象不被释放。

至此，一个新的连接已完全建立好，该连接的 `socket` 可读事件也已注册到 `EventLoop` 中了。

#### 6.3.2 消息的读取

假如客户端发送消息，导致已连接 `socket` 的可读事件触发，该事件对应的 `callback` 同样也会在 `EventLoop::loop()` 中被调用。

该事件的 `callback` 实际上就是 `TcpConnection::handleRead` 方法。在 `TcpConnection::handleRead` 方法中，主要做了两件事：

1. 从 `socket` 中读取数据，并将其放入 `inputbuffer` 中
2. 调用 `messageCallback`，执行业务逻辑。

  ```cpp
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
  }
  ```

`messageCallback` 是在建立新连接时，将 `TcpServer::messageCallback` 方法 `bind` 到了 `TcpConnection::messageCallback` 的方法。

`TcpServer::messageCallback` 就是业务逻辑的主要实现函数。通常情况下，我们可以在里面实现消息的编解码、消息的分发等工作，这里就不再深入探讨了。

在我们上面给出的示例代码中，`echo-server` 的 `messageCallback` 非常简单，就是直接将得到的数据，重新 `send` 回去。**在实际的业务处理中，一般都会调用 `TcpConnection::send()` 方法，给客户端回复消息**。

这里需要注意的是，在 `messageCallback` 中，用户会有可能会把任务抛给自定义的 `Worker` 线程池处理。
但是这个在 `Worker` 线程池中任务，切忌直接对 `Buffer` 的操作。因为 `Buffer` 并不是线程安全的。

我们需要记住一个准则:

> **所有对 `IO` 和 `buffer` 的读写，都应该在 `IO` 线程中完成**。

一般情况下，先在交给 `Worker` 线程池之前，应该现在 `IO` 线程中把 `Buffer` 进行切分解包等动作。将解包后的消息交由线程池处理，避免多个线程操作同一个资源。


#### 6.3.3 消息的发送

用户通过调用 `TcpConnection::send()` 向客户端回复消息。由于 `muduo` 中使用了 `OutputBuffer`，因此消息的发送过程比较复杂。

首先需要注意的是线程安全问题, 上文说到对于消息的读写必须都在 `EventLoop` 的同一个线程 (通常称为 IO 线程) 中进行：
因此，`TcpConnection::send` 必须要保证线程安全性，它是这么做的：

```cpp
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
```
检测 `send` 的时候，是否在当前 IO 线程，如果是的话，直接进行写相关操作 `sendInLoop`。如果不在一个线程的话，需要将该任务抛给 IO 线程执行 `runInloop`, 以保证 `write` 动作是在 IO 线程中执行的。

在 `sendInloop` 中，做了下面几件事：

1. 假如 `OutputBuffer` 为空，则直接向 `socket` 写数据
2. 如果向 `socket` 写数据没有写完，则统计剩余的字节个数，并进行下一步。没有写完可能是因为此时 `socket` 的 `TCP` 缓冲区已满了。
3. 如果此时 `OutputBuffer` 中的旧数据的个数和未写完字节个数之和大于 `highWaterMark`，则将 `highWaterMarkCallback` 放入待执行队列中
4. 将对应 `socket` 的可写事件注册到 `EventLoop` 中

> 注意：直到发送消息的时候，`muduo` 才会把 `socket` 的可写事件注册到了 `EventLoop` 中。在此之前只注册了可读事件。

连接 `socket` 的可写事件对应的 `callback` 是 `TcpConnection::handleWrite()`。当某个 `socket` 的可写事件触发时，`TcpConnection::handleWrite` 会做两个工作：

1. 尽可能将数据从 `OutputBuffer` 中向 `socket` 中 `write` 数据
1. 如果 `OutputBuffer` 没有剩余的，**则将该 `socket` 的可写事件移除**，并调用 `writeCompleteCallback`

**为什么要移除可写事件**

因为当 `OutputBuffer` 中没数据时，我们不需要向 `socket` 中写入数据。但是此时 `socket` 一直是处于可写状态的， 这将会导致 `TcpConnection::handleWrite()` 一直被触发。然而这个触发毫无意义，因为并没有什么可以写的。

所以 `muduo` 的处理方式是，当 `OutputBuffer` 还有数据时，`socket` 可写事件是注册状态。当 `OutputBuffer` 为空时，则将 `socket` 的可写事件移除。

此外，`highWaterMarkCallback` 和 `writeCompleteCallback` 一般配合使用，起到限流的作用。


#### 6.3.4 连接的断开

连接的断开分为被动断开和主动断开。主动断开和被动断开的处理方式基本一致。

**被动断开**即客户端断开了连接，`server` 端需要感知到这个断开的过程，然后进行的相关的处理。

其中感知远程断开这一步是在 Tcp 连接的可读事件处理函数 `handleRead` 中进行的：当对 `socket` 进行 `read` 操作时，返回值为 `0`，则说明此时连接已断开。

接下来会做四件事情：

1. 将该 TCP 连接对应的事件从 `EventLoop` 移除
2. 调用用户的 `ConnectionCallback`
3. 将对应的 `TcpConnection` 对象从 `Server` 移除。
4. `close` 对应的 `fd`。此步骤是在析构函数中自动触发的，当 `TcpConnection` 对象被移除后，引用计数为 0，对象析构时会调用 `close`。

### 6.4 `runInLoop` 的实现

在消息的发送过程，为保证对 `buffer` 和 `socket` 的写动作是在 IO 线程中进行，使用了一个 `runInLoop` 函数，将该写任务抛给了 IO 线程处理。此处看下 `runInLoop` 的实现。

```cpp
// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb) {
  if (isInLoopThread()) // 在当前的loop线程中，执行cb
  {
    cb();
  } else // 在非当前loop线程中执行cb , 就需要唤醒loop所在线程，执行cb
  {
    queueInLoop(cb);
  }
}
```

这里可以看到，做了一层判断。如果调用时是此 `EventLoop` 的运行线程，则直接执行此函数。否则调用 `queueInLoop` 函数。`queueInLoop` 的实现如下：

```cpp
// 把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb) {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    pendingFunctors_.emplace_back(cb);
  }

  // 唤醒相应的，需要执行上面回调操作的loop的线程
  // callingPendingFunctors_的意思是：当前loop正在执行回调，但是loop又有了新的回调
  if (!isInLoopThread() || callingPendingFunctors_) {
    wakeup(); // 唤醒loop所在线程
  }
}
```

这里有两个动作：

1. 加锁，然后将该函数放到该 `EventLoop` 的 `pendingFunctors_` 队列中。
2. 判断是否要唤醒 `EventLoop`，如果是则调用 `wakeup()` 唤醒该 `EventLoop`。
这里有几个问题：

- 为什么要唤醒 `EventLoop`？
- `wakeup` 是怎么实现的?
- `pendingFunctors_` 是如何被消费的?

### 6.4.1 为什么要唤醒 `EventLoop`

我们首先调用了 `pendingFunctors_.push_back(cb)`, 将该函数放在 `pendingFunctors_` 中。`EventLoop` 的每一轮循环在最后会调用 `doPendingFunctors` 依次执行这些函数。

而 `EventLoop` 的唤醒是通过 `epoll_wait` 实现的，如果此时该 `EventLoop` 中迟迟没有事件触发，那么 `epoll_wait` 一直就会阻塞。 这样会导致 `pendingFunctors_` 中的任务迟迟不能被执行。

所以必须要唤醒 `EventLoop` ，从而让 `pendingFunctors_` 中的任务尽快被执行。

### 6.4.2 `wakeup` 是怎么实现的

`muduo` 这里采用了对 `eventfd` 的读写来实现对 `EventLoop` 的唤醒。

在 `EventLoop` 建立之后，就创建一个 `eventfd`，并将其可读事件注册到 `EventLoop` 中。

`wakeup()` 的过程本质上是对这个 `eventfd` 进行写操作，以触发该 `eventfd` 的可读事件。这样就起到了唤醒 `EventLoop` 的作用。

```cpp
// 用来唤醒loop所在的线程的
// 向wakeupfd_写一个数据，wakeupChannel就发生读事件，当前loop线程就会被唤醒
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one) {
    LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
  }
}
```

很多库为了兼容 `macOS`，往往使用 `pipe` 来实现这个功能。`muduo` 采用了 `eventfd`，性能更好些，但代价是不能支持 `macOS` 了。

### 6.4.3 `doPendingFunctors` 的实现

本部分为 `doPendingFunctors` 的实现，`muduo` 是如何处理这些待处理的函数的，以及中间用了哪些优化操作。

```cpp
void EventLoop::doPendingFunctors() // 执行回调
{
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  {
    std::unique_lock<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  for (const Functor &functor : functors) {
    functor(); // 执行当前loop需要执行的回调操作
  }

  callingPendingFunctors_ = false;
}
```

从代码可以看到，函数非常简单。大概只有十行代码，但是这十行代码中却有两个非常巧妙的地方。

1. `callingPendingFunctors_` 的作用

从代码可以看出，如果 `callingPendingFunctors_` 为 `false`，则说明此时尚未开始执行 `doPendingFunctors` 函数。

这个有什么作用呢？需要结合下 `queueInLoop` 中对是否执行 `wakeup()` 的判断

```cpp
if (!isInLoopThread() || callingPendingFunctors_)
{
  wakeup();
}
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