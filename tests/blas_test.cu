#include <stdio.h>
#include <stdlib.h>

#include <quda_internal.h>
#include <color_spinor_field.h>
#include <blas_quda.h>

#include <test_util.h>

// include because of nasty globals used in the tests
#include <dslash_util.h>

// google test
#include <gtest.h>

extern int test_type;
extern QudaPrecision prec;
extern QudaDslashType dslash_type;
extern QudaInverterType inv_type;
extern int nvec;
extern int device;
extern int xdim;
extern int ydim;
extern int zdim;
extern int tdim;
extern int gridsize_from_cmdline[];
extern int niter;

extern bool verify_results;
extern int Nsrc;
extern int Msrc;
extern QudaSolveType solve_type;

extern void usage(char** );

const int Nkernels = 42;

using namespace quda;

ColorSpinorField *xH, *yH, *zH, *wH, *vH, *hH, *lH;
ColorSpinorField *xD, *yD, *zD, *wD, *vD, *hD, *lD, *xmD, *ymD, *zmD;
std::vector<cpuColorSpinorField*> xmH;
std::vector<cpuColorSpinorField*> ymH;
std::vector<cpuColorSpinorField*> zmH;
int Nspin;
int Ncolor;

void setPrec(ColorSpinorParam &param, const QudaPrecision precision)
{
  param.precision = precision;
  if (Nspin == 1 || Nspin == 2 || precision == QUDA_DOUBLE_PRECISION) {
    param.fieldOrder = QUDA_FLOAT2_FIELD_ORDER;
  } else {
    param.fieldOrder = QUDA_FLOAT4_FIELD_ORDER;
  }
}

void
display_test_info()
{
  printfQuda("running the following test:\n");
  printfQuda("S_dimension T_dimension Nspin Ncolor\n");
  printfQuda("%3d /%3d / %3d   %3d      %d     %d\n", xdim, ydim, zdim, tdim, Nspin, Ncolor);
  printfQuda("Grid partition info:     X  Y  Z  T\n");
  printfQuda("                         %d  %d  %d  %d\n",
	     dimPartitioned(0),
	     dimPartitioned(1),
	     dimPartitioned(2),
	     dimPartitioned(3));
  return;
}

int Nprec = 3;

bool skip_kernel(int precision, int kernel) {
  // if we've selected a given kernel then make sure we only run that
  if (test_type != -1 && kernel != test_type) return true;

  // if we've selected a given precision then make sure we only run that
  QudaPrecision this_prec = precision == 2 ? QUDA_DOUBLE_PRECISION : precision  == 1 ? QUDA_SINGLE_PRECISION : QUDA_HALF_PRECISION;
  if (prec != QUDA_INVALID_PRECISION && this_prec != prec) return true;

  if ( Nspin == 2 && precision == 0) {
    // avoid half precision tests if doing coarse fields
    return true;
  } else if (Nspin == 2 && kernel == 1) {
    // avoid low-precision copy if doing coarse fields
    return true;
  } else if (Ncolor != 3 && (kernel == 31 || kernel == 32)) {
    // only benchmark heavy-quark norm if doing 3 colors
    return true;
  } else if ((Nprec < 3) && (kernel == 0)) {
    // only benchmark high-precision copy() if double is supported
    return true;
  }

  return false;
}

void initFields(int prec)
{
  // precisions used for the source field in the copyCuda() benchmark
  QudaPrecision high_aux_prec = QUDA_INVALID_PRECISION;
  QudaPrecision low_aux_prec = QUDA_INVALID_PRECISION;

  ColorSpinorParam param;
  param.nColor = Ncolor;
  param.nSpin = Nspin;
  param.nDim = 4; // number of spacetime dimensions

  param.pad = 0; // padding must be zero for cpu fields

  if (solve_type == QUDA_DIRECT_PC_SOLVE) {
    param.siteSubset = QUDA_PARITY_SITE_SUBSET;
  } else if (solve_type == QUDA_DIRECT_SOLVE) {
    param.siteSubset = QUDA_FULL_SITE_SUBSET;
  } else {
    errorQuda("Unexpected solve_type=%d\n", solve_type);
  }

  if (param.siteSubset == QUDA_PARITY_SITE_SUBSET) param.x[0] = xdim/2;
  else param.x[0] = xdim;
  param.x[1] = ydim;
  param.x[2] = zdim;
  param.x[3] = tdim;

  param.siteOrder = QUDA_EVEN_ODD_SITE_ORDER;
  param.gammaBasis = QUDA_DEGRAND_ROSSI_GAMMA_BASIS;
  param.precision = QUDA_DOUBLE_PRECISION;
  param.fieldOrder = QUDA_SPACE_SPIN_COLOR_FIELD_ORDER;

  param.create = QUDA_ZERO_FIELD_CREATE;

  vH = new cpuColorSpinorField(param);
  wH = new cpuColorSpinorField(param);
  xH = new cpuColorSpinorField(param);
  yH = new cpuColorSpinorField(param);
  zH = new cpuColorSpinorField(param);
  hH = new cpuColorSpinorField(param);
  lH = new cpuColorSpinorField(param);

// create composite fields

  // xmH = new cpuColorSpinorField(param);
  // ymH = new cpuColorSpinorField(param);



  xmH.reserve(Nsrc);
  for (int cid = 0; cid < Nsrc; cid++) xmH.push_back(new cpuColorSpinorField(param));
  ymH.reserve(Msrc);
  for (int cid = 0; cid < Msrc; cid++) ymH.push_back(new cpuColorSpinorField(param));
  zmH.reserve(Nsrc);
  for (int cid = 0; cid < Nsrc; cid++) zmH.push_back(new cpuColorSpinorField(param));


  static_cast<cpuColorSpinorField*>(vH)->Source(QUDA_RANDOM_SOURCE, 0, 0, 0);
  static_cast<cpuColorSpinorField*>(wH)->Source(QUDA_RANDOM_SOURCE, 0, 0, 0);
  static_cast<cpuColorSpinorField*>(xH)->Source(QUDA_RANDOM_SOURCE, 0, 0, 0);
  static_cast<cpuColorSpinorField*>(yH)->Source(QUDA_RANDOM_SOURCE, 0, 0, 0);
  static_cast<cpuColorSpinorField*>(zH)->Source(QUDA_RANDOM_SOURCE, 0, 0, 0);
  static_cast<cpuColorSpinorField*>(hH)->Source(QUDA_RANDOM_SOURCE, 0, 0, 0);
  static_cast<cpuColorSpinorField*>(lH)->Source(QUDA_RANDOM_SOURCE, 0, 0, 0);
  for(int i=0; i<Nsrc; i++){
    static_cast<cpuColorSpinorField*>(xmH[i])->Source(QUDA_RANDOM_SOURCE, 0, 0, 0);
  }
  for(int i=0; i<Msrc; i++){
    static_cast<cpuColorSpinorField*>(ymH[i])->Source(QUDA_RANDOM_SOURCE, 0, 0, 0);
  }
  // Now set the parameters for the cuda fields
  //param.pad = xdim*ydim*zdim/2;

  if (param.nSpin == 4) param.gammaBasis = QUDA_UKQCD_GAMMA_BASIS;
  param.create = QUDA_ZERO_FIELD_CREATE;

  switch(prec) {
  case 0:
    setPrec(param, QUDA_HALF_PRECISION);
    high_aux_prec = QUDA_DOUBLE_PRECISION;
    low_aux_prec = QUDA_SINGLE_PRECISION;
    break;
  case 1:
    setPrec(param, QUDA_SINGLE_PRECISION);
    high_aux_prec = QUDA_DOUBLE_PRECISION;
    low_aux_prec = QUDA_HALF_PRECISION;
    break;
  case 2:
    setPrec(param, QUDA_DOUBLE_PRECISION);
    high_aux_prec = QUDA_SINGLE_PRECISION;
    low_aux_prec = QUDA_HALF_PRECISION;
    break;
  default:
    errorQuda("Precision option not defined");
  }

  checkCudaError();

  vD = new cudaColorSpinorField(param);
  wD = new cudaColorSpinorField(param);
  xD = new cudaColorSpinorField(param);
  yD = new cudaColorSpinorField(param);
  zD = new cudaColorSpinorField(param);

  param.is_composite = true;
  param.is_component = false;

// create composite fields
  param.composite_dim = Nsrc;
  xmD = new cudaColorSpinorField(param);

  param.composite_dim = Msrc;
  ymD = new cudaColorSpinorField(param);

  param.composite_dim = Nsrc;
  zmD = new cudaColorSpinorField(param);

  param.is_composite = false;
  param.is_component = false;
  param.composite_dim = 1;

  setPrec(param, high_aux_prec);
  hD = new cudaColorSpinorField(param);

  setPrec(param, low_aux_prec);
  lD = new cudaColorSpinorField(param);

  // check for successful allocation
  checkCudaError();

  // only do copy if not doing half precision with mg
  bool flag = !(param.nSpin == 2 &&
		(prec == 0 || low_aux_prec == QUDA_HALF_PRECISION) );

  if ( flag ) {
    *vD = *vH;
    *wD = *wH;
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    *hD = *hH;
    *lD = *lH;
    // for (int i=0; i < Nsrc; i++){
    //   xmD->Component(i) = *(xmH[i]);
    //   ymD->Component(i) = *(ymH[i]);
    // }
    // *ymD = *ymH;
  }
}


void freeFields()
{

  // release memory
  delete vD;
  delete wD;
  delete xD;
  delete yD;
  delete zD;
  delete hD;
  delete lD;
  delete xmD;
  delete ymD;
  delete zmD;

  // release memory
  delete vH;
  delete wH;
  delete xH;
  delete yH;
  delete zH;
  delete hH;
  delete lH;
  for (int i=0; i < Nsrc; i++) delete xmH[i];
  for (int i=0; i < Msrc; i++) delete ymH[i];
  for (int i=0; i < Nsrc; i++) delete zmH[i];
  xmH.clear();
  ymH.clear();
  zmH.clear();
}


double benchmark(int kernel, const int niter) {

  double a, b, c;
  quda::Complex a2, b2, c2;
  quda::Complex * A = new quda::Complex[Nsrc*Msrc];
  quda::Complex * B = new quda::Complex[Nsrc*Msrc];
  quda::Complex * C = new quda::Complex[Nsrc*Msrc];
  quda::Complex * A2 = new quda::Complex[Nsrc*Nsrc]; // for the block cDotProductNorm test

  cudaEvent_t start, end;
  cudaEventCreate(&start);
  cudaEventCreate(&end);
  cudaEventRecord(start, 0);

  {
    switch (kernel) {

    case 0:
      for (int i=0; i < niter; ++i) blas::copy(*yD, *hD);
      break;

    case 1:
      for (int i=0; i < niter; ++i) blas::copy(*yD, *lD);
      break;

    case 2:
      for (int i=0; i < niter; ++i) blas::axpby(a, *xD, b, *yD);
      break;

    case 3:
      for (int i=0; i < niter; ++i) blas::xpy(*xD, *yD);
      break;

    case 4:
      for (int i=0; i < niter; ++i) blas::axpy(a, *xD, *yD);
      break;

    case 5:
      for (int i=0; i < niter; ++i) blas::xpay(*xD, a, *yD);
      break;

    case 6:
      for (int i=0; i < niter; ++i) blas::mxpy(*xD, *yD);
      break;

    case 7:
      for (int i=0; i < niter; ++i) blas::ax(a, *xD);
      break;

    case 8:
      for (int i=0; i < niter; ++i) blas::caxpy(a2, *xD, *yD);
      break;

    case 9:
      for (int i=0; i < niter; ++i) blas::caxpby(a2, *xD, b2, *yD);
      break;

    case 10:
      for (int i=0; i < niter; ++i) blas::cxpaypbz(*xD, a2, *yD, b2, *zD);
      break;

    case 11:
      for (int i=0; i < niter; ++i) blas::axpyBzpcx(a, *xD, *yD, b, *zD, c);
      break;

    case 12:
      for (int i=0; i < niter; ++i) blas::axpyZpbx(a, *xD, *yD, *zD, b);
      break;

    case 13:
      for (int i=0; i < niter; ++i) blas::caxpbypzYmbw(a2, *xD, b2, *yD, *zD, *wD);
      break;

    case 14:
      for (int i=0; i < niter; ++i) blas::cabxpyAx(a, b2, *xD, *yD);
      break;

    case 15:
      for (int i=0; i < niter; ++i) blas::caxpbypz(a2, *xD, b2, *yD, *zD);
      break;

    case 16:
      for (int i=0; i < niter; ++i) blas::caxpbypczpw(a2, *xD, b2, *yD, c2, *zD, *wD);
      break;

    case 17:
      for (int i=0; i < niter; ++i) blas::caxpyXmaz(a2, *xD, *yD, *zD);
      break;

      // double
    case 18:
      for (int i=0; i < niter; ++i) blas::norm2(*xD);
      break;

    case 19:
      for (int i=0; i < niter; ++i) blas::reDotProduct(*xD, *yD);
      break;

    case 20:
      for (int i=0; i < niter; ++i) blas::axpyNorm(a, *xD, *yD);
      break;

    case 21:
      for (int i=0; i < niter; ++i) blas::xmyNorm(*xD, *yD);
      break;

    case 22:
      for (int i=0; i < niter; ++i) blas::caxpyNorm(a2, *xD, *yD);
      break;

    case 23:
      for (int i=0; i < niter; ++i) blas::caxpyXmazNormX(a2, *xD, *yD, *zD);
      break;

    case 24:
      for (int i=0; i < niter; ++i) blas::cabxpyAxNorm(a, b2, *xD, *yD);
      break;

    // double2
    case 25:
      for (int i=0; i < niter; ++i) blas::cDotProduct(*xD, *yD);
      break;

    case 26:
      for (int i=0; i < niter; ++i) blas::xpaycDotzy(*xD, a, *yD, *zD);
      break;

    case 27:
      for (int i=0; i < niter; ++i) blas::caxpyDotzy(a2, *xD, *yD, *zD);
      break;

    // double3
    case 28:
      for (int i=0; i < niter; ++i) blas::cDotProductNormA(*xD, *yD);
      break;

    case 29:
      for (int i=0; i < niter; ++i) blas::cDotProductNormB(*xD, *yD);
      break;

    case 30:
      for (int i=0; i < niter; ++i) blas::caxpbypzYmbwcDotProductUYNormY(a2, *xD, b2, *yD, *zD, *wD, *vD);
      break;

    case 31:
      for (int i=0; i < niter; ++i) blas::HeavyQuarkResidualNorm(*xD, *yD);
      break;

    case 32:
      for (int i=0; i < niter; ++i) blas::xpyHeavyQuarkResidualNorm(*xD, *yD, *zD);
      break;

    case 33:
      for (int i=0; i < niter; ++i) blas::tripleCGReduction(*xD, *yD, *zD);
      break;

    case 34:
      for (int i=0; i < niter; ++i) blas::tripleCGUpdate(a, b, *xD, *yD, *zD, *wD);
      break;

    case 35:
      for (int i=0; i < niter; ++i) blas::axpyReDot(a, *xD, *yD);
      break;

    case 36:
      for (int i=0; i < niter; ++i) blas::caxpy(A, *xmD,* ymD);
      break;

    case 37:
      for (int i=0; i < niter; ++i) blas::axpyBzpcx((double*)A, xmD->Components(), zmD->Components(), (double*)B, *yD, (double*)C);
      break;

    case 38:
      for (int i=0; i < niter; ++i) blas::caxpyBxpz(a2, *xD, *yD, b2, *zD);
      break;

    case 39:
      for (int i=0; i < niter; ++i) blas::caxpyBzpx(a2, *xD, *yD, b2, *zD);
      break;

    case 40:
      for (int i=0; i < niter; ++i) blas::cDotProduct(A2, xmD->Components(), xmD->Components());
      break;

    case 41:
      for (int i=0; i < niter; ++i) blas::cDotProduct(A, xmD->Components(), ymD->Components());
      break;

    default:
      errorQuda("Undefined blas kernel %d\n", kernel);
    }
  }

  cudaEventRecord(end, 0);
  cudaEventSynchronize(end);
  float runTime;
  cudaEventElapsedTime(&runTime, start, end);
  cudaEventDestroy(start);
  cudaEventDestroy(end);
  delete[] A;
  delete[] B;
  delete[] C;
  delete[] A2;
  double secs = runTime / 1000;
  return secs;
}

#define ERROR(a) fabs(blas::norm2(*a##D) - blas::norm2(*a##H)) / blas::norm2(*a##H)

double test(int kernel) {

  double a = M_PI, b = M_PI*exp(1.0), c = sqrt(M_PI);
  quda::Complex a2(a, b), b2(b, -c), c2(a+b, c*a);
  double error = 0;
  quda::Complex * A = new quda::Complex[Nsrc*Msrc];
  quda::Complex * B = new quda::Complex[Nsrc*Msrc];
  quda::Complex * C = new quda::Complex[Nsrc*Msrc];
  quda::Complex * A2 = new quda::Complex[Nsrc*Nsrc]; // for the block cDotProductNorm test
  quda::Complex * B2 = new quda::Complex[Nsrc*Nsrc]; // for the block cDotProductNorm test
  for(int i=0; i < Nsrc*Msrc; i++){
    A[i] = a2*  (1.0*((i/Nsrc) + i)) + b2 * (1.0*i) + c2 *(1.0*(Nsrc*Msrc/2-i));
    B[i] = a2*  (1.0*((i/Nsrc) + i)) - b2 * (M_PI*i) + c2 *(1.0*(Nsrc*Msrc/2-i));
    C[i] = a2*  (1.0*((M_PI/Nsrc) + i)) + b2 * (1.0*i) + c2 *(1.0*(Nsrc*Msrc/2-i));
  }
  for(int i=0; i < Nsrc*Nsrc; i++){
    A2[i] = a2*  (1.0*((i/Nsrc) + i)) + b2 * (1.0*i) + c2 *(1.0*(Nsrc*Nsrc/2-i));
    B2[i] = a2*  (1.0*((i/Nsrc) + i)) - b2 * (M_PI*i) + c2 *(1.0*(Nsrc*Nsrc/2-i));
  }
  // A[0] = a2;
  // A[1] = 0.;
  // A[2] = 0.;
  // A[3] = 0.;

  switch (kernel) {

  case 0:
    *hD = *hH;
    blas::copy(*yD, *hD);
    blas::copy(*yH, *hH);
    error = ERROR(y);
    break;

  case 1:
    *lD = *lH;
    blas::copy(*yD, *lD);
    blas::copy(*yH, *lH);
    error = ERROR(y);
    break;

  case 2:
    *xD = *xH;
    *yD = *yH;
    blas::axpby(a, *xD, b, *yD);
    blas::axpby(a, *xH, b, *yH);
    error = ERROR(y);
    break;

  case 3:
    *xD = *xH;
    *yD = *yH;
    blas::xpy(*xD, *yD);
    blas::xpy(*xH, *yH);
    error = ERROR(y);
    break;

  case 4:
    *xD = *xH;
    *yD = *yH;
    blas::axpy(a, *xD, *yD);
    blas::axpy(a, *xH, *yH);
    *zH = *yD;
    error = ERROR(y);
    break;

  case 5:
    *xD = *xH;
    *yD = *yH;
    blas::xpay(*xD, a, *yD);
    blas::xpay(*xH, a, *yH);
    error = ERROR(y);
    break;

  case 6:
    *xD = *xH;
    *yD = *yH;
    blas::mxpy(*xD, *yD);
    blas::mxpy(*xH, *yH);
    error = ERROR(y);
    break;

  case 7:
    *xD = *xH;
    blas::ax(a, *xD);
    blas::ax(a, *xH);
    error = ERROR(x);
    break;

  case 8:
    *xD = *xH;
    *yD = *yH;
    blas::caxpy(a2, *xD, *yD);
    blas::caxpy(a2, *xH, *yH);
    error = ERROR(y);
    break;

  case 9:
    *xD = *xH;
    *yD = *yH;
    blas::caxpby(a2, *xD, b2, *yD);
    blas::caxpby(a2, *xH, b2, *yH);
    error = ERROR(y);
    break;

  case 10:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    blas::cxpaypbz(*xD, a2, *yD, b2, *zD);
    blas::cxpaypbz(*xH, a2, *yH, b2, *zH);
    error = ERROR(z);
    break;

  case 11:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    blas::axpyBzpcx(a, *xD, *yD, b, *zD, c);
    blas::axpyBzpcx(a, *xH, *yH, b, *zH, c);
    error = ERROR(x) + ERROR(y);
    break;

  case 12:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    blas::axpyZpbx(a, *xD, *yD, *zD, b);
    blas::axpyZpbx(a, *xH, *yH, *zH, b);
    error = ERROR(x) + ERROR(y);
    break;

  case 13:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    *wD = *wH;
    blas::caxpbypzYmbw(a2, *xD, b2, *yD, *zD, *wD);
    blas::caxpbypzYmbw(a2, *xH, b2, *yH, *zH, *wH);
    error = ERROR(z) + ERROR(y);
    break;

  case 14:
    *xD = *xH;
    *yD = *yH;
    blas::cabxpyAx(a, b2, *xD, *yD);
    blas::cabxpyAx(a, b2, *xH, *yH);
    error = ERROR(y) + ERROR(x);
    break;

  case 15:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    {blas::caxpbypz(a2, *xD, b2, *yD, *zD);
      blas::caxpbypz(a2, *xH, b2, *yH, *zH);
      error = ERROR(z); }
    break;

  case 16:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    *wD = *wH;
    {blas::caxpbypczpw(a2, *xD, b2, *yD, c2, *zD, *wD);
      blas::caxpbypczpw(a2, *xH, b2, *yH, c2, *zH, *wH);
      error = ERROR(w); }
    break;

  case 17:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    {blas::caxpyXmaz(a, *xD, *yD, *zD);
     blas::caxpyXmaz(a, *xH, *yH, *zH);
     error = ERROR(y) + ERROR(x);}
    break;

    // double
  case 18:
    *xD = *xH;
    *yH = *xD;
    error = fabs(blas::norm2(*xD) - blas::norm2(*xH)) / blas::norm2(*xH);
    break;

  case 19:
    *xD = *xH;
    *yD = *yH;
    error = fabs(blas::reDotProduct(*xD, *yD) - blas::reDotProduct(*xH, *yH)) / fabs(blas::reDotProduct(*xH, *yH));
    break;

  case 20:
    *xD = *xH;
    *yD = *yH;
    {double d = blas::axpyNorm(a, *xD, *yD);
    double h = blas::axpyNorm(a, *xH, *yH);
    error = ERROR(y) + fabs(d-h)/fabs(h);}
    break;

  case 21:
    *xD = *xH;
    *yD = *yH;
    {double d = blas::xmyNorm(*xD, *yD);
    double h = blas::xmyNorm(*xH, *yH);
    error = ERROR(y) + fabs(d-h)/fabs(h);}
    break;

  case 22:
    *xD = *xH;
    *yD = *yH;
    {double d = blas::caxpyNorm(a, *xD, *yD);
    double h = blas::caxpyNorm(a, *xH, *yH);
    error = ERROR(y) + fabs(d-h)/fabs(h);}
    break;

  case 23:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    {double d = blas::caxpyXmazNormX(a, *xD, *yD, *zD);
      double h = blas::caxpyXmazNormX(a, *xH, *yH, *zH);
      error = ERROR(y) + ERROR(x) + fabs(d-h)/fabs(h);}
    break;

  case 24:
    *xD = *xH;
    *yD = *yH;
    {double d = blas::cabxpyAxNorm(a, b2, *xD, *yD);
      double h = blas::cabxpyAxNorm(a, b2, *xH, *yH);
      error = ERROR(x) + ERROR(y) + fabs(d-h)/fabs(h);}
    break;

    // double2
  case 25:
    *xD = *xH;
    *yD = *yH;
    error = abs(blas::cDotProduct(*xD, *yD) - blas::cDotProduct(*xH, *yH)) / abs(blas::cDotProduct(*xH, *yH));
    break;

  case 26:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    { quda::Complex d = blas::xpaycDotzy(*xD, a, *yD, *zD);
      quda::Complex h = blas::xpaycDotzy(*xH, a, *yH, *zH);
      error =  fabs(blas::norm2(*yD) - blas::norm2(*yH)) / blas::norm2(*yH) + abs(d-h)/abs(h);
    }
    break;

  case 27:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    {quda::Complex d = blas::caxpyDotzy(a, *xD, *yD, *zD);
      quda::Complex h = blas::caxpyDotzy(a, *xH, *yH, *zH);
    error = ERROR(y) + abs(d-h)/abs(h);}
    break;

    // double3
  case 28:
    *xD = *xH;
    *yD = *yH;
    { double3 d = blas::cDotProductNormA(*xD, *yD);
      double3 h = blas::cDotProductNormA(*xH, *yH);
      error = fabs(d.x - h.x) / fabs(h.x) + fabs(d.y - h.y) / fabs(h.y) + fabs(d.z - h.z) / fabs(h.z); }
    break;

  case 29:
    *xD = *xH;
    *yD = *yH;
    { double3 d = blas::cDotProductNormB(*xD, *yD);
      double3 h = blas::cDotProductNormB(*xH, *yH);
      error = fabs(d.x - h.x) / fabs(h.x) + fabs(d.y - h.y) / fabs(h.y) + fabs(d.z - h.z) / fabs(h.z); }
    break;

  case 30:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    *wD = *wH;
    *vD = *vH;
    { double3 d = blas::caxpbypzYmbwcDotProductUYNormY(a2, *xD, b2, *yD, *zD, *wD, *vD);
      double3 h = blas::caxpbypzYmbwcDotProductUYNormY(a2, *xH, b2, *yH, *zH, *wH, *vH);
      error = ERROR(z) + ERROR(y) + fabs(d.x - h.x) / fabs(h.x) +
	fabs(d.y - h.y) / fabs(h.y) + fabs(d.z - h.z) / fabs(h.z); }
    break;

  case 31:
    *xD = *xH;
    *yD = *yH;
    { double3 d = blas::HeavyQuarkResidualNorm(*xD, *yD);
      double3 h = blas::HeavyQuarkResidualNorm(*xH, *yH);
      error = fabs(d.x - h.x) / fabs(h.x) +
	fabs(d.y - h.y) / fabs(h.y) + fabs(d.z - h.z) / fabs(h.z); }
    break;

  case 32:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    { double3 d = blas::xpyHeavyQuarkResidualNorm(*xD, *yD, *zD);
      double3 h = blas::xpyHeavyQuarkResidualNorm(*xH, *yH, *zH);
      error = ERROR(y) + fabs(d.x - h.x) / fabs(h.x) +
	fabs(d.y - h.y) / fabs(h.y) + fabs(d.z - h.z) / fabs(h.z); }
    break;

  case 33:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    { double3 d = blas::tripleCGReduction(*xD, *yD, *zD);
      double3 h = make_double3(blas::norm2(*xH), blas::norm2(*yH), blas::reDotProduct(*yH, *zH));
      error = fabs(d.x - h.x) / fabs(h.x) +
	fabs(d.y - h.y) / fabs(h.y) + fabs(d.z - h.z) / fabs(h.z); }
    break;

  case 34:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    *wD = *wH;
    { blas::tripleCGUpdate(a, b, *xD, *yD, *zD, *wD);
      blas::tripleCGUpdate(a, b, *xH, *yH, *zH, *wH);
      error = ERROR(y) + ERROR(z) + ERROR(w); }
    break;

  case 35:
    *xD = *xH;
    *yD = *yH;
    { double d = blas::axpyReDot(a, *xD, *yD);
      double h = blas::axpyReDot(a, *xH, *yH);
      error = ERROR(y) + fabs(d-h)/fabs(h); }
    break;

  case 36:
    for (int i=0; i < Nsrc; i++) xmD->Component(i) = *(xmH[i]);
    for (int i=0; i < Msrc; i++) ymD->Component(i) = *(ymH[i]);

    blas::caxpy(A, *xmD, *ymD);
    for (int i=0; i < Nsrc; i++){
      for(int j=0; j < Msrc; j++){
	blas::caxpy(A[Msrc*i+j], *(xmH[i]), *(ymH[j]));
      }
    }
    error = 0;
    for (int i=0; i < Msrc; i++){
      error+= fabs(blas::norm2((ymD->Component(i))) - blas::norm2(*(ymH[i]))) / blas::norm2(*(ymH[i]));
    }
    error/= Msrc;
    break;

  case 37:
    for (int i=0; i < Nsrc; i++) {
      xmD->Component(i) = *(xmH[i]);
      zmD->Component(i) = *(zmH[i]);
    }
    *yD = *yH;

    blas::axpyBzpcx((double*)A, xmD->Components(), zmD->Components(), (double*)B, *yD, (const double*)C);

    for (int i=0; i<Nsrc; i++) {
      blas::axpyBzpcx(((double*)A)[i], *xmH[i], *zmH[i], ((double*)B)[i], *yH, ((double*)C)[i]);
    }

    error = 0;
    for (int i=0; i < Nsrc; i++){
      error+= fabs(blas::norm2((xmD->Component(i))) - blas::norm2(*(xmH[i]))) / blas::norm2(*(xmH[i]));
      //error+= fabs(blas::norm2((zmD->Component(i))) - blas::norm2(*(zmH[i]))) / blas::norm2(*(zmH[i]));
    }
    error/= Nsrc;
    break;

  case 38:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    {blas::caxpyBxpz(a, *xD, *yD, b2, *zD);
     blas::caxpyBxpz(a, *xH, *yH, b2, *zH);
     error = ERROR(x) + ERROR(z);}
    break;

  case 39:
    *xD = *xH;
    *yD = *yH;
    *zD = *zH;
    {blas::caxpyBzpx(a, *xD, *yD, b2, *zD);
     blas::caxpyBzpx(a, *xH, *yH, b2, *zH);
     error = ERROR(x) + ERROR(z);}
    break;

  case 40:
    for (int i=0; i < Nsrc; i++) xmD->Component(i) = *(xmH[i]);
    blas::cDotProduct(A2, xmD->Components(), xmD->Components());
    error = 0.0;
    for (int i = 0; i < Nsrc; i++) {
      for (int j = 0; j < Nsrc; j++) {
	B2[i*Nsrc+j] = blas::cDotProduct(xmD->Component(i), xmD->Component(j));
	error += std::abs(A2[i*Nsrc+j] - B2[i*Nsrc+j])/std::abs(B2[i*Nsrc+j]);
      }
    }
    error /= Nsrc*Nsrc;
    break;

  case 41:
    for (int i=0; i < Nsrc; i++) xmD->Component(i) = *(xmH[i]);
    for (int i=0; i < Msrc; i++) ymD->Component(i) = *(ymH[i]);
    blas::cDotProduct(A, xmD->Components(), ymD->Components());
    error = 0.0;
    for (int i = 0; i < Nsrc; i++) {
      for (int j = 0; j < Msrc; j++) {
	B[i*Msrc+j] = blas::cDotProduct(xmD->Component(i), ymD->Component(j));
	error += std::abs(A[i*Msrc+j] - B[i*Msrc+j])/std::abs(B[i*Msrc+j]);
      }
    }
    error /= Nsrc*Msrc;
    break;

  default:
    errorQuda("Undefined blas kernel %d\n", kernel);
  }
  delete[] A;
  delete[] B;
  delete[] C;
  delete[] A2;
  delete[] B2;
  return error;
}

const char *prec_str[] = {"half", "single", "double"};


// For googletest names must be non-empty, unique, and may only contain ASCII
// alphanumeric characters or underscore
const char *names[] = {
  "copyHS",
  "copyLS",
  "axpby",
  "xpy",
  "axpy",
  "xpay",
  "mxpy",
  "ax",
  "caxpy",
  "caxpby",
  "cxpaypbz",
  "axpyBzpcx",
  "axpyZpbx",
  "caxpbypzYmbw",
  "cabxpyAx",
  "caxpbypz",
  "caxpbypczpw",
  "caxpyXmaz",
  "norm",
  "reDotProduct",
  "axpyNorm",
  "xmyNorm",
  "caxpyNorm",
  "caxpyXmazNormX",
  "cabxpyAxNorm",
  "cDotProduct",
  "xpaycDotzy",
  "caxpyDotzy",
  "cDotProductNormA",
  "cDotProductNormB",
  "caxpbypzYmbwcDotProductUYNormY",
  "HeavyQuarkResidualNorm",
  "xpyHeavyQuarkResidualNorm",
  "tripleCGReduction",
  "tripleCGUpdate",
  "axpyReDot",
  "caxpy_block",
  "axpyBzpcx_block",
  "caxpyBxpz",
  "caxpyBzpx",
  "cDotProductNorm_block",
  "cDotProduct_block",
  "caxpy_composite"
};

int main(int argc, char** argv)
{
  
  ::testing::InitGoogleTest(&argc, argv);
  int result = 0;

  prec = QUDA_INVALID_PRECISION;
  test_type = -1;

  for (int i = 1; i < argc; i++){
    if(process_command_line_option(argc, argv, &i) == 0){
      continue;
    }
    printfQuda("ERROR: Invalid option:%s\n", argv[i]);
    usage(argv);
  }

  // override spin setting if mg solver is set to test coarse grids
  if (inv_type == QUDA_MG_INVERTER) {
    Nspin = 2;
    Ncolor = nvec;
  } else {
    // set spin according to the type of dslash
    Nspin = (dslash_type == QUDA_ASQTAD_DSLASH ||
	     dslash_type == QUDA_STAGGERED_DSLASH) ? 1 : 4;
    Ncolor = 3;
  }

  setSpinorSiteSize(24);
  initComms(argc, argv, gridsize_from_cmdline);
  display_test_info();
  initQuda(device);

  setVerbosity(QUDA_SILENT);

  // clear the error state
  cudaGetLastError();

  // lastly check for correctness
  if (verify_results) {    
    result = RUN_ALL_TESTS();
  }

  endQuda();

  finalizeComms();
  return result;
}

// The following tests each kernel at each precision using the google testing framework

using ::testing::TestWithParam;
using ::testing::Bool;
using ::testing::Values;
using ::testing::Range;
using ::testing::Combine;

class BlasTest : public ::testing::TestWithParam<::testing::tuple<int, int>> {
protected:
  ::testing::tuple<int, int> param;

public:
  virtual ~BlasTest() { }
  virtual void SetUp() {
    param = GetParam();
    initFields(::testing::get<0>(GetParam()));
  }
  virtual void TearDown() { freeFields(); }

};


TEST_P(BlasTest, verify) {
  int prec = ::testing::get<0>(GetParam());
  int kernel = ::testing::get<1>(GetParam());

  // certain tests will fail to run for coarse grids so mark these as
  // failed without running
  double deviation =  skip_kernel(prec,kernel) ? 1.0 : test(kernel);
  // printfQuda("%-35s error = %e\n", names[kernel], deviation);
  double tol = (prec == 2 ? 1e-10 : (prec == 1 ? 1e-5 : 1e-3));
  tol = (kernel < 2) ? 1e-4 : tol; // use different tolerance for copy
  EXPECT_LE(deviation, tol) << "CPU and CUDA implementations do not agree";
}

TEST_P(BlasTest, benchmark) {
  int prec = ::testing::get<0>(GetParam());
  int kernel = ::testing::get<1>(GetParam());
// do the initial tune
      benchmark(kernel, 1);

      // now rerun with more iterations to get accurate speed measurements
      quda::blas::flops = 0;
      quda::blas::bytes = 0;

      double secs = benchmark(kernel, niter);

      double gflops = (quda::blas::flops*1e-9)/(secs);
      double gbytes = quda::blas::bytes/(secs*1e9);
      RecordProperty("Gflops", std::to_string(gflops));
      RecordProperty("GBs", std::to_string(gbytes));
      printfQuda("%-31s: Gflop/s = %6.1f, GB/s = %6.1f\n", names[kernel], gflops, gbytes);  
}


std::string getblasname(testing::TestParamInfo<::testing::tuple<int, int>> param){
   int prec = ::testing::get<0>(param.param);
   int kernel = ::testing::get<1>(param.param);
   std::string str(names[kernel]);
   str += std::string("_");
   str += std::string(prec_str[prec]);
   return str;//names[kernel] + "_" + prec_str[prec];
}

// half precision
INSTANTIATE_TEST_CASE_P(QUDA, BlasTest, Combine( Range(0,3), Range(0, Nkernels) ), getblasname);

