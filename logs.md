# 开发日志

### 1 CMakeLists.txt 和 .gitignore 文件

### 2 noncopyable 类

1. 禁止拷贝构造和赋值构造
2. 作为基类

### 3 Logger （日志）类

**单例模式**

1. 多行的宏可以使用 `do { ... } while(0)` 包裹。
    - 语法完整性
    - 单语句行为
    - 避免变量作用域冲突
2. 可变参宏的编写
3. 提供给外部的接口：`LOG_INFO`, `LOG_ERROR`, `LOG_FATAL`, `LOG_DEBUG`

### 4 Timestamp 类

1. `explicit` 关键字禁止隐式转换
2. 自 Unix 纪元以来的微秒数转换为当前时间

### 5 InetAddress 类

1. `sockaddr_in`,`bzero` 的使用
2. `htonl`, `ntohl`, `htons`, `ntohs` 函数的使用
    1. `htonl`: host to network long, 将主机字节序转换为网络字节序
    2. `htons`: host to network short, 将主机字节序转换为网络字节序
    3. `ntohl`: network to host long, 将网络字节序转换为主机字节序
    4. `ntohs`: network to host short, 将网络字节序转换为主机字节序
3. 不再支持解析 ipv6 地址

### 6 Channel 类

1. 创建 Channel 文件，创建 TcpServer, EventLoop 空实现文件
2. **明确 TcpServer, EventLoop, Poller, Channel 的关系和作用**
    - TcpServer 是对外的服务器编程使用的类
    - EventLoop 是事件循环类
    - Channel
        1. 封装了 `sockfd` 和 `sockfd` 上感兴趣以及发生的事件，例如 `EPOLLIN`, `EPOLLOUT`等
        2. 绑定了 Poller 返回的具体事件。
        3. 负责调用具体事件的回调操作(因为它可以获知 fd 最终发生的具体事件 revents)
    - Poller 是对 epoll 的封装类
3. 如果只是使用指针类型，使用 class 的前置声明
4. `weak_ptr` 通过 `.lock()` 方法**提权来确定对象是否还存在**
5. 左值之间用 `std::move` 转换为右值引用（移动语义）
6. 1 个 EventLoop 对应 1 个 Poller 和 ChannelList，1 个 Poller 对应多个 Channel
7. `update()` 方法
    1. 当改变 channel 所表示 fd 的 events 后
    2. `update()` 方法负责在 Poller 里更改 fd 相应的事件
    3. 会调用 `epoll_ctl()` 方法
    4. **跨类如何调用的问题**
        1. channel 无法更改 Poller 中的 fd，但是二者都属于 EventLoop 的管理范围
        2. EventLoop 含有 Poller 和 ChannelList
        3. 通过 channel 所属的 EventLoop，调用 Poller 的相应方法，注册 fd 的 events
8. `tie()` 方法的调用时机和作用
    1. 在 TcpConnection 中，当 TcpConnection 被销毁时，需要将 Channel 从 Poller 中移除
    2. 通过 tie() 方法，将 TcpConnection 和 Channel 绑定在一起
    3. 当 TcpConnection 被销毁时，Channel 也会被销毁

### 7 Poller 抽象类

1. *Base class for IO Multiplexing*
2. 给所有 IO 复用保留统一的接口，*Must be called in the loop thread.*
3. `poll()` 方法负责 ***Polls the I/O events.***
4. `updateChannel()` 方法负责 ***Changes the interested I/O events.***
5. `removeChannel()` 方法负责 ***Remove the channel, when it destructs.***
6. **`ChannelMap` 负责保存 `Channel` 和 `fd` 的映射关系，此处双向哈希表的设计值得学习**
7. `static Poller* newDefaultPoller(EventLoop* loop)` 方法的实现需要单独放在一个文件中，**否则就要包含 `Poller.h` 和 `EventLoop.h` 两个头文件（基类包含派生类），造成循环引用。这是一种通用的解决循环引用的方法，值得学习**

### 8 DefaultPoller 类

1. 给出 `EpollPoller` 的空实现，放弃 `PollPoller` 的实现
2. 根据环境变量 `MUDUO_USE_POLL` 来选择 `nullptr` 或 `EpollPoller`
3. `EpollPoller` 即 epoll 的封装类，是 muduo 默认的 I/O 复用类

### 9 EpollPoller 类

***EpollPoller is IO Multiplexing with epoll(4).***

1. `override` 关键字显式地告诉编译器，这个函数是一个虚函数的重写（覆盖）
2. epoll 的使用：`epoll_create()`, `epoll_ctl()`, `epoll_wait()`
    1. 构造函数中创建 `epollfd_`，对应 `epoll_create()`
    2. `updateChannel()` 通过封装的 `update()` 方法 调用 `epoll_ctl()`，注册 fd 的 events
    3. `poll()` 方法中调用 `epoll_wait()`，等待事件发生
    4. 析构函数中关闭 `epollfd_`，对应 `close()`
3. `events_` 数组保存 `epoll_wait()` 返回的 `epoll_event` 结构体，初始大小为 `kInitEventListSize`，**只有扩容操作，没有缩容操作**
4. `update()` 方法 是一个 `epoll_ctl()` 操作，同时让 `event.data.ptr` 指向对应的 channel，**要深切了解 `struct epoll_event`**
5. `updateChannel()` 方法
    1. 更改 channel 的状态(`index`)
    2. **将 channel 添加到 `channels_` 中，也就是当前的 EPollPoller 对象中**
    2. 调用封装的 `update()` 方法，添加、修改、删除 channel 所关注的事件
6. `poll()` 方法
    1. 调用 `epoll_wait()`，等待事件发生
    2. 将 `epoll_wait()` 返回的事件保存到 `events_` 中
    3. 遍历 `events_`，调用封装的 `fillActiveChannels()` 方法，将发生事件的 channel 添加到 `activeChannels` 中，并设定 channel 的 `revents`。**如此一来，EventLoop 就可以通过 activeChannels 获取到发生事件的 Channel**
7. `&*vec.begin()` **可以获得 vector 底层数据的首地址**
8. 四大类型转换之 [`static_cast`](https://github.com/Corner430/study-notes/blob/main/cpp/cpp中级笔记.md#211-c-四种类型转换)
9. `errno` 是全局变量，保存了上一个系统调用的错误码，会被 `epoll_wait()` 修改，所以需要保存

### 10 CurrentThread 类

由于 *one loop per thread* 的设计，所以需要一个类来**获取当前线程的线程 ID**

> 如果每次都调用 syscall(SYS_gettid) 来获取当前线程的 tid，**会由于内核/用户态的切换而影响性能**，因此可以使用线程局部变量来缓存当前线程的 tid，这样就可以减少调用 syscall(SYS_gettid) 的次数。

1. `__thread` 关键字，**线程局部存储**，每个线程都有自己的变量，互不干扰
2. `extern` 关键字，**声明**一个变量，不分配内存，**在其他文件中定义**
3. [`__builtin_expect`](http://blog.man7.org/2012/10/how-much-do-builtinexpect-likely-and.html) 是 GCC 的内建函数，**用来告诉编译器分支预测，提高程序性能**。类似的还有 C++20 的 `[[likely]]` 和 `[[unlikely]]` 关键字，其实现也是通过 GCC 的 `__builtin_expect` 实现的

### 11 EventLoop 类

*EventLoop : Reactor, at most one per thread. This is an interface class, so don't expose too much details.*

1. EventLoop 是一个事件循环类，包含两个大模块：
    - ChannelList：保存了所有的 Channel
        1. 封装了 sockfd 和 sockfd 上感兴趣以及发生的事件
        2. 绑定了 Poller 返回的具体事件
        3. 负责调用具体事件的回调操作(因为它可以获知 fd 最终发生的具体事件revents)
    - Poller : epoll 的抽象类，含有 ChannelMap，保存了 Channel 和 fd 的映射关系
    - **EventLoop 中的 ChannelList 数量大于等于 Poller 中的 Channel 数量**
2. [`atomic`](https://github.com/Corner430/study-notes/blob/main/cpp/cpp高级笔记.md#46-基于-cas-操作的-atomic-原子类型)
3. **通过 [`eventfd`](https://man7.org/linux/man-pages/man2/eventfd.2.html)(代码中是 `wakeupFd_`) 来唤醒 `epoll_wait()`，实现跨线程唤醒 subReactor(subLoop)**。[`socketpair`](https://man7.org/linux/man-pages/man2/socketpair.2.html) 也可以实现，但是 `eventfd` 更加高效
4. [`unique_ptr`](https://github.com/Corner430/study-notes/blob/main/cpp/cpp高级笔记.md#233-unique_ptr) 用来保证只有一个 Poller 对象和 wakeupChannel_ 对象
5. [`unique_lock`](https://github.com/Corner430/study-notes/blob/main/cpp/cpp高级笔记.md#45-lock_guard-和-unique_lock)
6. [`emplace_back`](https://github.com/Corner430/study-notes/blob/main/cpp/cpp高级笔记.md#6-c11-容器-emplace-方法原理剖析)
7. **`doPendingFunctors()` 中通过 `swap` 减小临界区的长度**
8. `callingPendingFunctors_` 用来标记当前是否有任务正在执行，用在 `queueInLoop()` 中
    1. 如果调用 `queueInLoop()` 和 EventLoop 不在同一个线程，或者 `callingPendingFunctors_` 为 `true` 时（此时正在执行 `doPendingFunctors()`，即正在执行回调），则唤醒 loop 所在的线程
    2. 如果调用 `queueInLoop()` 和 EventLoop 在同一个线程，但是 `callingPendingFunctors_` 为 `false` 时，则说明：此时尚未执行到 `doPendingFunctors()`。**不必唤醒，这个优雅的设计可以减少对 `eventfd` 的 IO 读写**
9. `Poller` 的 `poll()` 方法监听两种 `fd`：`wakeupFd_` 和 `epollfd_`
10. **跨线程调用 `quit()` 方法，通过 `wakeup()` 方法唤醒 `subLoop`，实现跨线程唤醒**
11. EventLoop 跨线程通信也可以使用 **生产者-消费者** 模型，mainLoop 作为生产者，subLoop 作为消费者。**muduo 是通过 `wakeupFd_` 来直接通信，非常巧妙**

### 12 Thread 类

EventLoop 线程相关的类：Thread，EventLoopThread，EventLoopThreadPool

Thread 类封装了一个新线程的相关信息，包括线程 ID，线程状态，线程函数等

1. **为避免直接使用 std::thread 导致线程立刻启动，使用智能指针来管理线程**
2. 使用**信号量**来确保主线程等待新线程完成特定的初始化步骤。**这样可以避免在新线程还没有完全初始化之前，主线程就继续执行后续代码，导致未定义的行为或数据竞争问题**，详见 `man 7 sem_overview`

### 13 EventLoopThread 类

**将 loop 和 thread 封装在一起，确保了 one loop per thread ！！！**

1. 在 thread 中创建一个 EventLoop 对象，通过条件变量将值传递给主线程

### 14 EventLoopThreadPool 类

1. EventLoopThreadPool 是一个线程池，每个线程都有一个 EventLoop 对象
2. 通过 round-robin 轮询的方式，将新连接分配给不同的 subLoop
3. **`loops_` 保存了所有的 subLoop，`next_` 保存了下一个 subLoop 的索引**
4. 不需要对 `loops_` 进行析构，因为其指向的对象是栈上的对象，会随着线程的结束而自动销毁

### 15 Socket 类

Socket 类作为 Accepter 类的成员，必须要先进行输出

1. **Socket 类封装了 socket 的 bind, listen, accept 等操作**
2. 结合 InetAddress 类，保存对端地址和端口
3. `shutdownWrite()` 方法用来关闭写端，**[半关闭](https://github.com/Corner430/TCP-IP-NetworkNote?tab=readme-ov-file#713-针对优雅断开的-shutdown-函数)**

### 16 Accepter 类

*Acceptor of incoming TCP connections.*

1. Acceptor 类负责监听新连接，运行在 mainLoop 中
2. `newConnectionCallback_` 负责将新连接分发给 subLoop，**会被 TcpServer 的 `newConnection()` 方法调用**
3. `listen()` 方法负责监听新连接，即调用 `::listen()` 函数，**并将监听套接字的 Channel 注册到 Poller 中**，由 TcpServer 的 `start()` 方法调用
4. `handleRead()` 方法负责处理新连接，通过 `newConnectionCallback_` 回调函数将新连接分发给 subLoop

> **一言以蔽之，封装了 `acceptChannel`, `acceptSocket`, `newConnectionCallback_`，并且将 `acceptChannel` 注册到 `mainLoop` 中，监听新连接。通过 `newConnectionCallback_` 回调函数将新连接轮询分发给 `subLoop`**

### 17 Callback 类

*All client visible callbacks go here.*

`HighWaterMarkCallback` ： **发送数据时，应用写的快，而内核发送数据慢，需要把待发送数据写入缓冲区，当缓冲区的数据超过一定的水位时，就会调用这个回调函数**，用在 `TcpConnection` 类中

### 18 buffer 类

buffer 类是一个缓冲区类，用来保存数据

```shell
* +-------------------+------------------+------------------+
* | prependable bytes |  readable bytes  |  writable bytes  |
* |                   |     (CONTENT)    |                  |
* +-------------------+------------------+------------------+
* |                   |                  |                  |
* 0      <=      readerIndex   <=   writerIndex    <=     size
```

1. 使用 `std::vector<char>` 来保存数据，`readerIndex` 和 `writerIndex` 来标记读写位置，**巧妙的使用 `prependableBytes` 来减少数据的拷贝，仅 `writable bytes` 不足时需要拷贝数据**
2. 使用 `readv` 和 `writev` 系统调用，可以向内核传递多个缓冲区，减少系统调用次数
3. **从 `fd` 中读取数据到 `writeIndex` 位置，向 `fd` 中写入数据从 `readIndex` 位置开始**

### 19 TcpConnection 类

Tcpconnection 是对一条已建立连接的封装，包括 `Channel`, `Socket`(已连接), `Buffer`, `EventLoop`(subReactor), `peerAddress`, `localAddress` 等

> **Tcpconnection 和 Channel 一一对应**

顺序：TcpServer => Acceptor => TcpConnection => Channel => Poller

1. `static` 用来保证在不同的文件中同样函数名不会冲突
2. [`enable_shared_from_this`](https://zh.cppreference.com/w/cpp/memory/enable_shared_from_this) 可以在类中获取 `shared_ptr`
3. TcpConnection 所属的 EventLoop 是 subLoop, 即 subReactor
4. **在第一次调用 `send()` 方法时，Channel 还没有注册 `EPOLLOUT` 事件，仅当没有把数据发送完时，才注册 `EPOLLOUT` 事件**
5. 在 mainReactor 中建立一条新连接时，会调用 `TcpConnection::connectEstablished()` 方法，将相应的 Channel 绑定（`tie()`）到 TcpConnection 上，同时注册 `EPOLLIN` 事件，之后执行 `ConnectionCallback` 回调函数
6. **`handle` 系列方法中会执行用户传入的回调函数，这一系列 `handle` 方法会和 `Channel` 中的回调绑定在一起**

### 20 TcpServer 类

1. TcpServer 是对外的服务器编程使用的类，**是一个对于所有连接的管理类，包括 `Acceptor`, `EventLoop`, `Poller`, `ChannelList`, `TcpConnection`，起到了调动各个类的作用**
2. `start()` 方法负责启动服务器，即调用 `accepter` 的 `listen()` 方法，监听新连接
3. `newConnection()` 方法负责处理新连接，即调用 `accepter` 的 `handleRead()` 方法，处理新连接
4. `unique_ptr` 的 `get()` 方法返回指向的原始指针
5. `shared_ptr` 的 [`reset()` 方法](https://zh.cppreference.com/w/cpp/memory/shared_ptr/reset)