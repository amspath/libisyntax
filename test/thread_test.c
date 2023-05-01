#include "common.h"

#if WINDOWS
// TODO(avirodov): enable this test after we figured out cross-platform threading.
//   More discussion in https://github.com/amspath/libisyntax/issues/16
int main(void) {
    printf("Test disabled on windows.");
    return 0;
}
#else

#include <threads.h>
#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <time.h>

#include "libisyntax.h"

typedef struct test_thread_t {
  thrd_t thread_id;
  atomic_int* atomic_sync_flag; // Spinlock to sync threads.
  bool force_sync;

  // TODO(avirodov): implement if needed. Or we can keep making sure that test output comes after test work.
  // char* output_buffer; // Avoid console output due to it may be implemented with a mutex or other sync.
  // int output_buffer_len;
  // int output_buffer_capacity;

  void* arg;
  int (*func)(void*);
  int result;
} test_thread_t;

int test_print(void* arg) {
  clock_t current_clock = clock();
  printf("test_print tid=%ld currect_clock=%ld\n", thrd_current(), current_clock);
  return 0;
}

int parallel_sync_and_call(void* arg) {
  test_thread_t* test_thread = (test_thread_t*)arg;
  // Wait in spinlock.
  while (test_thread->force_sync && atomic_load(&test_thread->atomic_sync_flag) == 0) {
    // noop.
  }

  // Run test code.
  return test_thread->func(test_thread->arg);
}

void parallel_run(int (*func)(void*), void* arg, bool force_sync) {
#define n_threads 10
  const int n_iterations = 2;

  for (int iter = 0; iter < n_iterations; ++iter) {
    printf("== parallel run iter %d ==\n", iter);
    atomic_int atomic_sync_flag = 0;
    test_thread_t threads[n_threads] = {0};

    // Spawn the threads.
    for (int thread_i = 0; thread_i < n_threads; ++thread_i) {

      threads[thread_i].atomic_sync_flag = &atomic_sync_flag;
      threads[thread_i].func = func;
      threads[thread_i].arg = arg;
      threads[thread_i].force_sync = force_sync;

      int result = thrd_create(&threads[thread_i].thread_id, parallel_sync_and_call, &threads[thread_i]);
      assert(result == thrd_success);
    }

    // Sync and start the threads.
    const int milliseconds_to_nanoseconds = 1000 * 1000;
    thrd_sleep(&(struct timespec){.tv_nsec=10 * milliseconds_to_nanoseconds}, NULL);
    atomic_store(&atomic_sync_flag, 1);

    // Join the threads.
    for (int thread_i = 0; thread_i < n_threads; ++thread_i) {
      thrd_join(threads[thread_i].thread_id, &threads[thread_i].result);
    }
  }
}

extern atomic_int dbgctr_init_thread_pool_counter;
extern atomic_int dbgctr_init_global_mutexes_created;

int test_libisyntax_init(void* arg) {
  clock_t current_clock = clock();
  isyntax_error_t result = libisyntax_init();
  printf("test_print tid=%ld currect_clock=%ld result=%d init_counter=%d mutexes_created_counter=%d\n",
         thrd_current(), current_clock, result,
         atomic_load(&dbgctr_init_thread_pool_counter),
         atomic_load(&dbgctr_init_global_mutexes_created));
  return (int)result;
}

int main() {
  parallel_run(test_print, NULL, /*force_sync=*/true);
  parallel_run(test_libisyntax_init, NULL, /*force_sync=*/true);
  return 0;
}

#endif