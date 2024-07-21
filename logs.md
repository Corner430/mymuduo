# 开发日志

#### 1 CMakeLists.txt 和 .gitignore 文件的编写

#### 2 noncopyable 类的编写，禁止拷贝构造和赋值构造

**作为基类**

#### 3 Logger （日志）类的编写

**单例模式**

1. 多行的宏可以使用 `do { ... } while(0)` 包裹。
    - 语法完整性
    - 单语句行为
    - 避免变量作用域冲突
2. 可变参宏的编写
3. 提供给外部的接口：`LOG_INFO`, `LOG_ERROR`, `LOG_FATAL`, `LOG_DEBUG`

#### 4 Timestamp 类的编写

1. `explicit` 关键字禁止隐式转换
2. 自 Unix 纪元以来的微秒数转换为当前时间
