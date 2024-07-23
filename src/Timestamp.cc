#include "Timestamp.h"

#include <time.h>

// Timestamp 类的默认构造函数，初始化微秒数为 0
Timestamp::Timestamp() : microSecondsSinceEpoch_(0) {}

// 带参数的构造函数，使用初始化列表初始化成员变量
// 参数: microSecondsSinceEpoch 自 Unix 纪元以来的微秒数
Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
    : microSecondsSinceEpoch_(microSecondsSinceEpoch) {}

// 静态成员函数，返回当前时间的 Timestamp 对象
// 这里使用 time(NULL) 获取当前时间的秒数，并转换为 Timestamp 对象
Timestamp Timestamp::now() { return Timestamp(time(NULL)); }

// 将 Timestamp 对象转换为字符串表示形式
// 返回格式: "YYYY/MM/DD HH:MM:SS"
std::string Timestamp::toString() const {
  char buf[128] = {0};
  // 将微秒数转换为 time_t 类型，然后转换为 tm 结构体表示本地时间
  tm *tm_time = localtime(&microSecondsSinceEpoch_);
  // 格式化时间为字符串
  snprintf(buf, 128, "%4d/%02d/%02d %02d:%02d:%02d", tm_time->tm_year + 1900,
           tm_time->tm_mon + 1, tm_time->tm_mday, tm_time->tm_hour,
           tm_time->tm_min, tm_time->tm_sec);
  return buf;
}

/* 测试代码 */
// #include <iostream>
// int main() {
//   // 打印当前时间的字符串表示形式
//   std::cout << Timestamp::now().toString() << std::endl;
//   return 0;
// }
