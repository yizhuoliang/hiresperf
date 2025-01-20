#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <vector>

#include "LuceneHeaders.h"
#include "Document.h"
#include "Field.h"
#include "Term.h"
#include "TermQuery.h"
#include "TopFieldDocs.h"
#include "IndexWriter.h"
#include "IndexSearcher.h"
#include "KeywordAnalyzer.h"
#include "Query.h"

extern "C" {
#include "ldb/tag.h"
#include "logger.h"
}
#include "hrperf_api.h"

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#define barrier()       asm volatile("" ::: "memory")

#define CYCLES_PER_US 2396

using namespace Lucene;

constexpr uint64_t NORM = 100;
constexpr int searchN = 100;

std::vector<String> terms;
std::vector<uint64_t> frequencies;
uint64_t weight_sum;
Lucene::RAMDirectoryPtr dir;

struct payload {
  uint64_t term_index;
  uint64_t index;
  uint64_t hash;
};

static inline __attribute__((always_inline)) uint64_t rdtsc(void) {
  uint32_t a, d;
  asm volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) << 32);
}

static uint64_t mc_swap64(uint64_t in) {
    int64_t rv = 0;
    int i = 0;
    for(i = 0; i<8; i++) {
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

int TcpListen(uint16_t port, int backlog) {
  int fd;
  int opt = 1;
  struct sockaddr_in addr;

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fprintf(stderr, "Failed to create socket\n");
    return -1;
  }

  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    fprintf(stderr, "Failed to set socket options\n");
    return -1;
  }

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "Failed to bind\n");
    return -1;
  }

  if (listen(fd, backlog) < 0) {
    fprintf(stderr, "Failed to listen\n");
    return -1;
  }

  return fd;
}

int TcpAccept(int fd, uint16_t port) {
  int s;
  struct sockaddr_in addr;
  int addrlen = sizeof(addr);

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if ((s = accept(fd, (struct sockaddr *)&addr, (socklen_t*)&addrlen)) < 0) {
    fprintf(stderr, "Failed to accept\n");
    return -1;
  }

  return s;
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
    return -1;
  }

  return fd;
}

ssize_t TcpReadFull(int fd, void *buf, size_t len) {
  char *pos = reinterpret_cast<char *>(buf);
  size_t n = 0;
  while (n < len) {
    ssize_t ret = read(fd, pos + n, len - n);
    if (ret < 0) return ret;
    if (ret == 0) return -1; // Connection closed
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
    if (ret < 0) return ret;
    if (ret == 0) return -1; // Connection closed
    n += ret;
  }
  assert(n == len);
  return n;
}

String ChooseTerm() {
  uint64_t rand_ = (uint64_t)rand() % weight_sum;
  for (int i = 0; i < (int)frequencies.size(); ++i) {
    if (rand_ < frequencies[i]) {
      return terms[i];
    } else {
      rand_ -= frequencies[i];
    }
  }
  return terms.back();
}

String ChooseTerm(uint64_t hash) {
  uint64_t rand_ = hash % weight_sum;
  for (int i = 0; i < (int)frequencies.size(); ++i) {
    if (rand_ < frequencies[i]) {
      return terms[i];
    } else {
      rand_ -= frequencies[i];
    }
  }
  return terms.back();
}

void ReadFreqTerms() {
    std::cout << "Reading csv ...\t" << std::flush;
    std::ifstream fin("frequent_terms.csv");

    std::string line, word;
    String wword;
    uint64_t freq;

    weight_sum = 0;

    while(std::getline(fin, line)) {
        std::stringstream ss(line);

        if (!std::getline(ss, word, ',')) {
            continue;
        }
        wword = String(word.length(), L' ');
        std::copy(word.begin(), word.end(), wword.begin());
        terms.push_back(wword);

        if (!std::getline(ss, word, ',')) {
            continue;
        }

        try {
            freq = std::stoi(word) / NORM;
            frequencies.push_back(freq);
            weight_sum += freq;
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid argument: " << word << " in line: " << line << std::endl;
        } catch (const std::out_of_range& e) {
            std::cerr << "Out of range: " << word << " in line: " << line << std::endl;
        }
    }

    fin.close();
    std::cout << "Done" << std::endl;
}

std::string sanitizeString(const std::string& input) {
    std::string result;
    for (char c : input) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c == ' ') ||
            (c == ',') ||
            (c == '.')) {
            result += c;
        }
    }
    return result;
}

Lucene::DocumentPtr createDocument(const String& contents) {
    if (contents.empty()) {
        std::cerr << "Empty contents received for document creation." << std::endl;
        return nullptr;
    }

    DocumentPtr document = newLucene<Document>();
    if (!document) {
        std::cerr << "Failed to create a new document instance." << std::endl;
        return nullptr;
    }

    try {
        document->add(newLucene<Field>(L"contents", contents, Field::STORE_YES, Field::INDEX_ANALYZED));
    } catch (const LuceneException& e) {
        std::cerr << "Lucene exception in document field addition" << std::endl;
        return nullptr;
    } catch (const std::exception& e) {
        std::cerr << "Standard exception in document field addition: " << e.what() << std::endl;
        return nullptr;
    }

    return document;
}

void PopulateIndex() {
    std::cout << "Populating indices ...\t" << std::flush;
    uint64_t start = rdtsc();
    int num_docs = 0;

    dir = newLucene<RAMDirectory>();
    if (!dir) {
        std::cerr << "Failed to create RAMDirectory instance." << std::endl;
        return;
    }

    IndexWriterPtr indexWriter = newLucene<IndexWriter>(dir, newLucene<StandardAnalyzer>(LuceneVersion::LUCENE_CURRENT), true, IndexWriter::MaxFieldLengthLIMITED);
    if (!indexWriter) {
        std::cerr << "Failed to create IndexWriter instance." << std::endl;
        return;
    }

    std::ifstream csvFile("test.csv");
    if (!csvFile.is_open()) {
        std::cerr << "Unable to open file" << std::endl;
        return;
    }

    std::string line;
    int iteration = 0;
    while (getline(csvFile, line)) {
        iteration++;
        if (iteration % 1000 == 0) {
            std::cerr << "Processing line #" << iteration << std::endl;
        }

        std::stringstream ss(line);
        std::string polarity, title, review;
        getline(ss, polarity, ',');
        getline(ss, title, ',');
        getline(ss, review, ',');
        review = sanitizeString(review);

        if (review.empty()) {
            continue;
        }

        String wreview = String(review.length(), L' ');
        std::copy(review.begin(), review.end(), wreview.begin());
        if (wreview.empty()) {
            std::cerr << "Conversion to wide string resulted in empty string for review: " << review << std::endl;
            continue;
        }

        DocumentPtr document = createDocument(wreview);
        if (!document) {
            std::cerr << "Document creation failed for review: " << review << std::endl;
            continue;
        }

        try {
            indexWriter->addDocument(document);
            num_docs++;
        } catch (const LuceneException& e) {
            std::cerr << "Lucene exception when adding document" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Exception when adding document: " << e.what() << std::endl;
        }
    }
    csvFile.close();

    try {
        indexWriter->optimize();
        indexWriter->close();
    } catch (const LuceneException& e) {
        std::cerr << "Lucene exception during optimization or closing" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception during optimization or closing: " << e.what() << std::endl;
    }

    uint64_t finish = rdtsc();
    std::cout << "Done: " << num_docs << " documents ("
              << (finish - start) / CYCLES_PER_US / 1000000.0 << " s)" << std::endl;
}

// A simple net barrier for multiple processes. One process acts as a coordinator (server)
// and the others as participants (clients).
// All processes must call Wait() and will only proceed once all have connected and the
// coordinator has signaled start.
class NetBarrier {
public:
  NetBarrier(int numProcesses, bool isCoordinator, const std::string& host, int port)
    : numProcesses_(numProcesses),
      isCoordinator_(isCoordinator),
      host_(host),
      port_(port) {}

  void Wait() {
    if (isCoordinator_) {
      runCoordinator();
    } else {
      runParticipant();
    }
  }

private:
  int numProcesses_;
  bool isCoordinator_;
  std::string host_;
  int port_;

  void runCoordinator() {
    int server_fd;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
      throw std::runtime_error("Coordinator: socket failed");
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))) {
      close(server_fd);
      throw std::runtime_error("Coordinator: setsockopt failed");
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(host_.c_str());
    address.sin_port = htons(port_);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
      close(server_fd);
      throw std::runtime_error("Coordinator: bind failed");
    }

    if (listen(server_fd, numProcesses_ - 1) < 0) {
      close(server_fd);
      throw std::runtime_error("Coordinator: listen failed");
    }

    std::vector<int> client_sockets;
    client_sockets.reserve(numProcesses_ - 1);

    for (int i = 0; i < numProcesses_ - 1; ++i) {
      int new_socket;
      struct sockaddr_in client_address;
      socklen_t addrlen = sizeof(client_address);
      if ((new_socket = accept(server_fd, (struct sockaddr *)&client_address, &addrlen)) < 0) {
        close(server_fd);
        throw std::runtime_error("Coordinator: accept failed");
      }
      client_sockets.push_back(new_socket);
    }

    // All processes are connected now, send "start" signal to all
    const char* msg = "start";
    for (auto sock : client_sockets) {
      if (send(sock, msg, strlen(msg), 0) != (ssize_t)strlen(msg)) {
        // Non-critical error
      }
      close(sock);
    }

    close(server_fd);
  }

  void runParticipant() {
    int sock = 0;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      throw std::runtime_error("Participant: socket failed");
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &serv_addr.sin_addr) <= 0) {
      close(sock);
      throw std::runtime_error("Participant: invalid address");
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      close(sock);
      throw std::runtime_error("Participant: connection failed");
    }

    // Wait for "start" signal
    char buffer[128];
    int valread = recv(sock, buffer, 128, 0);
    if (valread <= 0) {
      close(sock);
      throw std::runtime_error("Participant: failed to receive start signal");
    }
    buffer[valread] = '\0';
    if (std::string(buffer) != "start") {
      close(sock);
      throw std::runtime_error("Participant: invalid start signal");
    }

    close(sock);
  }
};

void *luceneWorker(void *arg) {
  int c = *((int *)arg);
  free(arg);

  payload rp;
  ssize_t ret;

  IndexSearcherPtr searcher = newLucene<IndexSearcher>(dir, true);

  while (true) {
    ret = TcpReadFull(c, &rp, sizeof(rp));
    if (ret <= 0) break;

    uint64_t term_index = ntohll(rp.term_index);
    uint64_t index = ntohll(rp.index);
    ldb_tag_set(index);

    QueryPtr query = newLucene<TermQuery>(newLucene<Term>(L"contents", terms[term_index]));
    Collection<ScoreDocPtr> hits = searcher->search(query, FilterPtr(), searchN)->scoreDocs;

    ret = TcpWriteFull(c, &rp, sizeof(rp));
    if (ret <= 0) break;
    ldb_tag_clear();
  }

  close(c);

  return nullptr;
}

void runServer(uint16_t lucenePort) {
  int q = TcpListen(lucenePort, 4096);
  if (q < 0) {
    std::cerr << "Failed to listen on port " << lucenePort << std::endl;
    return;
  }

  while (true) {
    int c = TcpAccept(q, lucenePort);
    if (c < 0) {
      std::cerr << "Failed to accept connection" << std::endl;
      continue;
    }
    pthread_t worker_th;
    int *arg = (int *)malloc(sizeof(int));
    *arg = c;
    pthread_create(&worker_th, NULL, &luceneWorker, (void *)arg);
    pthread_detach(worker_th);
  }
}

int main(int argc, char *argv[]) {
  // Expected arguments:
  // argv[1]: lucenePort (the port this instance listens on)
  // argv[2]: numProcesses (for net barrier)
  // argv[3]: isCoordinator (1 or 0)
  // argv[4]: host (for net barrier)
  // argv[5]: netBarrierPort (for net barrier)
  if (argc != 6) {
    std::cerr << "Usage: " << argv[0] << " <lucenePort> <numProcesses> <isCoordinator> <host> <netBarrierPort>" << std::endl;
    return 1;
  }

  uint16_t lucenePort = (uint16_t)std::stoi(argv[1]);
  int numProcesses = std::stoi(argv[2]);
  bool isCoordinator = (std::stoi(argv[3]) == 1);
  std::string host = argv[4];
  int netBarrierPort = std::stoi(argv[5]);

  srand((unsigned)time(NULL));

  ReadFreqTerms();
  PopulateIndex();

  // Create the net barrier and wait for all processes
  NetBarrier netBarrier(numProcesses, isCoordinator, host, netBarrierPort);
  netBarrier.Wait();

  logger_reset();
  hrperf_start(); // the client should call pause, like manually with pause.c

  runServer(lucenePort);

  return 0;
}
