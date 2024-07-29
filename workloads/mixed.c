#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>

#include "hrperf_control.h"

// Macros to configure parameters
#define MEMORY_SIZE (4UL * 1024 * 1024 * 1024) // 4GB
#define CORE_ID 2
#define RUN_TIME 2 // seconds

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

void* memory_scanner(void* arg) {
    uint8_t *memory_region = (uint8_t*) arg;
    time_t start_time = time(NULL);

    while (difftime(time(NULL), start_time) < RUN_TIME) {
        for (size_t i = 0; i < MEMORY_SIZE; i++) {
            uint8_t value = memory_region[i];
            (void)value; // Prevent unused variable warning
        }
    }
    return NULL;
}

void* cpu_intensive_task(void* arg) {
    time_t start_time = time(NULL);
    unsigned long long a = 0, b = 1, temp;

    while (difftime(time(NULL), start_time) < RUN_TIME) {
        temp = a + b;
        a = b;
        b = temp;
    }
    return NULL;
}

int main() {
    pin_to_core(CORE_ID);

    // Allocate memory region
    uint8_t *memory_region = (uint8_t*) malloc(MEMORY_SIZE);
    if (!memory_region) {
        perror("Failed to allocate memory");
        return EXIT_FAILURE;
    }

    // Initialize memory to ensure it's actually allocated
    memset(memory_region, 0, MEMORY_SIZE);

    hrperf_start();

    // Create a thread to run the memory-intensive task
    pthread_t memory_thread;
    if (pthread_create(&memory_thread, NULL, memory_scanner, memory_region) != 0) {
        perror("pthread_create");
        free(memory_region);
        return EXIT_FAILURE;
    }

    // Wait for the memory-intensive task to finish
    if (pthread_join(memory_thread, NULL) != 0) {
        perror("pthread_join");
        free(memory_region);
        return EXIT_FAILURE;
    }

    // Create a thread to run the CPU-intensive task
    pthread_t cpu_thread;
    if (pthread_create(&cpu_thread, NULL, cpu_intensive_task, NULL) != 0) {
        perror("pthread_create");
        free(memory_region);
        return EXIT_FAILURE;
    }

    // Wait for the CPU-intensive task to finish
    if (pthread_join(cpu_thread, NULL) != 0) {
        perror("pthread_join");
        free(memory_region);
        return EXIT_FAILURE;
    }

    hrperf_pause();

    // Free allocated memory
    free(memory_region);

    return EXIT_SUCCESS;
}