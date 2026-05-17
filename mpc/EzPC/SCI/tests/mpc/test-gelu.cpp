#include "FloatingPoint/fp-math.h"
#include "BuildingBlocks/aux-protocols.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <math.h>

using namespace sci;
using namespace std;

#define MAX_THREADS 16

int party, port = 32000;
int num_threads = 16;
string address = "127.0.0.1";

int dim =  128*3072;
int bw_x = 20;
int bw_y = 37;
int s_x = 11;
int s_y = 4;

uint64_t mask_x = (bw_x == 64 ? -1 : ((1ULL << 12) - 1));
uint64_t mask_y = (bw_y == 64 ? -1 : ((1ULL << 13) - 1));

IOPack *iopackArr[MAX_THREADS];
OTPack *otpackArr[MAX_THREADS];

void operation_thread(int tid, uint64_t *x, uint64_t *y, int num_ops) {
  FPMath *fpmath;
  int this_party;
  if (tid & 1) {
    this_party = 3 - party;
  } else {
    this_party = party;
  }
  fpmath = new FPMath(this_party, iopackArr[tid], otpackArr[tid]);
  FixArray input = fpmath->fix->input(this_party, num_ops, x, true, bw_x, s_x);
  FixArray output = fpmath->gelu_approx_3(input);
  memcpy(y, output.data, num_ops*sizeof(uint64_t));
  delete fpmath;
}

int main(int argc, char **argv) {
  /************* Argument Parsing  ************/
  /********************************************/
  ArgMapping amap;
  amap.arg("r", party, "Role of party: ALICE = 1; BOB = 2");
  amap.arg("p", port, "Port Number");
  amap.arg("N", dim, "Number of operation operations");
  amap.arg("nt", num_threads, "Number of threads");
  amap.arg("ip", address, "IP Address of server (ALICE)");

  amap.parse(argc, argv);

  assert(num_threads <= MAX_THREADS);

  /********** Setup IO and Base OTs ***********/
  /********************************************/
  for (int i = 0; i < num_threads; i++) {
    iopackArr[i] = new IOPack(party, port + i, address);
    if (i & 1) {
      otpackArr[i] = new OTPack(iopackArr[i], 3 - party);
    } else {
      otpackArr[i] = new OTPack(iopackArr[i], party);
    }
  }
  std::cout << "All Base OTs Done" << std::endl;


  PRG128 prg;

  uint64_t *x = new uint64_t[dim];
  uint64_t *y = new uint64_t[dim];

  prg.random_data(x, dim * sizeof(uint64_t));

  for (int i = 0; i < dim; i++) {
    x[i] &= mask_x;
  }

  uint64_t total_comm = 0;
   uint64_t total_round = 0;
  uint64_t thread_comm[num_threads];
  uint64_t thread_round[num_threads];
  for (int i = 0; i < num_threads; i++) {
    thread_comm[i] = iopackArr[i]->get_comm();
    thread_round[i] = iopackArr[i]->get_rounds();
  }

  auto start = clock_start();
  std::thread operation_threads[num_threads];
  int chunk_size = dim / num_threads;
  for (int i = 0; i < num_threads; ++i) {
    int offset = i * chunk_size;
    int lnum_ops;
    if (i == (num_threads - 1)) {
      lnum_ops = dim - offset;
    } else {
      lnum_ops = chunk_size;
    }
    operation_threads[i] =
        std::thread(operation_thread, i, x + offset, y + offset, lnum_ops);
  }
  for (int i = 0; i < num_threads; ++i) {
    operation_threads[i].join();
  }
  long long t = time_from(start);

  for (int i = 0; i < num_threads; i++) {
    thread_comm[i] = iopackArr[i]->get_comm() - thread_comm[i];
    thread_round[i] = iopackArr[i]->get_rounds() - thread_round[i];

    total_round += thread_round[i];
    total_comm += thread_comm[i];
  }


  cout << "Number of operation/s:\t" << (double(dim) / t) * 1e6 << std::endl;
  cout << "operation Time\t" << t / (1000.0) << " ms" << endl;
  cout << "operation Bytes Sent\t" << total_comm << " bytes" << endl;
  cout << "operation rounds\t" << total_round / 12 << " round" << endl;


  delete[] x;
  delete[] y;
  for (int i = 0; i < num_threads; i++) {
    delete iopackArr[i];
    delete otpackArr[i];
  }
}
