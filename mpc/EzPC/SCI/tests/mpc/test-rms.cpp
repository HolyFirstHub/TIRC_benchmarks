

#include "FloatingPoint/fp-math.h"
#include "BuildingBlocks/aux-protocols.h"
#include "NonLinear/argmax.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <cmath>
#include <vector>

using namespace sci;
using namespace std;

#define MAX_THREADS 16

int party, port = 32000;
int num_threads = 16;
string address = "127.0.0.1";

int32_t dim = 16;
int32_t array_size = 2048;
int32_t bw_x = 37;
int32_t bw_y = 37;
int32_t s_x = 12;
int32_t s_y = 12;
int32_t input_size = dim*array_size;

bool signed_ = true;

uint64_t mask_x = (bw_x == 64 ? -1 : ((1ULL << 14) - 1));
uint64_t mask_y = (bw_y == 64 ? -1 : ((1ULL << 14) - 1));

IOPack *iopackArr[MAX_THREADS];
OTPack *otpackArr[MAX_THREADS];

void operation_thread(int tid, uint64_t *x, uint64_t *y, int num_ops) {
  FPMath *fpmath;
  // ArgMaxProtocol<uint64_t> *argmax_oracle;
  int this_party;
  if (tid & 1) {
    this_party = 3 - party;
  } else {
    this_party = party;
  }
  fpmath = new FPMath(this_party, iopackArr[tid], otpackArr[tid]);
  vector<FixArray> input_array;
  for(int i = 0; i < num_ops; i++){
    input_array.push_back(fpmath->fix->input(this_party, array_size, &x[i*array_size], true, bw_x, s_x));
  }
  FixArray w = fpmath->fix->input(this_party, num_ops*array_size, 1, true, bw_x, s_x);
  FixArray b = fpmath->fix->input(this_party, num_ops*array_size, 1, true, bw_x, s_x);
  vector<FixArray> output_array = fpmath->rms_norm_fix(input_array, w, b);
  for(int i = 0; i < num_ops; i++){
    memcpy(&y[i*array_size], output_array[i].data, array_size * sizeof(uint64_t));
  }
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

  /************ Generate Test Data ************/
  /********************************************/

  // char fix_key[] = "\x61\x7e\xcd\xa2\xa0\x51\x1e\x96"
  //                      "\x5e\x41\xc2\x9b\x15\x3f\xc7\x7a";

  PRG128 prg(fix_key);

  uint64_t *x = new uint64_t[input_size];
  uint64_t *y = new uint64_t[input_size];

  prg.random_data(x, dim * array_size * sizeof(uint64_t));

  for (int i = 0; i < dim; i++) {
    for(int j=0;j < array_size;j++)
       x[i*array_size+j] &= mask_x;
  }

  /************** Fork Threads ****************/
  /********************************************/
  uint64_t total_comm = 0;
  uint64_t thread_comm[num_threads];
  for (int i = 0; i < num_threads; i++) {
    thread_comm[i] = iopackArr[i]->get_comm();
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
        std::thread(operation_thread, i, &x[offset*array_size], &y[offset*array_size], lnum_ops);
  }
  for (int i = 0; i < num_threads; ++i) {
    operation_threads[i].join();
  }
  long long t = time_from(start);

  for (int i = 0; i < num_threads; i++) {
    thread_comm[i] = iopackArr[i]->get_comm() - thread_comm[i];
    total_comm += thread_comm[i];
  }

  cout << "Number of operation/s:\t" << (double(dim) / t) * 1e6 << std::endl;
  cout << "operation Time\t" << t / (1000.0) << " ms" << endl;
  cout << "operation Bytes Sent\t" << total_comm << " bytes" << endl;

  /******************* Cleanup ****************/
  /********************************************/
  delete[] x;
  delete[] y;
  for (int i = 0; i < num_threads; i++) {
    delete iopackArr[i];
    delete otpackArr[i];
  }
}
