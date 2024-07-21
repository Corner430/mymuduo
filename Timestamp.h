#pragma once

#include <iostream>
#include <string>

class Timestamp {
public:
  Timestamp(); // 默认构造函数

  // 带参数的构造函数，禁止隐式转换
  // 参数: microSecondsSinceEpoch 自 Unix 纪元以来的微秒数
  explicit Timestamp(int64_t microSecondsSinceEpoch);

  // 静态成员函数，返回当前时间的 Timestamp 对象
  static Timestamp now();

  // 将时间转换为字符串表示形式
  std::string toString() const;

private:
  // 成员变量，保存自 Unix 纪元以来的微秒数
  int64_t microSecondsSinceEpoch_;
};
