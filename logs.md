# 开发日志

#### 1 CMakeLists.txt 和 .gitignore 文件

#### 2 noncopyable 类

1. 禁止拷贝构造和赋值构造
2. 作为基类

#### 3 Logger （日志）类

**单例模式**

1. 多行的宏可以使用 `do { ... } while(0)` 包裹。
    - 语法完整性
    - 单语句行为
    - 避免变量作用域冲突
2. 可变参宏的编写
3. 提供给外部的接口：`LOG_INFO`, `LOG_ERROR`, `LOG_FATAL`, `LOG_DEBUG`

#### 4 Timestamp 类

1. `explicit` 关键字禁止隐式转换
2. 自 Unix 纪元以来的微秒数转换为当前时间

#### 5 InetAddress 类

1. `sockaddr_in`,`bzero` 的使用
2. `htonl`, `ntohl`, `htons`, `ntohs` 函数的使用
    1. `htonl`: host to network long, 将主机字节序转换为网络字节序
    2. `htons`: host to network short, 将主机字节序转换为网络字节序
    3. `ntohl`: network to host long, 将网络字节序转换为主机字节序
    4. `ntohs`: network to host short, 将网络字节序转换为主机字节序
3. 不再解析 ipv6 地址

#### 6 Channel 类

1. 创建 Channel 文件，创建 TcpServer, EventLoop 空实现文件
2. **明确 TcpServer, EventLoop, Poller, Channel 的关系和作用**
    - TcpServer 是对外的服务器编程使用的类
    - EventLoop 是事件循环类
    - Channel
        1. 封装了 `sockfd` 和 `sockfd` 上感兴趣以及发生的事件，例如 `EPOLLIN`, `EPOLLOUT`等
        2. 绑定了 Poller 返回的具体事件。
        3. 负责调用具体事件的回调操作(因为它可以获知 fd 最终发生的具体事件 revents)
    - Poller 是对 epoll 的封装类
3. class 的前置声明
    - 减少编译依赖
    - 避免循环依赖
    - 提高编译速度
    - 减少不必要的耦合
4. `weak_ptr` 用来**跨线程提权来确定对象是否还存在**
5. 左值之间用 `std::move` 转换为右值引用（移动语义）
6. `update()` 方法会调用 Poller 的 `updateChannel()` 方法，将自己加入到 Poller 中，然后 Poller 会调用 `epoll_ctl()` 方法，将自己加入到 epoll 中