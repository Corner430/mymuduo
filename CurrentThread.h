#pragma once

#include <sys/syscall.h>
#include <unistd.h>

namespace CurrentThread {
extern __thread int t_cachedTid; // 声明一个线程局部变量，用于缓存当前线程的tid

void cacheTid(); // 声明一个函数，用于缓存当前线程的tid

// 内联函数，返回当前线程的tid
inline int tid() {
  if (__builtin_expect(t_cachedTid == 0, 0)) {
    // 如果t_cachedTid为0，则调用cacheTid()进行缓存
    cacheTid();
  }
  return t_cachedTid; // 返回缓存的tid
}
} // namespace CurrentThread