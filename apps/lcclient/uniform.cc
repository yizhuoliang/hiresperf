#include <arpa/inet.h>
#include <assert.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>
#include <getopt.h>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "hrperf_api.h"

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#define barrier()       asm volatile("" ::: "memory")

#define CYCLES_PER_US 2396

constexpr uint64_t NORM = 100;

namespace {

using namespace std::chrono;
using sec = duration<double, std::micro>;

int threads;                // Threads per server
unsigned long raddr;        // Server IP address
double offered_load;
bool saturate = false;

// Instead of a single port, we now support multiple servers (each with its own port).
std::vector<uint16_t> serverPorts;

// Experiment parameters
constexpr uint64_t kWarmUpTime = 0;
constexpr uint64_t kExperimentTime = 10000000;
constexpr uint64_t kRTT = 1000;
std::vector<uint64_t> frequencies;

// Client-side statistic structures
struct cstat_raw {
  double offered_rps;
  double rps;
  double goodput;
  uint64_t total_requests_sent;
  uint64_t total_responses_received;
};

struct cstat {
  double offered_rps;
  double rps;
  double goodput;
  uint64_t total_requests_sent;
  uint64_t total_responses_received;
};

struct payload {
  uint64_t term_index;
  uint64_t index;
  uint64_t hash;
};

struct work_unit {
  double start_us, duration_us;
  int hash;
  bool success;
  uint64_t term_index;
  uint64_t timing;
  uint64_t idx;
};

static inline __attribute__((always_inline)) uint64_t rdtsc(void) {
  uint32_t a, d;
  asm volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) << 32);
}

static inline __attribute__((always_inline)) void cpu_relax(void)
{
  asm volatile("pause");
}

static inline __attribute__((always_inline)) void __time_delay_us(uint64_t us)
{
  uint64_t cycles = us * CYCLES_PER_US;
  unsigned long start = rdtsc();

  while (rdtsc() - start < cycles)
    cpu_relax();
}

static uint64_t mc_swap64(uint64_t in) {
    int64_t rv = 0;
    for(int i = 0; i<8; i++) {
       rv = (rv << 8) | (in & 0xff);
       in >>= 8;
    }
    return rv;
}

uint64_t ntohll(uint64_t val) {
   return mc_swap64(val);
}

uint64_t htonll(uint64_t val) {
   return mc_swap64(val);
}

int TcpDial(unsigned long ip, uint16_t port) {
  int fd;
  struct sockaddr_in addr;

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fprintf(stderr, "Failed to create a socket\n");
    return -1;
  }
  bzero(&addr, sizeof(addr));

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ip;
  addr.sin_port = htons(port);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
    fprintf(stderr, "Failed to connect: port = %u\n", port);
    close(fd);
    return -1;
  }

  return fd;
}

ssize_t TcpReadFull(int fd, void *buf, size_t len) {
  char *pos = reinterpret_cast<char *>(buf);
  size_t n = 0;
  while (n < len) {
    ssize_t ret = read(fd, pos + n, len - n);
    if (ret <= 0) return ret;
    n += ret;
  }
  assert(n == len);
  return n;
}

ssize_t TcpWriteFull(int fd, const void *buf, size_t len) {
  const char *pos = reinterpret_cast<const char *>(buf);
  size_t n = 0;
  while (n < len) {
    ssize_t ret = send(fd, pos + n, len - n, 0);
    if (ret <= 0) return ret;
    n += ret;
  }
  assert(n == len);
  return n;
}

// The maximum lateness to tolerate before dropping egress samples.
constexpr uint64_t kMaxCatchUpUS = 5;

uint64_t ChooseTerm() {
  static thread_local std::mt19937 rg(std::random_device{}());
  std::uniform_int_distribution<uint64_t> dist(0, frequencies.size() - 1);
  return dist(rg);
}

template <class Arrival>
std::vector<work_unit> GenerateWork(Arrival a, double cur_us,
                                    double last_us) {
  std::vector<work_unit> w;

  printf("Generating Work...\t");

  while (true) {
    cur_us += a();
    if (cur_us > last_us) break;
    w.emplace_back(work_unit{cur_us, 0, rand(), false, ChooseTerm()});
  }

  printf("Done\n");

  return w;
}

// Synchronization variables for start barrier
std::mutex mtx;
std::condition_variable cv;
std::atomic<int> ready_count(0);
bool start_flag = false;

std::vector<work_unit> ClientWorker(
    int c, int id, int total_threads,
    std::function<std::vector<work_unit>()> wf) {
  srand(time(NULL) * (id+1));
  std::vector<work_unit> w(wf());

  // Start the receiver thread.
  auto th = std::thread([&] {
    payload rp;
    ssize_t ret;
    uint64_t now;
    uint32_t idx;

    while (true) {
      ret = TcpReadFull(c, &rp, sizeof(rp));
      if (ret <= 0) break;

      barrier();
      now = rdtsc();
      barrier();

      idx = ntohll(rp.index);
      idx = (idx - id) / total_threads;

      if (idx < w.size()) {
        w[idx].duration_us = 1.0 * (now - w[idx].timing) / CYCLES_PER_US;
        w[idx].success = true;
      }
    }
  });

  // Synchronize with main thread before experiment start
  {
    std::unique_lock<std::mutex> lock(mtx);
    ready_count++;
    if (ready_count == total_threads) {
      cv.notify_one(); // Notify main thread
    }
    cv.wait(lock, [] { return start_flag; });
  }

  barrier();
  uint64_t expstart = rdtsc();
  barrier();

  payload p;
  auto wsize = w.size();

  printf("Thread %d started sending requests\n", id);
  for (unsigned int i = 0; i < wsize; ++i) {
    barrier();
    uint64_t now = rdtsc();
    barrier();
    if (now - expstart < w[i].start_us * CYCLES_PER_US) {
      __time_delay_us(w[i].start_us - 1.0 * (now - expstart) / CYCLES_PER_US);
    }
    if (!saturate && (now - expstart) > (w[i].start_us + kMaxCatchUpUS) * CYCLES_PER_US)
      continue;

    w[i].idx = i * total_threads + id;
    barrier();
    w[i].timing = rdtsc();
    barrier();

    p.term_index = htonll(w[i].term_index);
    p.index = htonll(w[i].idx);
    p.hash = htonll(w[i].hash);

    // Send request
    ssize_t ret = TcpWriteFull(c, &p, sizeof(p));
    if (ret <= 0) {
      break;
    }
  }
  printf("Thread %d finished sending requests\n", id);

  __time_delay_us(1000);
  shutdown(c, SHUT_RDWR);
  close(c);
  th.join();
  printf("Thread %d listener thread joined\n", id);

  return w;
}

std::vector<work_unit> RunExperiment(
    int threads_per_server,
    struct cstat_raw *csr, double *elapsed,
    std::function<std::vector<work_unit>()> wf) {
  // We now have multiple servers. We will create threads_per_server threads per server.
  // Total threads = serverPorts.size() * threads_per_server
  int total_threads = (int)(serverPorts.size() * threads_per_server);

  // Create one TCP connection per thread per server.
  std::vector<int> conns;
  conns.reserve(total_threads);

  for (auto port : serverPorts) {
    for (int i = 0; i < threads_per_server; ++i) {
      int outc = TcpDial(raddr, port);
      if (outc < 0) {
        fprintf(stderr, "Cannot connect to server on port %u\n", port);
        exit(1);
      }
      conns.push_back(outc);
    }
  }

  // Launch a worker thread for each connection.
  std::vector<std::thread> th;
  std::unique_ptr<std::vector<work_unit>> samples[total_threads];

  // Each thread is globally indexed. For server i (0-based) and thread j (0-based):
  // global_id = i * threads_per_server + j
  for (int i = 0; i < total_threads; ++i) {
    th.emplace_back(std::thread([&, i] {
      auto v = ClientWorker(conns[i], i, total_threads, wf);
      samples[i].reset(new std::vector<work_unit>(std::move(v)));
    }));
  }

  // Wait until all threads are ready
  {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [total_threads] { return ready_count == total_threads; });
    // All threads are ready
    start_flag = true;
    cv.notify_all();
  }

  // Start experiment timing
  barrier();
  uint64_t start = rdtsc();
  barrier();

  // Wait for the workers to finish.
  for (auto &t : th) t.join();

  // End experiment timing
  barrier();
  uint64_t finish = rdtsc();
  barrier();

  // Force connections to close (already done)
  for (int c : conns) close(c);

  double elapsed_ = 1.0 * (finish - start) / CYCLES_PER_US;
  elapsed_ -= kWarmUpTime;

  // Aggregate all samples
  std::vector<work_unit> w;
  uint64_t resps = 0;
  uint64_t offered = 0;
  uint64_t client_drop = 0;

  for (int i = 0; i < total_threads; ++i) {
    auto &v = *samples[i];
    // Remove requests arrived during warm-up
    v.erase(std::remove_if(v.begin(), v.end(),
                        [](const work_unit &s) {
                          return ((s.start_us + s.duration_us) < kWarmUpTime);
                        }),
            v.end());

    offered += v.size();
    client_drop += std::count_if(v.begin(), v.end(), [](const work_unit &s) {
      return (s.duration_us == 0);
    });

    // Remove local drops
    v.erase(std::remove_if(v.begin(), v.end(),
                        [](const work_unit &s) {
                          return (s.duration_us == 0);
                        }),
            v.end());
    int resp_success = std::count_if(v.begin(), v.end(), [](const work_unit &s) {
      return s.success;
    });
    resps += resp_success;
    w.insert(w.end(), v.begin(), v.end());
  }

  // Report results
  if (csr) {
    csr->offered_rps = static_cast<double>(offered) / elapsed_ * 1000000;
    csr->rps = static_cast<double>(resps) / elapsed_ * 1000000;
    csr->total_requests_sent = offered;
    csr->total_responses_received = resps;
  }

  *elapsed = elapsed_;
  return w;
}

void PrintStatResults(std::vector<work_unit> w, struct cstat *cs, double elapsed) {
  if (w.size() == 0) {
    std::cout << "No successful responses received." << std::endl;
    return;
  }

  printf("Generating output...\n");

  // Keep only successful responses
  w.erase(std::remove_if(w.begin(), w.end(),
             [](const work_unit &s) {
               return !s.success;
    }), w.end());

  double count = static_cast<double>(w.size());

  // Sort by duration
  std::sort(w.begin(), w.end(),
      [](const work_unit &s1, const work_unit &s2) {
      return s1.duration_us < s2.duration_us;
  });

  double sum = std::accumulate(
      w.begin(), w.end(), 0.0,
      [](double s, const work_unit &c) { return s + c.duration_us; });
  double mean = sum / w.size();
  double p50 = w[(int)(count * 0.5)].duration_us;
  double p90 = w[(int)(count * 0.9)].duration_us;
  double p99 = w[(int)(count * 0.99)].duration_us;
  double p999 = w[(int)(count * 0.999)].duration_us;
  double p9999 = w[(int)(count * 0.9999)].duration_us;
  double min = w[0].duration_us;
  double max = w[w.size() - 1].duration_us;

  std::cout << "Number of Client Threads per Server: " << threads << std::endl;
  std::cout << "Number of Servers: " << serverPorts.size() << std::endl;
  std::cout << "Total Threads: " << serverPorts.size() * threads << std::endl;
  std::cout << "Offered Load (RPS): " << cs->offered_rps << std::endl;
  std::cout << "Actual RPS: " << cs->rps << std::endl;
  std::cout << "Total Requests Sent: " << cs->total_requests_sent << std::endl;
  std::cout << "Total Responses Received: " << cs->total_responses_received << std::endl;
  std::cout << "Total Elapsed Time (s): " << elapsed / 1e6 << std::endl;
  std::cout << "Latency Statistics (us):" << std::endl;
  std::cout << "  Min: " << min << std::endl;
  std::cout << "  Mean: " << mean << std::endl;
  std::cout << "  P50: " << p50 << std::endl;
  std::cout << "  P90: " << p90 << std::endl;
  std::cout << "  P99: " << p99 << std::endl;
  std::cout << "  P99.9: " << p999 << std::endl;
  std::cout << "  P99.99: " << p9999 << std::endl;
  std::cout << "  Max: " << max << std::endl;
}

void SteadyStateExperiment(int threads_local, double offered_rps) {
  struct cstat_raw csr = {0};
  struct cstat cs;
  double elapsed;

  std::vector<work_unit> w = RunExperiment(threads_local, &csr, &elapsed,
                       [=] {
    std::mt19937 rg(rand());
    std::exponential_distribution<double> rd(
        1.0 / (1000000.0 / (offered_rps / static_cast<double>(threads_local * serverPorts.size()))));
    return GenerateWork(std::bind(rd, rg), 0, kExperimentTime);
  });

  cs = cstat{csr.offered_rps,
         csr.rps,
         csr.goodput,
         csr.total_requests_sent,
         csr.total_responses_received};

  PrintStatResults(w, &cs, elapsed);
}

void ReadFreqTerms() {
  std::ifstream fin("frequent_terms.csv");

  std::string line, word;

  while (std::getline(fin, line)) {
    std::stringstream ss(line);

    // Just read lines to maintain compatibility; we do not actually use frequencies now.
    getline(ss, word, ',');
    getline(ss, word, ',');
    // Assign equal weight for uniform distribution
    frequencies.push_back(1);
  }

  fin.close();
}

void print_usage(char *progname) {
  std::cout << "Usage: " << progname << " [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -t, --threads <N>         Number of threads per server" << std::endl;
  std::cout << "  -s, --server <IP>         Server IP address" << std::endl;
  std::cout << "  -l, --load <RPS>          Offered load (RPS)" << std::endl;
  std::cout << "  -p, --port <PORT>         Specify a server port (can be repeated)" << std::endl;
  std::cout << "      --saturate            Saturate the server (ignore pacing)" << std::endl;
  std::cout << "  -h, --help                Show this help message" << std::endl;
}

}  // anonymous namespace

int main(int argc, char *argv[]) {
  ReadFreqTerms();

  int option_index = 0;
  int c;

  static struct option long_options[] = {
      {"threads", required_argument, 0, 't'},
      {"server", required_argument, 0, 's'},
      {"load", required_argument, 0, 'l'},
      {"port", required_argument, 0, 'p'},
      {"saturate", no_argument, 0, 0},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
  };

  if (argc < 2) {
    print_usage(argv[0]);
    return -1;
  }

  while ((c = getopt_long(argc, argv, "t:s:l:p:h", long_options, &option_index)) != -1) {
    switch (c) {
      case 't':
        threads = std::stoi(optarg);
        break;
      case 's':
        raddr = inet_addr(optarg);
        if (raddr == INADDR_NONE) {
          std::cerr << "Invalid IP address: " << optarg << std::endl;
          return -1;
        }
        break;
      case 'l':
        offered_load = std::stod(optarg);
        break;
      case 'p': {
        uint16_t port = (uint16_t)std::stoi(optarg);
        serverPorts.push_back(port);
        break;
      }
      case 0:
        if (strcmp(long_options[option_index].name, "saturate") == 0) {
          saturate = true;
        }
        break;
      case 'h':
      default:
        print_usage(argv[0]);
        return -1;
    }
  }

  if (threads <= 0 || offered_load <= 0 || raddr == 0 || serverPorts.empty()) {
    print_usage(argv[0]);
    return -1;
  }

  SteadyStateExperiment(threads, offered_load);
  hrperf_pause();
  return 0;
}

