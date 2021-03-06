#include <tune_quda.h>
#include <uint_to_char.h>
#include <quda_internal.h>

// if this macro is defined then we use the driver API, else use the
// runtime API.  Typically the driver API has 10-20% less overhead
#define USE_DRIVER_API

// if this macro is defined then we profile the CUDA API calls
//#define API_PROFILE

#ifdef API_PROFILE
#define PROFILE(f, idx)                                 \
  apiTimer.TPSTART(idx);				\
  f;                                                    \
  apiTimer.TPSTOP(idx);
#else
#define PROFILE(f, idx) f;
#endif

namespace quda {

#ifdef USE_DRIVER_API
  static TimeProfile apiTimer("CUDA API calls (driver)");
#else
  static TimeProfile apiTimer("CUDA API calls (runtime)");
#endif

  class QudaMemCopy : public Tunable {

    void *dst;
    const void *src;
    const size_t count;
    const cudaMemcpyKind kind;
    const char *name;

    unsigned int sharedBytesPerThread() const { return 0; }
    unsigned int sharedBytesPerBlock(const TuneParam &param) const { return 0; }

  public:
    inline QudaMemCopy(void *dst, const void *src, size_t count, cudaMemcpyKind kind,
		       const char *func, const char *file, const char *line)
      : dst(dst), src(src), count(count), kind(kind) {

      switch(kind) {
      case cudaMemcpyDeviceToHost:
	name = "cudaMemcpyDeviceToHost";
	break;
      case cudaMemcpyHostToDevice:
	name = "cudaMemcpyHostToDevice";
	break;
      case cudaMemcpyHostToHost:
	name = "cudaMemcpyHostToHost";
	break;
      case cudaMemcpyDeviceToDevice:
	name = "cudaMemcpyDeviceToDevice";
	break;
      case cudaMemcpyDefault:
        name = "cudaMemcpyDefault";
        break;
      default:
	errorQuda("Unsupported cudaMemcpyType %d", kind);
      }
      strcpy(aux, func);
      strcat(aux, ",");
      strcat(aux, file);
      strcat(aux, ",");
      strcat(aux, line);
    }

    virtual ~QudaMemCopy() { }

    inline void apply(const cudaStream_t &stream) {
      tuneLaunch(*this, getTuning(), getVerbosity());
#ifdef USE_DRIVER_API
      switch(kind) {
      case cudaMemcpyDeviceToHost:
        cuMemcpyDtoH(dst, (CUdeviceptr)src, count);
	break;
      case cudaMemcpyHostToDevice:
        cuMemcpyHtoD((CUdeviceptr)dst, src, count);
	break;
      case cudaMemcpyHostToHost:
        memcpy(dst, src, count);
	break;
      case cudaMemcpyDeviceToDevice:
        cuMemcpyDtoD((CUdeviceptr)dst, (CUdeviceptr)src, count);
	break;
      case cudaMemcpyDefault:
        cuMemcpy((CUdeviceptr)dst, (CUdeviceptr)src, count);
      default:
	errorQuda("Unsupported cudaMemcpyType %d", kind);
      }
#else
      cudaMemcpy(dst, src, count, kind);
#endif
    }

    bool advanceTuneParam(TuneParam &param) const { return false; }

    TuneKey tuneKey() const {
      char vol[128];
      strcpy(vol,"bytes=");
      u64toa(vol+6, (uint64_t)count);
      return TuneKey(vol, name, aux);
    }

    long long flops() const { return 0; }
    long long bytes() const { return kind == cudaMemcpyDeviceToDevice ? 2*count : count; }

  };

  void qudaMemcpy_(void *dst, const void *src, size_t count, cudaMemcpyKind kind,
                   const char *func, const char *file, const char *line) {
    if (getVerbosity() == QUDA_DEBUG_VERBOSE)
      printfQuda("%s bytes = %llu\n", __func__, (long long unsigned int)count);

    if (count == 0) return;
#if 1
    QudaMemCopy copy(dst, src, count, kind, func, file, line);
    copy.apply(0);
#else
    cudaMemcpy(dst, src, count, kind);
#endif
    checkCudaError();
  }

  void qudaMemcpyAsync_(void *dst, const void *src, size_t count, cudaMemcpyKind kind, const cudaStream_t &stream,
                        const char *func, const char *file, const char *line)
  {
#ifdef USE_DRIVER_API
    switch (kind) {
    case cudaMemcpyDeviceToHost:
      PROFILE(cuMemcpyDtoHAsync(dst, (CUdeviceptr)src, count, stream), QUDA_PROFILE_MEMCPY_D2H_ASYNC);
      break;
    case cudaMemcpyHostToDevice:
      PROFILE(cuMemcpyHtoDAsync((CUdeviceptr)dst, src, count, stream), QUDA_PROFILE_MEMCPY_H2D_ASYNC);
      break;
    case cudaMemcpyDeviceToDevice:
      PROFILE(cuMemcpyDtoDAsync((CUdeviceptr)dst, (CUdeviceptr)src, count, stream), QUDA_PROFILE_MEMCPY_D2D_ASYNC);
      break;
    default:
      errorQuda("Unsupported cuMemcpyTypeAsync %d", kind);
    }
#else
    PROFILE(cudaMemcpyAsync(dst, src, count, kind, stream),
            kind == cudaMemcpyDeviceToHost ? QUDA_PROFILE_MEMCPY_D2H_ASYNC : QUDA_PROFILE_MEMCPY_H2D_ASYNC);
#endif
  }

  void qudaMemcpy2DAsync_(void *dst, size_t dpitch, const void *src, size_t spitch,
                          size_t width, size_t height, cudaMemcpyKind kind, const cudaStream_t &stream,
                          const char *func, const char *file, const char *line)
  {
#ifdef USE_DRIVER_API
    CUDA_MEMCPY2D param;
    param.srcPitch = spitch;
    param.srcY = 0;
    param.srcXInBytes = 0;
    param.dstPitch = dpitch;
    param.dstY = 0;
    param.dstXInBytes = 0;
    param.WidthInBytes = width;
    param.Height = height;

    switch (kind) {
    case cudaMemcpyDeviceToHost:
      param.srcDevice = (CUdeviceptr)src;
      param.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      param.dstHost = dst;
      param.dstMemoryType = CU_MEMORYTYPE_HOST;
      break;
    default:
      errorQuda("Unsupported cuMemcpyType2DAsync %d", kind);
    }
    PROFILE(cuMemcpy2DAsync(&param, stream), QUDA_PROFILE_MEMCPY2D_D2H_ASYNC);
#else
    PROFILE(cudaMemcpy2DAsync(dst, dpitch, src, spitch, width, height, kind, stream), QUDA_PROFILE_MEMCPY2D_D2H_ASYNC);
#endif
  }

  cudaError_t qudaLaunchKernel(const void* func, dim3 gridDim, dim3 blockDim, void** args, size_t sharedMem, cudaStream_t stream)
  {
    // no driver API variant here since we have C++ functions
    PROFILE(cudaError_t error = cudaLaunchKernel(func, gridDim, blockDim, args, sharedMem, stream), QUDA_PROFILE_LAUNCH_KERNEL);
    if (error != cudaSuccess && !activeTuning()) errorQuda("(CUDA) %s", cudaGetErrorString(error));
    return error;
  }

  cudaError_t qudaEventQuery(cudaEvent_t &event)
  {
#ifdef USE_DRIVER_API
    PROFILE(CUresult error = cuEventQuery(event), QUDA_PROFILE_EVENT_QUERY);
    switch (error) {
    case CUDA_SUCCESS:
      return cudaSuccess;
    case CUDA_ERROR_NOT_READY: // this is the only return value care about
      return cudaErrorNotReady;
    default:
      errorQuda("cuEventQuery return error code %d", error);
    }
    return cudaErrorUnknown;
#else
    PROFILE(cudaError_t error = cudaEventQuery(event), QUDA_PROFILE_EVENT_QUERY);
    return error;
#endif
  }

  cudaError_t qudaEventRecord(cudaEvent_t &event, cudaStream_t stream)
  {
#ifdef USE_DRIVER_API
    PROFILE(CUresult error = cuEventRecord(event, stream), QUDA_PROFILE_EVENT_RECORD);
    switch (error) {
    case CUDA_SUCCESS:
      return cudaSuccess;
    default: // should always return successful
      errorQuda("cuEventRecord return error code %d", error);
    }
    return cudaErrorUnknown;
#else
    PROFILE(cudaError_t error = cudaEventRecord(event, stream), QUDA_PROFILE_EVENT_RECORD);
    return error;
#endif
  }

  cudaError_t qudaStreamWaitEvent(cudaStream_t stream, cudaEvent_t event, unsigned int flags)
  {
#ifdef USE_DRIVER_API
    PROFILE(CUresult error = cuStreamWaitEvent(stream, event, flags), QUDA_PROFILE_STREAM_WAIT_EVENT);
    switch (error) {
    case CUDA_SUCCESS:
      return cudaSuccess;
    default: // should always return successful
      errorQuda("cuStreamWaitEvent return error code %d", error);
    }
    return cudaErrorUnknown;
#else
    PROFILE(cudaError_t error = cudaStreamWaitEvent(stream, event, flags), QUDA_PROFILE_STREAM_WAIT_EVENT);
    return error;
#endif
  }

  cudaError_t qudaStreamSynchronize(cudaStream_t &stream)
  {
#ifdef USE_DRIVER_API
    PROFILE(CUresult error = cuStreamSynchronize(stream), QUDA_PROFILE_STREAM_SYNCHRONIZE);
    switch (error) {
    case CUDA_SUCCESS:
      return cudaSuccess;
    default: // should always return successful
      errorQuda("cuStreamSynchronize return error code %d", error);
    }
    return cudaErrorUnknown;
#else
    PROFILE(cudaError_t error = cudaStreamSynchronize(stream), QUDA_PROFILE_STREAM_SYNCHRONIZE);
    return error;
#endif
  }

  cudaError_t qudaEventSynchronize(cudaEvent_t &event)
  {
#ifdef USE_DRIVER_API
    PROFILE(CUresult error = cuEventSynchronize(event), QUDA_PROFILE_EVENT_SYNCHRONIZE);
    switch (error) {
    case CUDA_SUCCESS:
      return cudaSuccess;
    default: // should always return successful
      errorQuda("cuEventSynchronize return error code %d", error);
    }
    return cudaErrorUnknown;
#else
    PROFILE(cudaError_t error = cudaEventSynchronize(event), QUDA_PROFILE_EVENT_SYNCHRONIZE);
    return error;
#endif
  }

  cudaError_t qudaDeviceSynchronize()
  {
#ifdef USE_DRIVER_API
    PROFILE(CUresult error = cuCtxSynchronize(), QUDA_PROFILE_DEVICE_SYNCHRONIZE);
    switch (error) {
    case CUDA_SUCCESS:
      return cudaSuccess;
    default: // should always return successful
      errorQuda("cuCtxSynchronize return error code %d", error);
    }
    return cudaErrorUnknown;
#else
    PROFILE(cudaError_t error = cudaDeviceSynchronize(), QUDA_PROFILE_DEVICE_SYNCHRONIZE);
    return error;
#endif
  }

#if (CUDA_VERSION >= 9000)
  cudaError_t qudaFuncSetAttribute(const void* func, cudaFuncAttribute attr, int value)
  {
    // no driver API variant here since we have C++ functions
    PROFILE(cudaError_t error = cudaFuncSetAttribute(func, attr, value), QUDA_PROFILE_FUNC_SET_ATTRIBUTE);
    return error;
  }
#endif

  void printAPIProfile() {
#ifdef API_PROFILE
    apiTimer.Print();
#endif
  }

} // namespace quda
