#ifndef TSC_H
#define TSC_H

#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include "../include/common.h"

static u64 cycles_per_us ALIGN_TO_CACHE_LINE = 0;

static inline __attribute__((always_inline)) void cpu_serialize(void) {
  asm volatile("xorl %%eax, %%eax\n\t"
               "cpuid"
               :
               :
               : "%rax", "%rbx", "%rcx", "%rdx");
}

static inline __attribute__((always_inline)) u64 __rdtsc(void) {
  u32 a, d;
  asm volatile("rdtsc" : "=a"(a), "=d"(d));
  return ((u64)a) | (((u64)d) << 32);
}

static inline __attribute__((always_inline)) u64 __rdtscp(uint32_t *auxp) {
  uint32_t a, d, c;
  asm volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(c));
  if (auxp)
    *auxp = c;
  return ((u64)a) | (((u64)d) << 32);
}

/* modified from DPDK implementation */
/* not used rn */
static u64 __time_calibrate_tsc(void) {
  u64 cycles_per_us = 0;

  struct timespec64 t_start, t_end;
  u64 start, end, ns;
  double secs;

  cpu_serialize();
  ktime_get_raw_ts64(&t_start);
  start = __rdtsc();

  /* Sleep for ~500ms */
  msleep(500);

  ktime_get_raw_ts64(&t_end);
  end = __rdtscp(NULL);

  ns = (t_end.tv_sec - t_start.tv_sec) * 1000000000ULL;
  ns += (t_end.tv_nsec - t_start.tv_nsec);

  secs = (double)ns / 1000.0; // microseconds
  cycles_per_us = (u64)((end - start) / secs);

  return cycles_per_us;
}

// Helper function to calibrate TSC frequency (cycles per microsecond)
static u64 hrp_calibrate_tsc(void) {
  u64 start_tsc, end_tsc, elapsed_tsc;
  ktime_t start_time, end_time;
  s64 elapsed_ns;
  const unsigned int delay_ms = 500;

  // Prevent migration during measurement
  preempt_disable();
  cpu_serialize();
  start_time = ktime_get();
  start_tsc = __rdtsc();
  preempt_enable();

  msleep(delay_ms);

  preempt_disable();
  end_tsc = __rdtscp(NULL);
  end_time = ktime_get();
  preempt_enable();

  elapsed_tsc = end_tsc - start_tsc;
  elapsed_ns = ktime_to_ns(ktime_sub(end_time, start_time));

  if (elapsed_ns <= 0) {
    pr_warn("hrperf: TSC calibration failed (elapsed_ns <= 0)\n");
    return 0;
  }

  // Calculate cycles per microsecond: (cycles * 1,000) / ns
  cycles_per_us = div64_u64(elapsed_tsc * 1000, elapsed_ns);
  return cycles_per_us;
}

#endif // TSC_H