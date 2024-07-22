#include "CurrentThread.h"

namespace CurrentThread {
__thread int t_cachedTid = 0; // 定义并初始化线程局部变量 t_cachedTid

void cacheTid() { // 缓存当前线程的 tid
  if (t_cachedTid == 0)
    t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid));
}
} // namespace CurrentThread