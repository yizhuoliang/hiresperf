#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <unistd.h>

#include "hrperf_api.h"

#define REGION_SIZE (1L * 1024 * 1024 * 1024)  // 1GB

void* fill_memory(void* arg) {
    hrp_bpf_start();
    hrperf_start();
    uint8_t* region = (uint8_t*)arg;
    for (size_t i = 0; i < REGION_SIZE; i++) {
        // Write the magic number 231 to each byte
        region[i] = 231;
    }
    hrperf_pause();
    hrp_bpf_stop();
    return NULL;
}

int main() {
    // Allocate and zero-initialize 4GB region
    uint8_t* region = (uint8_t*)malloc(REGION_SIZE);
    if (!region) {
        perror("Failed to allocate memory");
        return EXIT_FAILURE;
    }

    memset(region, 127, REGION_SIZE);

    sleep(3);

    pthread_t thread;
    pthread_attr_t attr;
    cpu_set_t cpuset;

    // Initialize thread attributes
    pthread_attr_init(&attr);

    // Set CPU affinity to core 2
    CPU_ZERO(&cpuset);
    CPU_SET(4, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);

    // Create the thread to fill memory
    if (pthread_create(&thread, &attr, fill_memory, (void*)region) != 0) {
        perror("Failed to create thread");
        free(region);
        return EXIT_FAILURE;
    }

    // Wait for the thread to finish
    pthread_join(thread, NULL);

    // Post thread join, scan through the buffer to verify all slots are written
    printf("Verifying buffer contents...\n");
    for (size_t i = 0; i < REGION_SIZE; i++) {
        if (region[i] != 231) {
            printf("Verification failed at index %zu: expected 231, found %d\n", i, region[i]);
            free(region);
            pthread_attr_destroy(&attr);
            return EXIT_FAILURE;
        }
    }

    // Clean up
    free(region);
    pthread_attr_destroy(&attr);

    printf("Memory fill and verification completed successfully.\n");
    return EXIT_SUCCESS;
}