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
2. **明确 TcpServer 是对外的服务器编程使用的类，EventLoop 是事件循环类，Channel 是 fd 和事件的封装类，Poller 是对 epoll 的封装类**