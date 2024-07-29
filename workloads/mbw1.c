#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

#include "hrperf_control.h"

// Macros to configure parameters
#define MEMORY_SIZE (4UL * 1024 * 1024 * 1024) // 4GB
#define CORE_ID 2

void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

void* memory_scanner(void* arg) {
    // start profiling
    hrperf_start();
    uint8_t *memory_region = (uint8_t*) arg;
    while (1) {
        for (size_t i = 0; i < MEMORY_SIZE; i++) {
            uint8_t value = memory_region[i];
            (void)value; // Prevent unused variable warning
        }
    }
    return NULL;
}

int main() {
    // Pin this program to core 1
    pin_to_core(CORE_ID);

    // Allocate memory region
    uint8_t *memory_region = (uint8_t*) malloc(MEMORY_SIZE);
    if (!memory_region) {
        perror("Failed to allocate memory");
        return 1;
    }

    // Initialize memory to ensure it's actually allocated
    memset(memory_region, 0, MEMORY_SIZE);

    // Create a thread to constantly scan the memory region
    pthread_t scanner_thread;
    pthread_create(&scanner_thread, NULL, memory_scanner, memory_region);

    // Join the scanner thread (in this example, it will run forever)
    pthread_join(scanner_thread, NULL);

    // Free allocated memory (unreachable in this example)
    free(memory_region);

    return 0;
}
