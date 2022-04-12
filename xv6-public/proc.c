#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// Custom Debug Util Header
#include "jc-utils.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

#ifdef SCHED_POLICY_MLFQ
struct proc_priority_queue proc_queue[MLFQ_K];

// priority_queue 관련 모든 함수는 ptable.lock 을 지닌 채로 호출해야함.

// push, priority가 상승했을 경우를 처리
void upheap (struct proc_priority_queue* queue, int idx) {
  struct proc* tmp;
  struct proc** heap = queue->heap;

  while (idx > 1) {
    if (heap[idx]->priority <= heap[idx >> 1]->priority) {
      break;
    }

    // swap
    tmp = heap[idx];
    heap[idx] = heap[idx >> 1];
    heap[idx >> 1] = tmp;

    // change idx
    heap[idx]->idx_on_queue = idx;
    heap[idx >> 1]->idx_on_queue = idx >> 1;

    idx >>= 1;
  }
}

// pop, priority가 하락했을 경우를 처리
void downheap (struct proc_priority_queue* queue, int idx) {
  struct proc* tmp;
  struct proc** heap = queue->heap;
  int target;

  while ((idx << 1) <= queue->size) {
    target = idx << 1;
    if (target + 1 <= queue->size && heap[target]->priority < heap[target + 1]->priority) {
      target++;
    }

    if (heap[idx]->priority >= heap[target]->priority) {
      break;
    }

    tmp = heap[idx];
    heap[idx] = heap[target];
    heap[target] = tmp;

    heap[idx]->idx_on_queue = idx;
    heap[target]->idx_on_queue = target;

    idx = target;
  }
}

int push_queue (struct proc* p) {
  int lv = p->queue_level;
  if (lv < 0 || MLFQ_K <= lv) {
    panic("push on invalid level");
  }

  if (p->state != RUNNABLE) {
    panic("push non-runnable process");
  }

  if (p->idx_on_queue != -1) {
    panic("push already in-queue process");
  }

  if (proc_queue[lv].size == NPROC) {
    panic("push at full queue");
  }

  struct proc_priority_queue* queue = &proc_queue[lv];
  int idx = ++queue->size;
  
  queue->heap[idx] = p;
  p->idx_on_queue = idx;

  upheap(queue, idx);

  return 0;
}

int pop_queue (struct proc* p) {
  log_v("pop_queue (pid: %d, lv: %d, idx: %d)\n", p->pid, p->queue_level, p->idx_on_queue);

  if (p->queue_level < 0 || MLFQ_K <= p->queue_level) {
    panic("pop on invalid level");
  }

  if (p->idx_on_queue == -1) {
    panic("pop not in-queue process");
  }

  if (p->idx_on_queue > proc_queue[p->queue_level].size) {
    panic("pop index is out of range");
  }

  if (proc_queue[p->queue_level].size == 0 && p->idx_on_queue != 0) {
    panic("pop on empty queue");
  }

  int idx = p->idx_on_queue;
  struct proc_priority_queue* queue = &proc_queue[p->queue_level];

  if (idx == 0) {
    p->idx_on_queue = -1;
    queue->heap[idx] = 0;
  } else if (idx == queue->size) {
    p->idx_on_queue = -1;
    queue->size--;
  } else {
    p->idx_on_queue = -1;
    queue->size--;

    queue->heap[idx] = queue->heap[queue->size];
    queue->heap[idx]->idx_on_queue = idx;

    if (p->priority < queue->heap[idx]->priority) {
      upheap(queue, idx);
    } else {
      downheap(queue, idx);
    }
  }

  return 0;
}

// queue에서 가장 높은 priority를 지닌 process를 0으로 끌어옴
void extract_queue (struct proc_priority_queue* queue) {
  if (queue->size == 0) {
    return ;
  }

  queue->heap[0] = queue->heap[1];
  queue->heap[0]->idx_on_queue = 0;


  queue->heap[1] = queue->heap[queue->size--];
  if (queue->size > 1) {
    downheap(queue, 1);
  }
  
  return ;
}

void change_priority (struct proc* p, int priority) {
  int old_priority = p->priority;
  
  p->priority = priority;

  if (old_priority == priority || p->idx_on_queue == -1) {
    return ;
  }

  if (old_priority > priority) {
    downheap(&proc_queue[p->queue_level], p->idx_on_queue);
  } else {
    upheap(&proc_queue[p->queue_level], p->idx_on_queue);
  }
}

void boost_priority (void) {
  log_d("priority boosting!\n");
  
  // heap 초기화
  for (int i = 0; i < MLFQ_K; i++) {
    proc_queue[i].heap[0] = 0;
    proc_queue[i].size = 0;
  }

  struct proc_priority_queue* queue = &proc_queue[0];

  // 프로세스 레벨 및 Time Quantum 초기화 + 레벨 0 heap 채우기
  for (int i = 0; i < NPROC; i++) {
    ptable.proc[i].queue_level = 0;
    ptable.proc[i].remain_time_quantum = 2;

    if (ptable.proc[i].state == RUNNABLE) {
      queue->size++;
      queue->heap[queue->size] = &ptable.proc[i];
      ptable.proc[i].idx_on_queue = queue->size;
    } else {
      ptable.proc[i].idx_on_queue = -1;
    }
  }

  // build heap
  for (int i = queue->size >> 1; i > 0; i--) {
    downheap(queue, i);
  }
}

struct proc* getRunnable (struct proc_priority_queue* queue) {
  if (!queue->heap[0]) {
    extract_queue(queue);
  }

  return queue->heap[0];
}

#endif

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

#ifdef SCHED_POLICY_MLFQ
  p->priority = 0;
  p->queue_level = 0;
  p->idx_on_queue = -1;
  p->need_reset_lv_tq = 0;
#endif

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
#ifdef SCHED_POLICY_MLFQ
  p->remain_time_quantum = 2;
  push_queue(p);
#endif

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
#ifdef SCHED_POLICY_MLFQ
  np->remain_time_quantum = 2;
  push_queue(np);
#endif

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  log_d("exit (pid: %d)\n", curproc->pid);

  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

#ifdef SCHED_POLICY_MULTILEVEL
  int proc_idx = 0;
  int lookup_count;
  struct proc *fcfs_target = 0;
#elif SCHED_POLICY_MLFQ
  uint last_boosting_tick = ticks;
#endif

  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

#ifdef SCHED_POLICY_MULTILEVEL
    log_v("Multilevel Loop (re) starting...\n");
    for (lookup_count = 0; lookup_count < NPROC; proc_idx = (proc_idx + 1) % NPROC){
      p = &ptable.proc[proc_idx];
      log_v("Lookup proc_idx = %d... ", proc_idx);

      if (p->state != RUNNABLE) {
        log_v("- Not Runnable. Skipped.\n");
        goto skip;
      }

      log_v("- pid %d, ", p->pid);

      // RUNNABLE && odd pid
      if (p->pid % 2 == 1) {
        if (!fcfs_target || fcfs_target->pid > p->pid) {
          log_v("fcfs target changed. (old: %d)\n", fcfs_target ? fcfs_target->pid : -1);
          fcfs_target = p;
        } else {
          log_v("fcfs target not changed. (old: %d)\n", fcfs_target->pid);
        }
        goto skip;
      }

      log_v("even pid found.\n");
      // RUNNABLE && even pid
      goto found_proc;


      skip: // 모든 continue를 이곳에서 실행
      lookup_count++;
      // ptable을 한바퀴 돌았는데 odd pid 프로세스만 발견한 경우
      if (lookup_count == NPROC) {
        log_v("There is no Runnable RR.");
        if (fcfs_target) {
          log_v(" FCFS Selected.\n");
          p = fcfs_target;
          goto found_proc;
        }

        log_v("Breaking loop...\n");
      }

      continue;

      found_proc: // 프로세스 스위치
      log_v("Process Switching... (pid: %d)\n", p->pid);
      lookup_count = 0;
      fcfs_target = 0;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }

#else // give me #elifdef from C++23!
#ifdef SCHED_POLICY_MLFQ
    // log_d("(re)starting scheduler level loop... (tick: %d)\n", ticks);
    
    // 더 이상 Runnable 한 프로세스가 없어서 
    // 루프 밖에서 priority_boosting을 해줘야 할 때
    // priority_boosting
    if (last_boosting_tick + 100U <= ticks) {
      last_boosting_tick = ticks;
      boost_priority();
    }
    
    for (int lv = 0; lv < MLFQ_K; lv++) {
      struct proc_priority_queue *queue = &proc_queue[lv];

      p = getRunnable(queue);
      if (!p) { continue; }

      log_d("schedule pid: %d, level: %d = %d\n", p->pid, p->queue_level, lv);
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.

      // Use Time Quantum
      p->remain_time_quantum--;

      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      
      // 다시 level 0부터 프로세스 탐색
      lv = -1;

      // priority_boosting
      if (last_boosting_tick + 100U <= ticks) {
        last_boosting_tick = ticks;
        boost_priority();
        continue; 
      }

      // priority boosting에서 RUNNABLE 한 프로세스만 queue에 포함하므로
      // priotiry boosting을 하지 않는 경우에만 pop을 처리해도 된다

      // 마지막으로 돌린 process가 더 이상 RUNNABLE 하지 않거나
      // 남은 time quantum이 0인 경우
      // queue level, time quantum을 reset 해야하는 경우
      // queue에서 제거
      if (p->state == SLEEPING || p->state == ZOMBIE || p->remain_time_quantum == 0 || p->need_reset_lv_tq) {
        pop_queue(p);
      }

      // sys_sleep, sys_yield를 호출하여 queue level, time quantum을 리셋 해야하는 경우
      if (p->need_reset_lv_tq) {
        log_d("reset lv, tq tid: %d\n", p->pid);
        p->need_reset_lv_tq = 0;
        p->queue_level = 0;
        p->remain_time_quantum = 2;
        if (p->state == RUNNABLE) { push_queue(p); }
      }

      // SLEEPING으로 인해 queue에서 제거되었더라도
      // level, TQ 업데이트를 처리해야
      // wakeup할 때 남은 TQ를 사용할 수 있다
      if (p->remain_time_quantum == 0) {
          p->queue_level++;
          p->remain_time_quantum = (p->queue_level) * 4 + 2;
        if (p->queue_level < MLFQ_K && p->state == RUNNABLE) { push_queue(p); }
      }
    }
#else
  // XV6 Default Round-Robin Scheduler

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
#endif
#endif
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}


// Give up the CPU for one scheduling round.
static void __wrapped_yield__ (int by_self) 
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
#ifdef SCHED_POLICY_MLFQ
  if (by_self) { myproc()->need_reset_lv_tq = 1; }
#endif
  sched();
  release(&ptable.lock);
}

void yield(void) {
  __wrapped_yield__(0);
}

#ifdef SCHED_POLICY_MLFQ

void yield_by_self(void) {
  __wrapped_yield__(1);
}

#endif



// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
static void __wrapped_sleep__(void *chan, struct spinlock *lk, int by_self)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

#ifdef SCHED_POLICY_MLFQ
  if (by_self) { p->need_reset_lv_tq = 1; }
#endif

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

void sleep(void *chan, struct spinlock *lk) {
  __wrapped_sleep__(chan, lk, 0);
}

#ifdef SCHED_POLICY_MLFQ

void sleep_by_self(void *chan, struct spinlock *lk) {
  __wrapped_sleep__(chan, lk, 1);
}

#endif

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
#ifdef SCHED_POLICY_MLFQ
      // 우연히 마지막 레벨, 마지막 TQ에서 Sleep을 한 경우
      // queue_level이 MLFQ_K이고, 이 경우는 queue에 넣으면 안됨
      if (p->queue_level < MLFQ_K) {
        push_queue(p);
      }
#endif      
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
#ifdef SCHED_POLICY_MLFQ
        // 우연히 마지막 레벨, 마지막 TQ에서 Sleep을 한 경우
        // queue_level이 MLFQ_K이고, 이 경우는 queue에 넣으면 안됨
        if (p->queue_level < MLFQ_K) {
          push_queue(p);
        }
#endif
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

#ifdef SCHED_POLICY_MLFQ
int setpriority(int pid, int priority) {
  struct proc *p;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->pid == pid) {
      if (p->parent != curproc) {
        break;
      }

      change_priority(p, priority);

      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
#endif

