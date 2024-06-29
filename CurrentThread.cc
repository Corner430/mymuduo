#include "CurrentThread.h"

namespace CurrentThread {
__thread int t_cachedTid = 0; // 定义并初始化线程局部变量t_cachedTid

// 缓存当前线程的tid
void cacheTid() {
  if (t_cachedTid == 0) {
    // 通过Linux系统调用，获取当前线程的tid值
    t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid));
  }
}
} // namespace CurrentThread