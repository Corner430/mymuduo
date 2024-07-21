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
3. 不再解析 ipv6 地址

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