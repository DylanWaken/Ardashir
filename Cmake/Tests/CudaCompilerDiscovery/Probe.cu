#include <cuda_runtime.h>

__global__ void Probe(int* Value) { *Value = 42; }

// Compilation/linking needs a toolkit, but this test needs no GPU or driver.
int main() { return 0; }
