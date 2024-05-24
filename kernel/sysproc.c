#include "types.h"
#include "loongarch.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sem.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_chpri(void)
{
  int pid, pr;
  if (argint(0,&pid) < 0)
    return -1;
  if (argint(1, &pr) < 0)
    return -1;
  return chpri(pid,pr);
}

uint64
sys_sh_var_read()
{
  return (uint64)sh_var_for_sem_demo;
}

uint64
sys_sh_var_write()
{
  int n;
  if(argint(0, &n) < 0)
    return (uint64)-1;
  sh_var_for_sem_demo = n;
  return (uint64)sh_var_for_sem_demo;
}

uint64
sys_shmgetat (void)
{
  int key, num;
  if(argint(0, &key) < 0 || argint(1, &num) < 0)
    return -1;
  return (uint64)shmgetat(key,num);
}

uint64
sys_shmrefcount(void)
{
  int key;
  if(argint(0,&key)<0)
    return -1;
  return shmrefcount(key);
}

uint64
sys_mqget(void)
{
  int in;
  if(argint(0,&in)<0)
    return -1;
  return mqget(in);
}

uint64
sys_msgsnd(void)
{
  int mqid;
  int type,sz;
  char * msg = "This is a empty massage";
  if(argint(0, &mqid) < 0 || argint(1, &type) < 0
  || argint(2, &sz) < 0)
    return -1;
  if(argstr(3,msg,sz) < 0)
    return -1;
  return msgsnd(mqid,type,sz, msg);
}

uint64
sys_msgrcv(void)
{
  int a;
  int b,c;
  int d;
  if(argint(0, &a) < 0 || argint(1, &b) < 0
  || argint(2, &c) < 0 || argint(3, &d) < 0)
    return -1;
  return msgrcv(a,b,c,d);
}

uint64 sys_clone(void){
  uint64 a;
  uint64 b;
  uint64 c;
  argaddr(0,&a);
  argaddr(1,&b);
  argaddr(2, &c);
 
  return (uint64)clone((void (*)(void *))a,(void*)b, (void *)c);
}

uint64 sys_join(void){
  return (uint64)join();
}

uint64 sys_myalloc(void){
  int n;
  if(argint(0, &n) < 0)
    return -1;
  return mygrowproc(n);
}

uint64 sys_myfree(void){
  uint64 address;
  if(argaddr(0, &address) < 0)
    return -1;
  return myreduceproc(address);
}

uint64 sys_getcpuid(void){
  return getcpuid();
}
