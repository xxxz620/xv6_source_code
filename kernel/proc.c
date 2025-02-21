#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "loongarch.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    if(mappages(kpgtbl, va, PGSIZE, (uint64)pa,  PTE_NX | PTE_P | PTE_W | PTE_MAT | PTE_D) != 0)
      panic("kvmmap");
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc)); 
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->slot = SLOT;
  p->priority = 10;
  p->shm = TRAPFRAME -64 *2*PGSIZE;
  p->shmkeymask = 0;
  p->mqmask = 0;
  p->pthread = 0;

  for (int i = 0; i < 10; i++)
  {
    p->vm[i].next = -1;
    p->vm[i].length = 0;
  }
  p->vm[0].next = 0;
  

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with kstack pages.

pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

    // map the trapframe beneath 2 pages of KSTACK, for uservec.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_NX | PTE_P | PTE_W | PTE_MAT | PTE_D) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }
  
  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x04, 0x00, 0x00, 0x1c, 0x84, 0x00, 0xc1, 0x28,
  0x05, 0x00, 0x00, 0x1c, 0xa5, 0x00, 0xc1, 0x28,
  0x0b, 0x1c, 0x80, 0x03, 0x00, 0x00, 0x2b, 0x00, 
  0x0b, 0x08, 0x80, 0x03, 0x00, 0x00, 0x2b, 0x00,
  0xff, 0xfb, 0xff, 0x57, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->era = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer 

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/"); 

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz+n>=MAXVA-PGSIZE)return -1;//trampoline
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  //  Copy shared memory
  shmaddcount(p->shmkeymask);
  np->shm = p->shm;
  np->shmkeymask = p->shmkeymask;

  for(i = 0;i < 8;++i)
  {
    if(shmkeyused(i,np->shmkeymask))
    {
      np->shmva[i] = p->shmva[i];
    }
  }

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  addmqcount(p->mqmask);
  np->mqmask = p->mqmask;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  if(p->parent==0 && p->pthread!=0)
    wakeup(p->pthread);
  else
    wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          shmrelease(np->pagetable,np->shm,np->shmkeymask);
          np->shm = TRAPFRAME - 64*2*PGSIZE;
          np->shmkeymask = 0;
          releasemq2(np->mqmask);
          np->mqmask = 0;
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct proc *temp;
  struct cpu *c = mycpu();
  int priority;
  int needed = 1;
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      if(needed)
      {
        priority = 19;
        for(temp = proc;temp < &proc[NPROC];temp++)
        {
          if(temp->state == RUNNABLE && temp->priority < priority)
            priority = temp->priority;
        }
      }
      needed = 0;
      if(p->state != RUNNABLE)
        continue;
      if(p->priority > priority)
        continue;

      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
      needed = 1;
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.

void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

void 
wakeup1p(void *chan) 
{
  struct proc *p;
 
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if(p != myproc()) {
      acquire(&p->lock);
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      release(&p->lock);
      break;
    }
    release(&p->lock);
    }
  }
   
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)  //todo
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)  //todo
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("time slice:%d t,pid=%d,state=%s,priority=%d %s", p->slot,p->pid, 
    state, p->priority,p->name);
    printf("\n");

    for (int i = p->vm[0].next; i != 0; i = p->vm[i].next)
    {
      printf("start: %d, length: %d \n",p->vm[i].address, p->vm[i].length);
    }
    printf("\n");
    
  }
}

uint64
chpri(int pid,int priority)
{
  struct proc *p;
  for(p = proc;p < &proc[NPROC];p++)
  {
    acquire(&p->lock);
    if(p->pid == pid)
    {
      p->priority = priority;
      release(&p->lock);
      break;
    }
    release(&p->lock);

  }
  return (uint64)pid;
}

//调用clone()前需要分配好线程栈的内存空间，并通过stack参数传入
int clone(void (*fcn)(void *), void *stack, void *arg) {

  struct proc *curproc = myproc();    //记录发出clone的进程
  struct proc *np;
  if ((np = allocproc()) == 0)        //为新线程分配PCB/TCB
    return -1;
 
   np->pagetable = curproc->pagetable;   //线程间共用同一个页表
 
   if((np->trapframe = (struct trapframe *)kalloc()) == 0){    //分配线程内核栈空间
     freeproc(np);
     release(&np->lock);
     return 0;
   }
 
   
   np->sz = curproc->sz;
   np->pthread = curproc;          // exit时用于找到父线程并唤醒
   np->ustack = stack;             // 设置自己的线程栈
   np->parent = 0;
   *(np->trapframe) = *(curproc->trapframe);   //继承trapframe
 
   // 设置trapframe映射
   if(mappages(np->pagetable, TRAPFRAME - PGSIZE, PGSIZE,
               (uint64)(np->trapframe), PTE_NX | PTE_P | PTE_W | PTE_MAT | PTE_D) < 0){  
    uvmfree(np->pagetable, 0);
    return 0;
  }
  // 设置栈指针
  // np->trapframe->sp = (e)(stack + 4096 -8);
  np->trapframe->sp = (uint64)(stack + 4096 -8);
  // 修改返回值a0
  np->trapframe->a0 = (uint64)arg;

  // 修改返回地址
  np->trapframe->era = (uint64)fcn;

  // 复制文件描述符
  for (int i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  int pid = np->pid;

  release(&np->lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  // 返回新线程的pid
  return pid;
}

int join() {
struct proc *curproc = myproc();
  struct proc *p;
  int havekids;
  for (;;) {
    havekids = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
      if (p->pthread != curproc)    // 判断是不是自己的子线程
        continue;
 
      havekids = 1;
      if (p->state == ZOMBIE) {
        acquire(&p->lock);
        if(p->trapframe)
          kfree((void*)p->trapframe);
        p->trapframe = 0;
        if(p->pagetable)
          uvmunmap(p->pagetable, TRAPFRAME - PGSIZE, 1, 0); // 释放内核栈
        p->pagetable = 0;
        p->sz = 0;
        p->pid = 0;
        p->parent = 0;
        p->pthread = 0;
        p->name[0] = 0;
        p->chan = 0;
        p->killed = 0;
        p->xstate = 0;
        p->state = UNUSED;

        int pid = p->pid;

        release(&p->lock);
        return pid;
      }
    }
    if (!havekids || curproc->killed) {
      return -1;
    }
    sleep(curproc, &wait_lock);
  }
  return 0;
}

uint64 
mygrowproc(int n){                 // 实现首次最佳适应算法
	struct proc *proc = myproc();
  struct vma *vm = proc->vm;     // 遍历寻找合适的空间
	uint64 start = proc->sz;          // 寻找合适的分配起点
	int index;
	int prev = 0;
	int i;

 	for(index = vm[0].next; index != 0; index = vm[index].next){
 		if(start + n < vm[index].address)
		break;
 		start = vm[index].address + vm[index].length;
 		prev = index;
 	}
 	
 	for(i = 1; i < 10; i++) {            // 寻找一块没有用的 vma 记录新的内存块
 		if(vm[i].next == -1){
 			vm[i].next = index;			
 			vm[i].address = start;
 			vm[i].length = n;
 
 			vm[prev].next = i;              //将vm[i]挂入链表尾部
 			
 			myallocuvm(proc->pagetable, start, start + n);    //为vm[i]分配内存
 			return start;   // 返回分配的地址
 		}
 	}
 	return 0;
}

int
myreduceproc(uint64 address){  // 释放 address 开头的内存块
 	int prev = 0;
 	int index;
  struct proc *proc = myproc();
 
 	for(index = proc->vm[0].next; index != 0; index = proc->vm[index].next) {
 		if(proc->vm[index].address == address && proc->vm[index].length > 0) {    //找到对应内存块
 			mydeallocuvm(proc->pagetable, proc->vm[index].address, proc->vm[index].address + proc->vm[index].length);		  //释放内存	
 			proc->vm[prev].next = proc->vm[index].next;     //从链上摘除
 			proc->vm[index].next = -1;        //标记为未用
 			proc->vm[index].length = 0;
 			break;
 		}
 		prev = index;
 	}
  return 0;
}

int
getcpuid(void)
{
	int id = r_tp();
	return id;
}
