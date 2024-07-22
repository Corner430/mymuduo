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