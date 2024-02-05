#include "sched.h"
#include "stdio.h"
#include "test.h"
#include "sched.h"

int start_kernel() {
  puts("ZJU OSLAB 5\n");
  puts("3210102037 徐铭");
  puts("3210106039 王思雨");
  
  slub_init();
  task_init();

  // 设置第一次时钟中断
  asm volatile("ecall");
  
  init_test_case();
  call_first_process();
  
  dead_loop();
  return 0;
}
