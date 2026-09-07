// STL-free CUDA translation unit. Kept deliberately free of the GPUDirectFabric
// headers so the public C++20 core is never contaminated with nvcc-specific code.
#include <cuda_runtime.h>

extern "C" {

int gdf_cuda_count(int* count) { return static_cast<int>(cudaGetDeviceCount(count)); }

int gdf_cuda_props(int index, char* name, int name_len, int* cc_major, int* cc_minor,
                   unsigned long long* total_mem) {
  cudaDeviceProp p{};
  int r = static_cast<int>(cudaGetDeviceProperties(&p, index));
  if (r != 0) return r;
  if (name != nullptr && name_len > 0) {
    int i = 0;
    for (; i < name_len - 1 && p.name[i] != 0; ++i) name[i] = p.name[i];
    name[i] = 0;
  }
  if (cc_major) *cc_major = p.major;
  if (cc_minor) *cc_minor = p.minor;
  if (total_mem) *total_mem = static_cast<unsigned long long>(p.totalGlobalMem);
  return 0;
}

int gdf_cuda_malloc(void** ptr, unsigned long long n) {
  return static_cast<int>(cudaMalloc(ptr, static_cast<size_t>(n)));
}
int gdf_cuda_free(void* ptr) { return static_cast<int>(cudaFree(ptr)); }

int gdf_cuda_malloc_host(void** ptr, unsigned long long n) {
  return static_cast<int>(cudaMallocHost(ptr, static_cast<size_t>(n)));
}
int gdf_cuda_free_host(void* ptr) { return static_cast<int>(cudaFreeHost(ptr)); }

int gdf_cuda_h2d(void* dst, const void* src, unsigned long long n) {
  return static_cast<int>(cudaMemcpy(dst, src, static_cast<size_t>(n), cudaMemcpyHostToDevice));
}
int gdf_cuda_d2h(void* dst, const void* src, unsigned long long n) {
  return static_cast<int>(cudaMemcpy(dst, src, static_cast<size_t>(n), cudaMemcpyDeviceToHost));
}
int gdf_cuda_sync() { return static_cast<int>(cudaDeviceSynchronize()); }

int gdf_cuda_meminfo(unsigned long long* free_bytes, unsigned long long* total_bytes) {
  size_t f = 0, t = 0;
  int r = static_cast<int>(cudaMemGetInfo(&f, &t));
  if (free_bytes) *free_bytes = static_cast<unsigned long long>(f);
  if (total_bytes) *total_bytes = static_cast<unsigned long long>(t);
  return r;
}

__global__ void gdf_invert_kernel(unsigned char* p, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) p[i] = static_cast<unsigned char>(~p[i]);
}

int gdf_cuda_invert(void* p, unsigned long long n) {
  int bytes = static_cast<int>(n);
  int blocks = (bytes + 255) / 256;
  gdf_invert_kernel<<<blocks, 256>>>(static_cast<unsigned char*>(p), bytes);
  return static_cast<int>(cudaGetLastError());
}

}  // extern "C"
