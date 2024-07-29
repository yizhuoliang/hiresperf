#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include "hrperf_control.h"

// Macros to configure the CPU ID
#define CORE_ID 2

void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np");
        exit(EXIT_FAILURE);
    }
}

void* cpu_intensive_task(void* arg) {
    hrperf_start()
    unsigned long long a = 0, b = 1, temp;
    while (1) {
        temp = a + b;
        a = b;
        b = temp;
    }
    return NULL;
}

int main() {
    // Pin this program to core 2
    pin_to_core(CORE_ID);

    // Create a thread to perform CPU-intensive tasks
    pthread_t cpu_thread;
    if (pthread_create(&cpu_thread, NULL, cpu_intensive_task, NULL) != 0) {
        perror("pthread_create");
        return EXIT_FAILURE;
    }

    // Join the CPU-intensive thread (in this example, it will run forever)
    if (pthread_join(cpu_thread, NULL) != 0) {
        perror("pthread_join");
        return EXIT_FAILURE;
    }

    return 0;
}
