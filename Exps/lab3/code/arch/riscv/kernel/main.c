#include "sched.h"
#include "stdio.h"
#include "test.h"

int start_kernel() {
  puts("ZJU OSLAB 3\n");
  puts("学号:3210102037 姓名:Xu Ming\n");
  puts("学号:3210106039 姓名:Wang Siyu\n");
  
  
  task_init();

  // 设置第一次时钟中断
  asm volatile("ecall");
  
  init_test_case();
  call_first_process();
  
  dead_loop();
  return 0;
}
