// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum runnable_state { 
  UNUSED,     // Process / Thread     : 해당 객체가 사용 가능한 상태
  EMBRYO,     // Process / Thread     : 해당 객체를 사용하기 위해 초기화중인 상태
  SLEEPING,   // Thread Only          : sleep 중인 상태
  RUNNABLE,   // Process / Thread     : 초기화가 완료되어 사용중인 상태
  RUNNING,    // Thread Only          : 스레드가 실행중인 상태
  ZOMBIE      // Process / Thread     : 실행을 종료하고 다른 객체에 의해 정리 되기를 기다리는 상태
};

// Per-process state
struct proc {
  uint sz;                       // Shared: Size of process memory (bytes)
  pde_t* pgdir;                  // Shared: Page table
  enum runnable_state state;          // Shared: Process state
  int pid;                       // Shared: Process ID
  struct proc *parent;           // Shared: Parent process
  struct thread *main_thread;    // Shared: first thread of this process
  struct thread *running_thread; // Shared: Current Running Thread
  struct thread *exiting_thread; // Shared: exit() thread
  int killed;                    // Shared: If non-zero, have been killed
  int exiting;                   // Shared: If non-zero, process is exiting.
  struct file *ofile[NOFILE];    // Shared: Open files
  struct inode *cwd;             // Shared: Current directory
  char name[16];                 // Process name (debugging)
  int thread_count;              // Shread: RUNNABLE thread count of this process
};

struct thread {
  char *kstack;                // Private: Bottom of kernel stack for this process
  enum runnable_state state;      // Private: Thread State
  struct proc *process;        // Shared : Process
  int tid;                     // Private: Thread ID
  struct trapframe *tf;        // Private: Trap frame for current syscall
  struct context *context;     // Private: swtch() here to run process
  void *chan;                  // Private: If non-zero, sleeping on chan
  void *retval;                // Private: Thread return value
  int will_joined;              // Private: If non-zero, there is a thread waiting this thread for join
};


// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
