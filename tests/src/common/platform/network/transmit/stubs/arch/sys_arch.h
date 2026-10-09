#pragma once
typedef int sys_sem_t;
typedef int sys_mutex_t;
typedef int sys_mbox_t;
typedef int sys_thread_t;
#define SYS_ARCH_DECL_PROTECT(level) unsigned level
#define SYS_ARCH_PROTECT(level)      (void)(level = 0)
#define SYS_ARCH_UNPROTECT(level)    (void)level
#define sys_mbox_valid(box)          (*(box) != 0)
#define sys_sem_valid(sem)           (*(sem) != 0)
