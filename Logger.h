#pragma once
#include "noncopyable.h"
#include <string>

// 用于记录信息级别日志的宏。
// 使用方法: LOG_INFO("%s %d", arg1, arg2)
#define LOG_INFO(logmsgFormat, ...)                                            \
  do {                                                                         \
    Logger &logger = Logger::instance();                                       \
    logger.setLogLevel(INFO);                                                  \
    char buf[1024] = {0};                                                      \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);                          \
    logger.log(buf);                                                           \
  } while (0)

// 用于记录错误级别日志的宏。
// 使用方法: LOG_ERROR("%s %d", arg1, arg2)
#define LOG_ERROR(logmsgFormat, ...)                                           \
  do {                                                                         \
    Logger &logger = Logger::instance();                                       \
    logger.setLogLevel(ERROR);                                                 \
    char buf[1024] = {0};                                                      \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);                          \
    logger.log(buf);                                                           \
  } while (0)

// 用于记录严重错误级别日志并退出程序的宏。
// 使用方法: LOG_FATAL("%s %d", arg1, arg2)
#define LOG_FATAL(logmsgFormat, ...)                                           \
  do {                                                                         \
    Logger &logger = Logger::instance();                                       \
    logger.setLogLevel(FATAL);                                                 \
    char buf[1024] = {0};                                                      \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);                          \
    logger.log(buf);                                                           \
    exit(-1);                                                                  \
  } while (0)

// 用于记录调试级别日志的宏，仅在定义了MUDEBUG时有效。
// 使用方法: LOG_DEBUG("%s %d", arg1, arg2)
#ifdef MUDEBUG
#define LOG_DEBUG(logmsgFormat, ...)                                           \
  do {                                                                         \
    Logger &logger = Logger::instance();                                       \
    logger.setLogLevel(DEBUG);                                                 \
    char buf[1024] = {0};                                                      \
    snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);                          \
    logger.log(buf);                                                           \
  } while (0)
#else
#define LOG_DEBUG(logmsgFormat, ...)
#endif

// 定义日志的级别
enum LogLevel {
  INFO,  // 普通信息
  ERROR, // 错误信息
  FATAL, // 严重错误信息（导致程序退出）
  DEBUG, // 调试信息
};

// 日志类
class Logger : noncopyable {
public:
  static Logger &instance(); // 获取 Logger 的唯一实例对象（单例模式）
  void setLogLevel(int level); // 设置日志级别
  void log(std::string msg);   // 写日志

private:
  int logLevel_; // 当前的日志级别
};
