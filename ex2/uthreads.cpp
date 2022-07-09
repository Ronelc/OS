#include <vector>
#include <map>
#include <deque>
#include <set>
#include <stack>
#include <iostream>
#include <algorithm>
#include "uthreads.h"
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

using namespace std;
typedef unsigned long address_t;

#define FAILURE -1
#define SUCCESS 0
#define READY 0
#define BLOCKED 1
#define RUNNING 2

static int curr_thread_size;
static int quantum;
static int total_quantum = 0;
static sigset_t sig_set;

struct Thread {
    int id;
    int state;
    char *stack;
    address_t pc;
    address_t sp;
    sigjmp_buf env[1];
    thread_entry_point entry_point;
    int total_q = 0; // num of times thread was in running state
    int init_q; // num of quantums when thread initial
    int sleep_quantums = 0; // num of quantum that thread has to sleep
};

static vector<int> id_set;
static deque<Thread *> READY_Q;
static map<int, Thread *> THREADS_MAP;

static int running_id;
struct sigaction sa = {0};
struct itimerval timer;
void timer_handler (int sig);

class Scheduler {

 public:

#ifdef __x86_64__
/* code for 64 bit Intel arch */

  typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.*/
  address_t translate_address (address_t addr) {
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
  }

#else
  /* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}


#endif

  /**
   * @brief Saves the current thread state, and jumps to the other thread.
   */
  void switch_threads (int new_location) {
    if (sigsetjmp(THREADS_MAP[running_id]->env[0], 1) == 0) {
      if (!READY_Q.empty ()) {
        int cur = running_id;
        running_id = READY_Q.front ()->id;
        THREADS_MAP[running_id]->state = RUNNING;
        READY_Q.pop_front ();
        THREADS_MAP[cur]->state = new_location;
        if (new_location == READY) {
          READY_Q.push_back (THREADS_MAP[cur]);
        }
        timer_init ();
        THREADS_MAP[running_id]->total_q += 1;
        siglongjmp(THREADS_MAP[running_id]->env[0], 1);
      }
      else {
        THREADS_MAP[running_id]->total_q += 1;
        timer_init ();
      }
    }
    return;
  }

  /**
   *  Install timer_handler as the signal handler for SIGVTALRM.
   */
  void timer_init () {
    timer.it_value.tv_sec = quantum / 1000000;;
    timer.it_value.tv_usec = quantum % 1000000;
    timer.it_interval.tv_sec = quantum / 1000000;
    timer.it_interval.tv_usec = quantum % 1000000;
    if (setitimer (ITIMER_VIRTUAL, &timer, NULL)) {
      std::cerr << "setitimer error." << endl;
    }
    total_quantum += 1;
  }
};

Scheduler s;
/**
 * Blocks all signals.
 * @return -1 is there was an error blocking the signals; 0 otherwise.
 */
int block_signals () {
  if (sigprocmask (SIG_BLOCK, &sig_set, NULL) < 0) {
    std::cerr << "Sigprocmask Error: Blocking failed" << endl;
    return FAILURE;
  }
  return SUCCESS;
}

/**
 * Unlocks all signals.
 * @return -1 is there was an error unblocking the signals; 0 otherwise.
 */
int unblock_signals () {
  if (sigprocmask (SIG_UNBLOCK, &sig_set, NULL) < 0) {
    std::cerr << "Sigprocmask Error: Unblocking failed" << endl;
    return FAILURE;
  }
  return SUCCESS;
}

/**
 * @brief initializes the thread library.
 *
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init (int quantum_usecs) {

  if (quantum_usecs <= 0) {
    cerr << "thread library error: quantum_usecs mast be positive number"
         << endl;
    return FAILURE;
  }
  quantum = quantum_usecs;

  //  create the first thread
  auto *first_thread = new Thread;
  first_thread->id = 0;
  first_thread->state = RUNNING;
  first_thread->total_q += 1;
  THREADS_MAP[first_thread->id] = first_thread;
  running_id = first_thread->id;
  curr_thread_size = 1;
  sa.sa_handler = &timer_handler;
  if (sigaction (SIGVTALRM, &sa, NULL) < 0) {
    std::cerr << "sigaction error." << endl;
  }
  s.timer_init ();
  THREADS_MAP[first_thread->id]->init_q = uthread_get_total_quantums ();
  //  initial id array
  for (int i = 1; i < MAX_THREAD_NUM; i++) {
    id_set.push_back (i);
  }
  return SUCCESS;
}

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn (thread_entry_point entry_point) {
  block_signals ();
  if (curr_thread_size + 1 > MAX_THREAD_NUM or entry_point == nullptr) {
    cerr
        << "thread library error: number of concurrent threads over the limit or func is null"
        << endl;
    unblock_signals ();
    return FAILURE;
  }
  auto *new_thread = new Thread;
  new_thread->id = *id_set.cbegin ();
  remove (id_set.begin (), id_set.end (), new_thread->id);
  new_thread->state = READY;
  new_thread->stack = new char[STACK_SIZE];
  if (new_thread->stack == nullptr) {
    unblock_signals ();
    return FAILURE;
  }
  new_thread->init_q = uthread_get_total_quantums ();
  new_thread->entry_point = entry_point;
  new_thread->sp =
      (address_t) (new_thread->stack) + STACK_SIZE - sizeof (address_t);
  new_thread->pc = (address_t) entry_point;
  if (sigsetjmp(new_thread->env[0], 1) != 0)return FAILURE;
  (new_thread->env[0]->__jmpbuf)[JB_SP] = s.translate_address (new_thread->sp);
  (new_thread->env[0]->__jmpbuf)[JB_PC] = s.translate_address (new_thread->pc);
  sigemptyset (&new_thread->env[0]->__saved_mask);
  THREADS_MAP[new_thread->id] = new_thread;
  READY_Q.push_back (new_thread);
  curr_thread_size += 1;
  unblock_signals ();
  return new_thread->id;
}

/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate (int tid) {
  block_signals ();
  if (tid == 0) {
    for (auto &it : THREADS_MAP) {
      delete[] it.second->stack;
      exit (0);
    }
  }
  if (THREADS_MAP.find (tid) == THREADS_MAP.end ()) {
    cerr << "thread library error: thread id invalid" << endl;
    unblock_signals ();
    return FAILURE;
  }
  if (tid == running_id) {
    if (sigsetjmp(THREADS_MAP[tid]->env[0], 1) == 0) {
      running_id = READY_Q.front ()->id;
      THREADS_MAP[running_id]->state = RUNNING;
      READY_Q.pop_front ();
      THREADS_MAP[running_id]->total_q += 1;
      siglongjmp(THREADS_MAP[running_id]->env[0], 1);
    }
  }
  READY_Q.erase (std::remove (READY_Q.begin (), READY_Q.end (), THREADS_MAP[tid]), READY_Q.end ());
  THREADS_MAP.erase (tid);
  id_set.push_back (tid);
  sort (id_set.begin (), id_set.end ());
  curr_thread_size -= 1;
  unblock_signals ();
  return SUCCESS;
}

/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block (int tid) {
  block_signals ();
  if (THREADS_MAP.find (tid) == THREADS_MAP.end () or (tid == 0)) {
    cerr << "thread library error: thread id invalid" << endl;
    unblock_signals ();
    return FAILURE;
  }
  if (THREADS_MAP[tid]->state == BLOCKED) {
    unblock_signals ();
    return SUCCESS;
  }
  else if (THREADS_MAP[tid]->state == READY) {
    THREADS_MAP[tid]->state = BLOCKED;
    READY_Q.erase (std::remove (READY_Q.begin (), READY_Q.end (), THREADS_MAP[tid]), READY_Q.end ());
  }
  else {
    s.switch_threads (BLOCKED);
  }
  unblock_signals ();
  return SUCCESS;
}

/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume (int tid) {
  block_signals ();
  if (THREADS_MAP.find (tid) == THREADS_MAP.end ()) {
    cerr << "thread library error: thread id invalid" << endl;
    unblock_signals ();
    return FAILURE;
  }
  if (THREADS_MAP[tid]->state == READY or running_id == tid) {
    unblock_signals ();
    return SUCCESS;
  }
  THREADS_MAP[tid]->state = READY;
  READY_Q.push_back (THREADS_MAP[tid]);
  unblock_signals ();
  return SUCCESS;
}

/**
 * @brief Blocks the RUNNING thread for sleep_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY threads list.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid==0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep (int num_quantums) {
  block_signals ();
  if (THREADS_MAP[running_id]->id == 0) {
    cerr << "thread library error: thread id invalid" << endl;
    unblock_signals ();
    return FAILURE;
  }
  THREADS_MAP[running_id]->sleep_quantums = num_quantums;
  s.switch_threads (BLOCKED);
  unblock_signals ();
  return SUCCESS;
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid () {
  return running_id;
}

/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums () {
  return total_quantum;
}

/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums (int tid) {
  if (THREADS_MAP.find (tid) == THREADS_MAP.end ()) {
    cerr << "thread library error: thread id invalid" << endl;
    return FAILURE;
  }
  return THREADS_MAP[tid]->total_q;

}

/**
 * handle with timer signal and check if some threads need to resume.
 */
void timer_handler (int sig) {
  if (sig == SIGVTALRM) {
    for (auto &it : THREADS_MAP) {
      if (it.second->state == BLOCKED) {
        if (uthread_get_total_quantums ()
            == it.second->init_q - 1 + it.second->sleep_quantums) {
          uthread_resume (it.first);
        }
      }
    }
    s.switch_threads (READY);
  }
}




