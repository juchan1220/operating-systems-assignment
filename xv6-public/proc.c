#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct thread thread_pool[NTHREAD];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

int thread_join_found(struct thread*, void**, int);

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


/**
 * @brief thread pool에서 사용할 수 있는 thread를 찾아
 *  커널에서 실행할 수 있는 상태로 초기화한 thread를 반환합니다.
 *  
 * @return 성공한 경우 struct thread* 를, 아닌 경우 0을 반환합니다.
 * @warning caller는 반드시 ptable.lock을 holding 하고 있어야 합니다.
 */
static struct thread* alloc_thread (void) {
  struct thread* t;
  char *sp;

  for(t = ptable.thread_pool; t < &ptable.thread_pool[NTHREAD]; t++)
    if(t->state == UNUSED)
      goto found;

  return 0;

found:
  t->state = EMBRYO;
  t->tid = nexttid++;

  // tid는 0을 사용할 수 없음
  if (t->tid == 0) {
    t->tid = nexttid++;
  }

  // Allocate kernel stack.
  if((t->kstack = kalloc()) == 0){
    t->state = UNUSED;
    return 0;
  }
  sp = t->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *t->tf;
  t->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *t->context;
  t->context = (struct context*)sp;
  memset(t->context, 0, sizeof *t->context);
  t->context->eip = (uint)forkret;

  return t;
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

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  if ((p->main_thread = alloc_thread()) == 0) {
    p->state = UNUSED;
    release(&ptable.lock);
    return 0;
  }

  p->thread_count = 1;
  p->main_thread->process = p;

  release(&ptable.lock);

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
  memset(p->main_thread->tf, 0, sizeof(*p->main_thread->tf));
  p->main_thread->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->main_thread->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->main_thread->tf->es = p->main_thread->tf->ds;
  p->main_thread->tf->ss = p->main_thread->tf->ds;
  p->main_thread->tf->eflags = FL_IF;
  p->main_thread->tf->esp = PGSIZE;
  p->main_thread->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  p->main_thread->state = RUNNABLE;

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
    kfree(np->main_thread->kstack);
    np->main_thread->kstack = 0;
    np->main_thread->state = UNUSED;
    np->state = UNUSED;
    return -1;
  }

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->main_thread->tf = *curproc->running_thread->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->main_thread->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  np->main_thread->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}


void join_all_other_threads() {
  struct proc *curproc = myproc();
  struct thread *cur_thread;
  struct thread *t;

  acquire(&ptable.lock);

  cur_thread = curproc->running_thread;

  // 이미 다른 thread가 exit를 호출한 경우
  if (curproc->exiting_thread != 0) {
    // 먼저 exit를 호출한 스레드가 exit를 처리하도록 하고
    // 이 스레드는 trap에서 thread_exit 될 예정
    release(&ptable.lock);
    return ;
  }

  curproc->killed = 1;
  curproc->exiting_thread = cur_thread;

  // 상호 대기를 방지하기 위해, exit 담당 스레드를 기다리는 스레드를 모두 꺠움
  wakeup1(cur_thread);

  // 일반 wait와는 다르게 exiting 도중에도
  // 다른 스레드가 thread_create 시스템 콜을 처리하고 있는 중이어서
  // 새롭게 스레드가 생겨날 수 있음

  // 나를 제외한 모든 스레드 join 처리
  while (curproc->thread_count > 1) {
    for (t = ptable.thread_pool; t < &ptable.thread_pool[NTHREAD]; t++) {
      if (t != cur_thread && t->process == curproc) {
        thread_join_found(t, 0, 0);
      }
    }
  }

  release(&ptable.lock);
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
  
  join_all_other_threads();

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

        // 해당 프로세스의 마지막 스레드 정리
        kfree(p->running_thread->kstack);
        p->running_thread->kstack = 0;
        p->running_thread->tid = 0;
        p->running_thread->process = 0;
        p->running_thread->state = UNUSED;
        p->running_thread->will_joined = 0;
        p->thread_count--;

        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->main_thread = 0;
        p->exiting_thread = 0;
        p->running_thread = 0;
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
  struct thread *t;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for (t = ptable.thread_pool; t < &ptable.thread_pool[NTHREAD]; t++) {
      if (t->state != RUNNABLE) {
        continue;
      }
      
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      t->process->running_thread = t;
      c->proc = t->process;
      switchuvm(t->process);
      t->state = RUNNING;

      swtch(&(c->scheduler), t->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
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
  swtch(&p->running_thread->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->running_thread->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

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
void
sleep(void *chan, struct spinlock *lk)
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
  p->running_thread->chan = chan;
  p->running_thread->state = SLEEPING;

  sched();

  // Tidy up.
  p->running_thread->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct thread* t;

  for(t = ptable.thread_pool; t < &ptable.thread_pool[NTHREAD]; t++)
    if(t->state == SLEEPING && t->chan == chan)
      t->state = RUNNABLE;
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
  struct thread *t;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      for (t = ptable.thread_pool; t < &ptable.thread_pool[NTHREAD]; t++) {
        if (t->process == p && t->state == SLEEPING) {
          t->state = RUNNABLE;
        }
      }

      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

/**
 * @brief 현재 프로세스에서 start_routine이 가리키는 함수를 실행하는 새로운 스레드를 추가합니다.
 * 
 * @param thread_id_ptr thread id를 저장할 uva
 * @param start_routine 실행할 함수를 가리키는 uva
 * @param arg void* 타입의 인자
 * @return int 성공한 경우 0, 아닌 경우 -1을 반환합니다
 */
int thread_create (int* thread_id_ptr, thread_routine* start_routine, void* arg) {
  struct proc *curproc = myproc();
  struct thread *nt;

  acquire(&ptable.lock);

  // 스레드 할당
  if ((nt = alloc_thread()) == 0) {
    return 0;
  }

  // 유저 스택 할당
  uint old_sz = curproc->sz;
  uint new_sz = curproc->sz + 2*PGSIZE;

  if ((curproc->sz = allocuvm(curproc->pgdir, old_sz, new_sz)) == 0) {
    curproc->sz = old_sz;
    goto bad;
  }

  clearpteu(curproc->pgdir, (char*)(curproc->sz - 2*PGSIZE));

  uint sp = curproc->sz;
  uint ustack[2] = { 0xffffffff, (uint)arg };

  sp -= 2 * 4;
  if (copyout(curproc->pgdir, sp, ustack, 2 * 4) < 0) {
    if ((curproc->sz = deallocuvm(curproc->pgdir, new_sz, old_sz)) == 0) {
      panic("thread_create: dealloc user stack failed.");
    }
    goto bad;
  }

  // trapframe 복사
  *nt->tf = *curproc->running_thread->tf;
  nt->tf->eip = (uint)start_routine;
  nt->tf->esp = sp;
  
  // 스레드와 프로세스 연결
  curproc->thread_count++;
  nt->process = curproc;
  nt->state = RUNNABLE;
  switchuvm(curproc);

  *thread_id_ptr = nt->tid;

  release(&ptable.lock);
  return 0;

bad:
  kfree(nt->kstack);
  nt->kstack = 0;
  nt->state = UNUSED;
  release(&ptable.lock);
  return -1;
}

/**
 * @brief 현재 스레드의 실행을 종료합니다.
 *  현재 스레드가 프로세스의 마지막 스레드인 경우, 프로세스도 종료합니다.
 *  종료된 스레드는 ZOMBIE 상태로 남아서
 *  다른 스레드의 join (마지막 스레드인 경우, 부모 프로세스의 wait) 에 의해 정리되기를 기다립니다.
 * 
 * @param retval 이 스레드의 반환 값을 retval로 설정합니다.
 */
void thread_exit (void* retval) {
  struct proc* curproc = myproc();
  struct thread* cur_thread;

  acquire(&ptable.lock);

  cur_thread = curproc->running_thread;

  cur_thread->retval = retval;
  cur_thread->state = ZOMBIE;

  // 이 프로세스가 마지막 스레드인 경우 exit() 처리가 필요함
  if (curproc->thread_count == 1) {
    // 다른 스레드가 없기 때문에 interrupt 되어도 이 프로세스에 새로운 스레드가 생성될 수 없음
    // 따라서 release 를 하고 exit() 를 호출해도 안전함
    release(&ptable.lock);
    exit();
    // exit never return
  }
  
  // 이 스레드를 join 하기 위해 기다리고 있는 스레드를 wakeup
  wakeup1(cur_thread);

  curproc->thread_count--;

  sched();
  panic("zombie thread return");
}

/**
 * @brief 지정한 스레드의 실행이 종료될 때까지 sleep 합니다.
 *  지정한 스레드가 이미 종료된 경우, 바로 반환합니다.
 * 
 * @param thread_id join 대상 스레드의 id
 * @param retval_ptr retval을 저장할 uva
 * @return int 성공적으로 join한 경우 0, 그 외의 경우 (id에 해당하는 thread가 없는 경우) -1을 반환합니다.
 */
int thread_join (int thread_id, void** retval_ptr) {
  struct proc* curproc = myproc();
  struct thread* t;
  int ret = -1;

  acquire(&ptable.lock);

  for (t = ptable.thread_pool; t < &ptable.thread_pool[NTHREAD]; t++) {
    if (t->tid == thread_id && t->process == curproc) {
      ret = thread_join_found(t, retval_ptr, 1);
      break;
    }
  }

  release(&ptable.lock);
  return ret;
}

/**
 * @brief t 스레드가 종료될 때까지 sleep하고
 *  종료된 경우 t 스레드의 자원을 정리하고 retval을 설정합니다
 * 
 * @param t join 대상 스레드
 * @param retval_ptr retval을 설정할 uva
 * @param set_retval non-zero 인 경우 reval_ptr에 retval을 설정하고, 0인 경우 설정하지 않습니다.
 * @warning caller는 반드시 ptable.lock을 들고 있어야 합니다
 */
int thread_join_found (struct thread* t, void** retval_ptr, int set_retval) {
  struct proc* curproc = myproc();

  // 자기 자신에게 join을 시도하는 경우
  if (curproc->running_thread == t) {
    return -1;
  }

  // 이미 대상 스레드를 join 하기 위해 대기중인 스레드가 있는 경우
  if (t->will_joined != 0) {
    return -1;
  }
  
  // exit() 담당 스레드에게 join을 시도하는 경우
  if (curproc->exiting_thread == t) {
    return -1;
  }

  t->will_joined = 1;

  // sleep 중에도 kill에 의해 강제로 wakeup 될 수도 있으므로 무한 반복으로 체크
  while (t->state != ZOMBIE) {
    sleep(t, &ptable.lock);

    // exit() 담당 스레드에게 join을 시도하는 경우
    if (curproc->exiting_thread == t) {
      t->will_joined = 0;
      return -1;
    }
  }

  // retval 설정
  if (set_retval != 0) {
    *retval_ptr =  t->retval;
  }

  // 스레드 관련 자원 초기화
  kfree(t->kstack);
  t->tid = 0;
  t->process = 0;
  t->will_joined = 0;
  t->state = UNUSED;

  // TODO: 시간이 남는다면 free된 유저 스택을 linked list로 관리

  return 0;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  // TODO: 필요하다면 프로세스 정보와 스레드 정보를 분리해서 출력
 /*
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
*/
}
