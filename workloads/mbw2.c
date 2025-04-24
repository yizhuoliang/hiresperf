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
#define MEMORY_SIZE (4UL * 1024 * 1024 * 1024) // 4 GiB
#define CORE_ID 2

void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

void* memory_writer(void* arg) {
    uint8_t *memory_region = (uint8_t*) arg;
    struct timespec start_time, end_time;
    uint64_t start_ns, end_ns, elapsed_ns;

    // Get start time using CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);
    start_ns = (uint64_t)start_time.tv_sec * 1000000000ULL + start_time.tv_nsec;

    printf("Write start time: %lu.%09lu seconds (CLOCK_MONOTONIC_RAW)\n",
           (unsigned long)start_time.tv_sec, start_time.tv_nsec);

    // Start profiling
    hrperf_start();

    // Perform one full write pass
    for (size_t i = 0; i < MEMORY_SIZE; i++) {
        memory_region[i] = (uint8_t)(i & 0xFF);
    }

    // Pause profiling
    hrperf_pause();

    // Get end time using CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &end_time);
    end_ns = (uint64_t)end_time.tv_sec * 1000000000ULL + end_time.tv_nsec;
    elapsed_ns = end_ns - start_ns;

    printf("Write end time: %lu.%09lu seconds (CLOCK_MONOTONIC_RAW)\n",
           (unsigned long)end_time.tv_sec, end_time.tv_nsec);
    printf("Total write time: %lu.%09lu seconds (%lu nanoseconds)\n",
           elapsed_ns / 1000000000ULL, elapsed_ns % 1000000000ULL, elapsed_ns);

    return NULL;
}

int main() {
    // Pin this program to CORE_ID
    pin_to_core(CORE_ID);

    // Allocate memory region
    uint8_t *memory_region = malloc(MEMORY_SIZE);
    if (!memory_region) {
        perror("Failed to allocate memory");
        return 1;
    }

    // Optionally initialize to zero (cold pages)
    memset(memory_region, 0, MEMORY_SIZE);

    // Create a thread to write the memory region
    pthread_t writer_thread;
    if (pthread_create(&writer_thread, NULL, memory_writer, memory_region) != 0) {
        perror("Failed to create writer thread");
        free(memory_region);
        return 1;
    }

    // Wait for the writer thread to complete
    pthread_join(writer_thread, NULL);

    // Free allocated memory
    free(memory_region);

    return 0;
}
