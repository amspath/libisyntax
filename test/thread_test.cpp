#include <thread>
#include <atomic>
#include <functional>
#include <iostream>
#include <sstream>

#include <assert.h>
#include <time.h>

#include "libisyntax.h"

#include "common.h"
#if WINDOWS
#include <windows.h>
#else
#include <semaphore.h>
#include <unistd.h>
#endif

#include "intrinsics.h"


typedef struct test_thread_t {
    using test_func_t = std::function<int(test_thread_t *)>;

    std::thread thread;
    std::atomic<int> *atomic_sync_flag; // Spinlock to sync threads.
    bool force_sync = false;

    std::stringstream output;

    test_func_t func;
    int result = 0;
} test_thread_t;

auto time_now() {
    auto now = std::chrono::high_resolution_clock::now();
    auto now_ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);
    return now_ns.time_since_epoch().count();
}


int test_print(test_thread_t *test_thread) {
    auto current_clock = time_now();

    test_thread->output
            << "test_print tid=" << std::this_thread::get_id()
            << " current_clock=" << current_clock
            << std::endl;
    return 0;
}

int parallel_sync_and_call(test_thread_t *test_thread) {
    // Wait in spinlock.
    while (test_thread->force_sync && test_thread->atomic_sync_flag->load() == 0) {
        // noop.
    }

    // Run test code.
    return test_thread->func(test_thread);
}

void parallel_run(test_thread_t::test_func_t func, bool force_sync) {
#define n_threads 10
    const int n_iterations = 2;

    for (int iter = 0; iter < n_iterations; ++iter) {
        printf("== parallel run iter %d ==\n", iter);
        std::atomic<int> atomic_sync_flag = 0;
        test_thread_t threads[n_threads];

        // Spawn the threads.
        for (int thread_i = 0; thread_i < n_threads; ++thread_i) {

            threads[thread_i].atomic_sync_flag = &atomic_sync_flag;
            threads[thread_i].func = func;
            threads[thread_i].force_sync = force_sync;
            threads[thread_i].thread = std::thread(std::bind(parallel_sync_and_call, &threads[thread_i]));
        }

        // Sync and start the threads.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        atomic_sync_flag.store(1);

        // Join the threads.
        for (int thread_i = 0; thread_i < n_threads; ++thread_i) {
            threads[thread_i].thread.join();
        }

        // Sequentially print the output.
        for (int thread_i = 0; thread_i < n_threads; ++thread_i) {
            std::cout << threads[thread_i].output.str();
        }
    }
}


extern "C" int32_t volatile dbgctr_init_thread_pool_counter;
extern "C" int32_t volatile dbgctr_init_global_mutexes_created;

int test_libisyntax_init(test_thread_t *test_thread) {
    auto current_clock = time_now();
    isyntax_error_t result = libisyntax_init();
    read_barrier;
    test_thread->output
            << "test_print tid=" << std::this_thread::get_id()
            << " current_clock=" << current_clock
            << " result=" << result
            << " init_counter=" << dbgctr_init_thread_pool_counter
            << " mutexes_created_counter=" << dbgctr_init_global_mutexes_created
            << std::endl;
    return (int) result;
}

int main() {
    parallel_run(test_print, /*force_sync=*/true);
    parallel_run(test_libisyntax_init, /*force_sync=*/true);
    return 0;
}
