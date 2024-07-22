#pragma once

#include <sys/syscall.h>
#include <unistd.h>

namespace CurrentThread {
extern __thread int t_cachedTid; // 声明一个线程局部变量，用于缓存当前线程的 tid

/* 如果每次都调用 syscall(SYS_gettid) 来获取当前线程的 tid，
 * 会由于内核/用户态的切换而影响性能，因此可以使用线程局部变量来缓存当前线程的
 * tid，这样就可以减少调用 syscall(SYS_gettid) 的次数。*/
void cacheTid(); // 缓存当前线程的 tid

inline int tid() { // 内联函数，返回当前线程的 tid
  if (__builtin_expect(t_cachedTid == 0, 0))
    cacheTid();
  return t_cachedTid; // 返回缓存的tid
}
} // namespace CurrentThread