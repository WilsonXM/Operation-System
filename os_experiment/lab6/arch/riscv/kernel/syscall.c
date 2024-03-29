#include "syscall.h"
#include "list.h"
#include "riscv.h"
#include "sched.h"
#include "task_manager.h"
#include "stdio.h"
#include "defs.h"
#include "slub.h"
#include "mm.h"
#include "vm.h"

extern uint64_t text_start;
extern uint64_t rodata_start;
extern uint64_t data_start;
extern uint64_t user_program_start;
extern void trap_s_bottom(void);

int strcmp(const char *a, const char *b) {
  while (*a && *b) {
    if (*a < *b)
      return -1;
    if (*a > *b)
      return 1;
    a++;
    b++;
  }
  if (*a && !*b)
    return 1;
  if (*b && !*a)
    return -1;
  return 0;
}

uint64_t get_program_address(const char * name) {
    uint64_t offset = 0;
    if (strcmp(name, "hello") == 0) offset = PAGE_SIZE;
    else if (strcmp(name, "malloc") == 0) offset = PAGE_SIZE * 2;
    else if (strcmp(name, "print") == 0) offset = PAGE_SIZE * 3;
    else if (strcmp(name, "guess") == 0) offset = PAGE_SIZE * 4;
    else {
        printf("Unknown user program %s\n", name);
        while (1);
    }
    return PHYSICAL_ADDR((uint64_t)(&user_program_start) + offset);
}

struct ret_info syscall(uint64_t syscall_num, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t sp) {
    uint64_t* sp_ptr = (uint64_t*)(sp);

    struct ret_info ret;
    switch (syscall_num) {
    case SYS_GETPID: {
        ret.a0 = getpid();
        sp_ptr[4] = ret.a0;
        sp_ptr[16] += 4;
        break;
    }
    case SYS_READ: {
        ret.a0 = getchar();
        sp_ptr[4] = ret.a0;
        sp_ptr[16] += 4;
        break;
    }
    case SYS_FORK: {
        // TODO:
        // 1. create new task and set counter, priority and pid (use our task array)
        // 2. create root page table, set current process's satp
        //   2.1 copy current process's user program address, create mapping for user program
        //   2.2 create mapping for kernel address
        //   2.3 create mapping for UART address
        // 3. create user stack, copy current process's user stack and save user stack sp to new_task->sscratch
        // 4. copy mm struct and create mapping
        // 5. set current process a0 = new task pid, sepc += 4
        // 6. copy kernel stack (only need trap_s' stack)
        // 7. set new process a0 = 0, and ra = trap_s_bottom, sp = register number * 8
        
        //1 创建新进程，设置 counter，priority，pid 等内容（counter 可任意设置，但需要不为 0，pid 需要保证不重复）。注意存放在我们的 task 数组中。
        int i = 0;
        for (i = 0; i < NR_TASKS; i++) {
            if (!task[i] || task[i]->counter == 0)
                break;
        }
        if (!task[i])
            task[i] = (struct task_struct*)(VIRTUAL_ADDR(alloc_page()));
        task[i]->state = TASK_RUNNING;
        task[i]->counter = 1000;
        task[i]->priority = 999;
        task[i]->blocked = 0;
        task[i]->pid = i;
        //2 创建页表，设置 task->satp
        uint64_t root_page_table = alloc_page();
        task[i]->satp = root_page_table >> 12 | 0x8000000000000000 | (((uint64_t) (task[i]->pid))  << 44);
        //2.1 拷贝当前进程的用户进程代码地址，建立映射（当前进程运行的用户进程代码起始地址保存在 task->mm.user_program_start 处，用户进程代码占用一页大小）
        task[i]->mm.user_program_start=current->mm.user_program_start;
        create_mapping((uint64_t*)root_page_table, 0x1000000, task[i]->mm.user_program_start, PAGE_SIZE, PTE_V | PTE_R | PTE_X | PTE_U | PTE_W);
        //2.2 创建内核地址映射 （和 Lab 3 一致，包含 0xffff.... 的虚拟内存映射和等值映射）
        // 调用 create_mapping 函数将虚拟地址 0xffffffc000000000 开始的 16 MB 空间映射到起始物理地址为 0x80000000 的 16MB 空间
        create_mapping((uint64_t*)root_page_table, 0xffffffc000000000, 0x80000000, 16 * 1024 * 1024, PTE_V | PTE_R | PTE_W | PTE_X);
        // 修改对内核空间不同 section 所在页属性的设置，完成对不同section的保护，其中text段的权限为 r-x, rodata 段为 r--, 其他段为 rw-。
        create_mapping((uint64_t*)root_page_table, 0xffffffc000000000, 0x80000000, PHYSICAL_ADDR((uint64_t)&rodata_start) - 0x80000000, PTE_V | PTE_R | PTE_X);
        create_mapping((uint64_t*)root_page_table, (uint64_t)&rodata_start, PHYSICAL_ADDR((uint64_t)&rodata_start), (uint64_t)&data_start - (uint64_t)&rodata_start, PTE_V | PTE_R);
        create_mapping((uint64_t*)root_page_table, (uint64_t)&data_start, PHYSICAL_ADDR((uint64_t)&data_start), (uint64_t)&_end - (uint64_t)&data_start, PTE_V | PTE_R | PTE_W);
        // 对内核起始地址 0x80000000 的16MB空间做等值映射（将虚拟地址 0x80000000 开始的 16 MB 空间映射到起始物理地址为 0x80000000 的 16MB 空间）
        create_mapping((uint64_t*)root_page_table, 0x80000000, 0x80000000, 16 * 1024 * 1024, PTE_V | PTE_R | PTE_W | PTE_X);
        // 修改对内核空间不同 section 所在页属性的设置，完成对不同section的保护，其中text段的权限为 r-x, rodata 段为 r--, 其他段为 rw-。
        create_mapping((uint64_t*)root_page_table, 0x80000000, 0x80000000, PHYSICAL_ADDR((uint64_t)&rodata_start) - 0x80000000, PTE_V | PTE_R | PTE_X);
        create_mapping((uint64_t*)root_page_table, PHYSICAL_ADDR((uint64_t)&rodata_start), PHYSICAL_ADDR((uint64_t)&rodata_start), (uint64_t)&data_start - (uint64_t)&rodata_start, PTE_V | PTE_R);
        create_mapping((uint64_t*)root_page_table, PHYSICAL_ADDR((uint64_t)&data_start), PHYSICAL_ADDR((uint64_t)&data_start), (uint64_t)&_end - (uint64_t)&data_start, PTE_V | PTE_R | PTE_W);
        //2.3 创建 UART 的硬件地址映射（和 Lab 3 一致）
        // 将必要的硬件地址（如 0x10000000 为起始地址的 UART ）进行等值映射 ( 可以映射连续 1MB 大小 )，无偏移，3 为映射的读写权限
        create_mapping((uint64_t*)root_page_table, 0x10000000, 0x10000000, 1 * 1024 * 1024, PTE_V | PTE_R | PTE_W | PTE_X);
        //3 分配一页用来做新进程的用户栈，拷贝当前进程的用户栈内容到新进程中，设置好新进程的用户栈指针(task->sscrath)。另外，将用户栈物理地址保存到 task->mm.user_stack 中，方便之后的回收。
        uint64_t physical_stack = alloc_page();
        memcpy((uint64_t *)physical_stack, (uint64_t *)current->mm.user_stack, PAGE_SIZE);
        task[i]->sscratch = read_csr(sscratch);
        task[i]->mm.user_stack = physical_stack;
        create_mapping((uint64_t*)root_page_table, 0x1001000, physical_stack, PAGE_SIZE, PTE_V | PTE_R | PTE_W | PTE_U);
        //4 拷贝动态内存管理部分 task->mm.vm ，拷贝动态内存分配的空间内容，并且建立对应的映射。
        task[i]->mm.vm = kmalloc(sizeof(struct vm_area_struct));
        INIT_LIST_HEAD(&(task[i]->mm.vm->vm_list));
        struct vm_area_struct* vma;
        list_for_each_entry(vma, &current->mm.vm->vm_list, vm_list) {
            struct vm_area_struct * copy = kmalloc(sizeof(struct vm_area_struct));
            memcpy(copy, vma, sizeof(struct vm_area_struct));
            list_add(&(copy->vm_list), &task[i]->mm.vm->vm_list);
            if (vma->mapped) {
                uint64_t pa = alloc_pages((vma->vm_end - vma->vm_start) / PAGE_SIZE);
                create_mapping((uint64_t*)root_page_table, vma->vm_start, pa, vma->vm_end - vma->vm_start, vma->vm_flags);
                uint64_t pte = get_pte((current->satp & ((1ULL << 44) - 1)) << 12, vma->vm_start);
                memcpy((uint64_t *)pa, (uint64_t *)((pte >> 10) << 12), vma->vm_end - vma->vm_start);
            }
        }
        //5 正确设置当前进程内核栈上的 a0, sepc 值
        sp_ptr[4] = task[i]->pid;//返回值是pid
        sp_ptr[16] += 4;//sepc
        //6 拷贝内核栈内容（只需要拷贝 trap_s 的内核栈）
        memcpy((uint64_t*)((uint64_t)task[i] + PAGE_SIZE - 31 * 8), (uint64_t*)((uint64_t)current + PAGE_SIZE - 31 * 8), 31 * 8);
        //7 正确设置新进程内核栈上的 a0, sepc 值，以及 task->thread.sp ， task->thread.ra 。
        *(uint64_t *)((uint64_t)(sp_ptr + 4) - (uint64_t)current + (uint64_t)task[i]) = 0;
        task[i]->thread.sp = (uint64_t)task[i] + PAGE_SIZE - 31 * 8;
        task[i]->thread.ra = (uint64_t)&trap_s_bottom;
        break;
    }
    case SYS_EXEC: {
        // TODO:
        // 1. free current process vm_area_struct and it's mapping area
        // 2. reset user stack, user_program_start
        // 3. create mapping for new user program address
        // 4. set sepc = 0x1000000
        // 5. refresh TLB

        // 1. 清空当前进程的动态内存分配区域 `task->mm.vm` ，包括映射和空间回收
        uint64_t root_pgtable = (current->satp & ((1ULL << 44) - 1)) << 12;
        struct vm_area_struct *vma = NULL;
        list_for_each_entry(vma, &current->mm.vm->vm_list, vm_list) {
            if(vma->mapped == 1) 
                free_pages(((get_pte((uint64_t *)root_pgtable, vma->vm_start)) >> 10) << 12);
            create_mapping((uint64_t *)root_pgtable, vma->vm_start, 0, vma->vm_end-vma->vm_start, 0);
            list_del(&(vma->vm_list));
            kfree(vma);
        }
        
        // 2. 重置用户栈指针（ssratch）【我们复用用户栈】和 user_program_start【我们复用用户进程代码】
        write_csr(sscratch, 0x1002000);
        current->mm.user_program_start = get_program_address((char *)arg0);
        // 3. 重新设置用户进程代码的虚拟地址映射【使用 `get_program_address` 获取新的用户进程代码的首地址】 (注意修改了映射后需要使用 sfence.vma 刷新 TLB)
        create_mapping((uint64_t *)root_pgtable, 0x1000000, current->mm.user_program_start, PAGE_SIZE, PTE_U|PTE_V|PTE_R|PTE_W|PTE_X);
        asm volatile("sfence.vma");
        // 4. 重新设置 sepc
        sp_ptr[16] = 0x1000000;

        break;
    }
    case SYS_EXIT: {
        // TODO:
        // 1. free current process vm_area_struct and it's mapping area
        // 2. free user stack
        // 3. clear current task, set current task->counter = 0
        // 4. call schedule

        // 1. 清空当前进程的动态内存分配区域 `task->mm.vm` ，包括映射和空间回收。
        uint64_t root_pgtable = (current->satp & ((1ULL << 44) - 1)) << 12;
        struct vm_area_struct *vma = NULL;
        list_for_each_entry(vma, &current->mm.vm->vm_list, vm_list) {
            if(vma->mapped == 1) 
                free_pages(((get_pte((uint64_t *)root_pgtable, vma->vm_start)) >> 10) << 12);
            create_mapping((uint64_t *)root_pgtable, vma->vm_start, 0, vma->vm_end-vma->vm_start, 0);
            list_del(&(vma->vm_list));
            kfree(vma);
        }
        // 2. 清空用户栈。
        free_pages(current->mm.user_stack);
        current->mm.user_stack = 0;
        // 3. 清空当前 task_struct 信息，设置 counter = 0，避免再次调度。
        kfree(&(current->mm.vm));
        current->mm.vm = 0;
        free_pages(root_pgtable);
        current->counter = 0;
        // 4. 调用 schedule，执行别的进程。
        schedule();
        break;
    }
    case SYS_WAIT: {
        // TODO:
        // 1. find the process which pid == arg0
        // 2. if not find
        //   2.1. sepc += 4, return
        // 3. if find and counter = 0
        //   3.1. free it's kernel stack and page table
        // 4. if find and counter != 0
        //   4.1. change current process's priority
        //   4.2. call schedule to run other process
        //   4.3. goto 1. check again

        int finish = 1, find = 0;
        while(finish) {
            finish = 0;
            for(int i = 0; i < NR_TASKS; i++) {
                if(task[i] && task[i]->pid == arg0) {
                    find = 1;
                    if(task[i]->counter != 0) {
                        current->priority = task[i]->priority + 1;
                        finish = 1;
                        schedule();
                    }
                    else if(task[i]->counter == 0) {
                        free_pages(PHYSICAL_ADDR(task[i]));
                        task[i] = 0;
                    }
                }
            }
        }
        if(!find) 
            sp_ptr[16] += 4;
        break;
    }
    case SYS_WRITE: {
        int fd = arg0;
        char* buffer = (char*)arg1;
        int size = arg2;
        if(fd == 1) {
            for(int i = 0; i < size; i++) {
                putchar(buffer[i]);
            }
        }
        ret.a0 = size;
        sp_ptr[4] = ret.a0;
        sp_ptr[16] += 4;
        break;
    }
    case SYS_MMAP: {
        struct vm_area_struct* vma = (struct vm_area_struct*)kmalloc(sizeof(struct vm_area_struct));
        if (vma == NULL) {
            ret.a0 = -1;
            break;
        }
        vma->vm_start = arg0;
        vma->vm_end = arg0 + arg1;
        vma->vm_flags = arg2;
        vma->mapped = 0;
        list_add(&(vma->vm_list), &(current->mm.vm->vm_list));

        ret.a0 = vma->vm_start;
        sp_ptr[16] += 4;
        break;
    }
    case SYS_MUNMAP: {
        ret.a0 = -1;
        struct vm_area_struct* vma;
        list_for_each_entry(vma, &current->mm.vm->vm_list, vm_list) {
            if (vma->vm_start == arg0 && vma->vm_end == arg0 + arg1) {
                if (vma->mapped == 1) {
                    uint64_t pte = get_pte((current->satp & ((1ULL << 44) - 1)) << 12, vma->vm_start);
                    free_pages((pte >> 10) << 12);
                }
                create_mapping((current->satp & ((1ULL << 44) - 1)) << 12, vma->vm_start, 0, (vma->vm_end - vma->vm_start), 0);
                list_del(&(vma->vm_list));
                kfree(vma);

                ret.a0 = 0;
                break;
            }
        }
        // flash the TLB
        asm volatile ("sfence.vma");
        sp_ptr[16] += 4;
        break;
    }
    default:
        printf("Unknown syscall! syscall_num = %d\n", syscall_num);
        while(1);
        break;
    }
    return ret;
}