#include <blas_magma.h>
#include <string.h>

#include <vector>
#include <algorithm>

#ifndef MAX
#define MAX(a, b) (a > b) ? a : b;
#endif

#define MAGMA_15 //version of the MAGMA library

#ifdef MAGMA_LIB
#include <magma.h>

#if (defined MAGMA_14)

#define _cV 'V' 
#define _cU 'U'

#define _cR 'R'
#define _cL 'L'

#define _cC 'C'
#define _cN 'N'

#elif (defined MAGMA_15)

#define _cV MagmaVec 
#define _cU MagmaUpper

#define _cR MagmaRight
#define _cL MagmaLeft

#define _cC MagmaConjTrans
#define _cN MagmaNoTrans

#endif

#endif

void BlasMagmaArgs::OpenMagma(){ 

#ifdef MAGMA_LIB
    magma_int_t err = magma_init(); 

    if(err != MAGMA_SUCCESS) printf("\nError: cannot initialize MAGMA library\n");

    int major, minor, micro;

    magma_version( &major, &minor, &micro);
    printf("\nMAGMA library version: %d.%d\n\n", major,  minor);
#else
    printf("\nError: MAGMA library was not compiled, check your compilation options...\n");
    exit(-1);
#endif    

    return;
}

void BlasMagmaArgs::CloseMagma(){  

#ifdef MAGMA_LIB
    if(magma_finalize() != MAGMA_SUCCESS) printf("\nError: cannot close MAGMA library\n");
#else
    printf("\nError: MAGMA library was not compiled, check your compilation options...\n");
    exit(-1);
#endif    

    return;
}

 BlasMagmaArgs::BlasMagmaArgs(const int prec) : m(0), max_nev(0), prec(prec), ldm(0), info(-1), llwork(0), 
  lrwork(0), liwork(0), sideLR(0), htsize(0), dtsize(0), lwork_max(0), W(0), W2(0), 
  hTau(0), dTau(0), lwork(0), rwork(0), iwork(0)
{

#ifdef MAGMA_LIB
    magma_int_t dev_info = magma_getdevice_arch();//mostly to check whether magma is intialized...
    if(dev_info == 0)  exit(-1);

    printf("\nMAGMA will use device architecture %d.\n", dev_info);

    alloc = false;
    init  = true;
#else
    printf("\nError: MAGMA library was not compiled, check your compilation options...\n");
    exit(-1);
#endif    

    return;
}


BlasMagmaArgs::BlasMagmaArgs(const int m, const int ldm, const int prec) 
  : m(m), max_nev(0),  prec(prec), ldm(ldm), info(-1), sideLR(0), htsize(0), dtsize(0), 
  W(0), hTau(0), dTau(0)
{

#ifdef MAGMA_LIB

    magma_int_t dev_info = magma_getdevice_arch();//mostly to check whether magma is intialized...

    if(dev_info == 0)  exit(-1);

    printf("\nMAGMA will use device architecture %d.\n", dev_info);

    const int complex_prec = 2*prec;

    magma_int_t nbtrd = prec == 4 ? magma_get_chetrd_nb(m) : magma_get_zhetrd_nb(m);//ldm

    llwork = MAX(m + m*nbtrd, 2*m + m*m);//ldm 
    lrwork = 1 + 5*m + 2*m*m;//ldm
    liwork = 3 + 5*m;//ldm

    magma_malloc_pinned((void**)&W2,   ldm*m*complex_prec);
    magma_malloc_pinned((void**)&lwork, llwork*complex_prec);
    magma_malloc_cpu((void**)&rwork,    lrwork*prec);
    magma_malloc_cpu((void**)&iwork,    liwork*sizeof(magma_int_t));

    init  = true;
    alloc = true;

#else
    printf("\nError: MAGMA library was not compiled, check your compilation options...\n");
    exit(-1);
#endif    

    return;
}



BlasMagmaArgs::BlasMagmaArgs(const int m, const int max_nev, const int ldm, const int prec) 
  : m(m), max_nev(max_nev),  prec(prec), ldm(ldm), info(-1)
{

#ifdef MAGMA_LIB

    magma_int_t dev_info = magma_getdevice_arch();//mostly to check whether magma is intialized...

    if(dev_info == 0)  exit(-1);

    printf("\nMAGMA will use device architecture %d.\n", dev_info);

    const int complex_prec = 2*prec;

    magma_int_t nbtrd = prec == 4 ? magma_get_chetrd_nb(ldm) : magma_get_zhetrd_nb(ldm);//ldm<-m
    magma_int_t nbqrf = prec == 4 ? magma_get_cgeqrf_nb(ldm) : magma_get_zgeqrf_nb(ldm);//ldm

    htsize   = max_nev;//MIN(l,k)-number of Householder vectors, but we always have k <= MIN(m,n)
    dtsize   = ( 2*htsize + ((htsize + 31)/32)*32 )*nbqrf;//in general: MIN(m,k) for side = 'L' and MIN(n,k) for side = 'R'

    magma_malloc_pinned((void**)&hTau, htsize*complex_prec);
    magma_malloc((void**)&dTau,        dtsize*complex_prec);

//these are needed for the eigCG solver only.
    sideLR = (m - max_nev + nbqrf)*(m + nbqrf) + m*nbqrf;//ldm

    magma_malloc_pinned((void**)&W,    sideLR*complex_prec);
    magma_malloc_pinned((void**)&W2,   ldm*m*complex_prec);

    llwork = MAX(m + m*nbtrd, 2*m + m*m);//ldm 
    lrwork = 1 + 5*m + 2*m*m;//ldm
    liwork = 3 + 5*m;//ldm

    magma_malloc_pinned((void**)&lwork, llwork*complex_prec);
    magma_malloc_cpu((void**)&rwork,    lrwork*prec);
    magma_malloc_cpu((void**)&iwork,    liwork*sizeof(magma_int_t));

    init  = true;
    alloc = true;

#else
    printf("\nError: MAGMA library was not compiled, check your compilation options...\n");
    exit(-1);
#endif    

    return;
}

BlasMagmaArgs::~BlasMagmaArgs()
{
#ifdef MAGMA_LIB

   if(alloc == true)
   {
     if(dTau) magma_free(dTau);
     if(hTau) magma_free_pinned(hTau);

     if(W) magma_free_pinned(W);
     if(W2) magma_free_pinned(W2);
     if(lwork) magma_free_pinned(lwork);

     if(rwork) magma_free_cpu(rwork);
     if(iwork) magma_free_cpu(iwork);

     alloc = false;
   }

   init  = false;

#endif

   return;
}

void BlasMagmaArgs::MagmaHEEVD(void *dTvecm, void *hTvalm, const int prob_size, bool host)
{
     if(prob_size > m) printf("\nError in MagmaHEEVD (problem size cannot exceed given search space %d), exit ...\n", m), exit(-1);
   
#ifdef MAGMA_LIB
     cudaPointerAttributes ptr_attr;

     if(!host)
     {
       //check if dTvecm is a device pointer..
       cudaPointerGetAttributes(&ptr_attr, dTvecm);

       if(ptr_attr.memoryType != cudaMemoryTypeDevice || ptr_attr.devicePointer == NULL ) printf("Error in MagmaHEEVD, no device pointer found."), exit(-1);

       if(prec == 4)
       {
         magma_cheevd_gpu(_cV, _cU, prob_size, (magmaFloatComplex*)dTvecm, ldm, (float*)hTvalm, (magmaFloatComplex*)W2, ldm, (magmaFloatComplex*)lwork, llwork, (float*)rwork, lrwork, iwork, liwork, &info);
         if(info != 0) printf("\nError in MagmaHEEVD (magma_cheevd_gpu), exit ...\n"), exit(-1);
       }
       else
       {
         magma_zheevd_gpu(_cV, _cU, prob_size, (magmaDoubleComplex*)dTvecm, ldm, (double*)hTvalm, (magmaDoubleComplex*)W2, ldm, (magmaDoubleComplex*)lwork, llwork, (double*)rwork, lrwork, iwork, liwork, &info);
         if(info != 0) printf("\nError in MagmaHEEVD (magma_zheevd_gpu), exit ...\n"), exit(-1);
       }
     }
     else
     {
       //check if dTvecm is a device pointer..
       cudaPointerGetAttributes(&ptr_attr, dTvecm);

       if(ptr_attr.memoryType != cudaMemoryTypeHost || ptr_attr.hostPointer == NULL ) printf("Error in MagmaHEEVD, no host pointer found."), exit(-1);

       if(prec == 4)
       {
         magma_cheevd(_cV, _cU, prob_size, (magmaFloatComplex*)dTvecm, ldm, (float*)hTvalm, (magmaFloatComplex*)lwork, llwork, (float*)rwork, lrwork, iwork, liwork, &info);
         if(info != 0) printf("\nError in MagmaHEEVD (magma_cheevd_gpu), exit ...\n"), exit(-1);
       }
       else
       {
         magma_zheevd(_cV, _cU, prob_size, (magmaDoubleComplex*)dTvecm, ldm, (double*)hTvalm, (magmaDoubleComplex*)lwork, llwork, (double*)rwork, lrwork, iwork, liwork, &info);
         if(info != 0) printf("\nError in MagmaHEEVD (magma_zheevd_gpu), exit ...\n"), exit(-1);
       }
     }   
#endif
  return;
}  

int BlasMagmaArgs::MagmaORTH_2nev(void *dTvecm, void *dTm)
{
     const int l = max_nev;

#ifdef MAGMA_LIB
     if(prec == 4)
     {
        magma_int_t nb = magma_get_cgeqrf_nb(m);//ldm

        magma_cgeqrf_gpu(m, l, (magmaFloatComplex *)dTvecm, ldm, (magmaFloatComplex *)hTau, (magmaFloatComplex *)dTau, &info);
        if(info != 0) printf("\nError in MagmaORTH_2nev (magma_cgeqrf_gpu), exit ...\n"), exit(-1);

        //compute dTevecm0=QHTmQ
        //get TQ product:
        magma_cunmqr_gpu(_cR, _cN, m, m, l, (magmaFloatComplex *)dTvecm, ldm, (magmaFloatComplex *)hTau, (magmaFloatComplex *)dTm, ldm, (magmaFloatComplex *)W, sideLR, (magmaFloatComplex *)dTau, nb, &info); 
        if(info != 0) printf("\nError in MagmaORTH_2nev (magma_cunmqr_gpu), exit ...\n"), exit(-1);
             	
        //get QHT product:
        magma_cunmqr_gpu(_cL, _cC, m, l, l, (magmaFloatComplex *)dTvecm, ldm, (magmaFloatComplex *)hTau, (magmaFloatComplex *)dTm, ldm, (magmaFloatComplex *)W, sideLR, (magmaFloatComplex *)dTau, nb, &info);
        if(info != 0) printf("\nError in MagmaORTH_2nev (magma_cunmqr_gpu), exit ...\n"), exit(-1);  
     }
     else
     {
        magma_int_t nb = magma_get_zgeqrf_nb(m);//ldm

        magma_zgeqrf_gpu(m, l, (magmaDoubleComplex *)dTvecm, ldm, (magmaDoubleComplex *)hTau, (magmaDoubleComplex *)dTau, &info);
        if(info != 0) printf("\nError in MagmaORTH_2nev (magma_zgeqrf_gpu), exit ...\n"), exit(-1);

        //compute dTevecm0=QHTmQ
        //get TQ product:
        magma_zunmqr_gpu(_cR, _cN, m, m, l, (magmaDoubleComplex *)dTvecm, ldm, (magmaDoubleComplex *)hTau, (magmaDoubleComplex *)dTm, ldm, (magmaDoubleComplex *)W, sideLR, (magmaDoubleComplex *)dTau, nb, &info); 
        if(info != 0) printf("\nError in MagmaORTH_2nev (magma_zunmqr_gpu), exit ...\n"), exit(-1);
             	
        //get QHT product:
        magma_zunmqr_gpu(_cL, _cC, m, l, l, (magmaDoubleComplex *)dTvecm, ldm, (magmaDoubleComplex *)hTau, (magmaDoubleComplex *)dTm, ldm, (magmaDoubleComplex *)W, sideLR, (magmaDoubleComplex *)dTau, nb, &info);
        if(info != 0) printf("\nError in MagmaORTH_2nev (magma_zunmqr_gpu), exit ...\n"), exit(-1);  

     }
#endif

  return l;
}

void BlasMagmaArgs::RestartV(void *dV, const int vld, const int vlen, const int vprec, void *dTevecm, void *dTm)
{
#ifdef MAGMA_LIB 
       const int cprec = 2*prec;
       int l           = max_nev;
//
       //void *Tmp = 0;
       //magma_malloc((void**)&Tmp, vld*l*cprec);     

       //cudaMemset(Tmp, 0, vld*l*cprec);   
//
       const int bufferSize = 2*vld+l*l;
      
       int bufferBlock = bufferSize / l;

       void  *buffer = 0;
       magma_malloc(&buffer, bufferSize*cprec);
       cudaMemset(buffer, 0, bufferSize*cprec);


       if(prec == 4)
       {
         magma_int_t nb = magma_get_cgeqrf_nb(m);//ldm
         magma_cunmqr_gpu(_cL, _cN, m, l, l, (magmaFloatComplex*)dTevecm, ldm, (magmaFloatComplex*)hTau, (magmaFloatComplex*)dTm, ldm, (magmaFloatComplex*)W, sideLR, (magmaFloatComplex*)dTau, nb, &info);
        
         if(info != 0) printf("\nError in RestartV (magma_cunmqr_gpu), exit ...\n"), exit(-1); 
       }
       else
       {
         magma_int_t nb = magma_get_zgeqrf_nb(m);//ldm
         magma_zunmqr_gpu(_cL, _cN, m, l, l, (magmaDoubleComplex*)dTevecm, ldm, (magmaDoubleComplex*)hTau, (magmaDoubleComplex*)dTm, ldm, (magmaDoubleComplex*)W, sideLR, (magmaDoubleComplex*)dTau, nb, &info);

         if(info != 0) printf("\nError in RestartV (magma_zunmqr_gpu), exit ...\n"), exit(-1); 
       }

       if(vprec == 4)
       {
         magmaFloatComplex *dtm;

         if(prec == 8)
         {
            magma_malloc((void**)&dtm, ldm*l*prec);

            double *hbuff1;
            float  *hbuff2;

            magma_malloc_pinned((void**)&hbuff1, ldm*l*cprec);
            magma_malloc_pinned((void**)&hbuff2, ldm*l*prec);

            cudaMemcpy(hbuff1, dTm, ldm*l*cprec, cudaMemcpyDefault); 
            for(int i = 0; i < ldm*l; i++) hbuff2[i] = (float)hbuff1[i];

            cudaMemcpy(dtm, hbuff2, ldm*l*prec, cudaMemcpyDefault); 

            magma_free_pinned(hbuff1);
            magma_free_pinned(hbuff2);
         }
         else 
         {
            dtm = (magmaFloatComplex *)dTm;
         }

         //magmablas_cgemm('N', 'N', vlen, l, m, MAGMA_C_ONE, (magmaFloatComplex*)dV, vld, dtm, ldm, MAGMA_C_ZERO, (magmaFloatComplex*)Tmp, vld);

         for (int blockOffset = 0; blockOffset < vlen; blockOffset += bufferBlock) 
         {
           if (bufferBlock > (vlen-blockOffset)) bufferBlock = (vlen-blockOffset);

           magmaFloatComplex *ptrV = &(((magmaFloatComplex*)dV)[blockOffset]);

           magmablas_cgemm(_cN, _cN, bufferBlock, l, m, MAGMA_C_ONE, ptrV, vld, dtm, ldm, MAGMA_C_ZERO, (magmaFloatComplex*)buffer, bufferBlock);

           cudaMemcpy2D(ptrV, vld*cprec, buffer, bufferBlock*cprec,  bufferBlock*cprec, l, cudaMemcpyDefault);

         }

         if(prec == 8) magma_free(dtm);

       }
       else
       {
         //magmablas_zgemm('N', 'N', vlen, l, m, MAGMA_Z_ONE, (magmaDoubleComplex*)dV, vld, (magmaDoubleComplex*)dTm, ldm, MAGMA_Z_ZERO, (magmaDoubleComplex*)Tmp, vld);

         for (int blockOffset = 0; blockOffset < vlen; blockOffset += bufferBlock) 
         {
           if (bufferBlock > (vlen-blockOffset)) bufferBlock = (vlen-blockOffset);

           magmaDoubleComplex *ptrV = &(((magmaDoubleComplex*)dV)[blockOffset]);

           magmablas_zgemm(_cN, _cN, bufferBlock, l, m, MAGMA_Z_ONE, ptrV, vld, (magmaDoubleComplex*)dTm, ldm, MAGMA_Z_ZERO, (magmaDoubleComplex*)buffer, bufferBlock);

           cudaMemcpy2D(ptrV, vld*cprec, buffer, bufferBlock*cprec,  bufferBlock*cprec, l, cudaMemcpyDefault);
	 }
       }

       //cudaMemcpy(dV, Tmp, vld*l*cprec, cudaMemcpyDefault); 

       //magma_free(Tmp);
       magma_free(buffer);
#endif

       return;
}


void BlasMagmaArgs::SolveProjMatrix(void* rhs, const int ldn, const int n, void* H, const int ldH)
{
#ifdef MAGMA_LIB
       const int complex_prec = 2*prec;
       void *tmp; 
       magma_int_t *ipiv;
       magma_int_t err;

       magma_malloc_pinned((void**)&tmp, ldH*n*complex_prec);
       magma_malloc_pinned((void**)&ipiv, n*sizeof(magma_int_t));

       memcpy(tmp, H, ldH*n*complex_prec);

       if (prec == 4)
       {
          err = magma_cgesv(n, 1, (magmaFloatComplex*)tmp, ldH, ipiv, (magmaFloatComplex*)rhs, ldn, &info);
          if(err != 0) printf("\nError in SolveProjMatrix (magma_cgesv), exit ...\n"), exit(-1);
       }
       else
       {
          err = magma_zgesv(n, 1, (magmaDoubleComplex*)tmp, ldH, ipiv, (magmaDoubleComplex*)rhs, ldn, &info);
          if(err != 0) printf("\nError in SolveProjMatrix (magma_zgesv), exit ...\n"), exit(-1);
       }

       magma_free_pinned(tmp);
       magma_free_pinned(ipiv);
#endif
       return;
}

void BlasMagmaArgs::SolveGPUProjMatrix(void* rhs, const int ldn, const int n, void* H, const int ldH)
{
#ifdef MAGMA_LIB
       const int complex_prec = 2*prec;
       void *tmp; 
       magma_int_t *ipiv;
       magma_int_t err;

       magma_malloc((void**)&tmp, ldH*n*complex_prec);
       magma_malloc_pinned((void**)&ipiv, n*sizeof(magma_int_t));

       cudaMemcpy(tmp, H, ldH*n*complex_prec, cudaMemcpyDefault);

       if (prec == 4)
       {
          err = magma_cgesv_gpu(n, 1, (magmaFloatComplex*)tmp, ldH, ipiv, (magmaFloatComplex*)rhs, ldn, &info);
          if(err != 0) printf("\nError in SolveGPUProjMatrix (magma_cgesv), exit ...\n"), exit(-1);
       }
       else
       {
          err = magma_zgesv_gpu(n, 1, (magmaDoubleComplex*)tmp, ldH, ipiv, (magmaDoubleComplex*)rhs, ldn, &info);
          if(err != 0) printf("\nError in SolveGPUProjMatrix (magma_zgesv), exit ...\n"), exit(-1);
       }

       magma_free(tmp);
       magma_free_pinned(ipiv);
#endif
       return;
}

void BlasMagmaArgs::SpinorMatVec
(void *spinorOut, const void *spinorSetIn, const int sld, const int slen, const void *vec, const int vlen)
{
#ifdef MAGMA_LIB
       if (prec == 4)
       {
           magmaFloatComplex *spmat = (magmaFloatComplex*)spinorSetIn; 
           magmaFloatComplex *spout = (magmaFloatComplex*)spinorOut; 

           magmablas_cgemv(_cN, slen, vlen, MAGMA_C_ONE, spmat, sld, (magmaFloatComplex*)vec, 1, MAGMA_C_ZERO, spout, 1);//in colour-major format
       }
       else
       {
           magmaDoubleComplex *spmat = (magmaDoubleComplex*)spinorSetIn; 
           magmaDoubleComplex *spout = (magmaDoubleComplex*)spinorOut; 

           magmablas_zgemv(_cN, slen, vlen, MAGMA_Z_ONE, spmat, sld, (magmaDoubleComplex*)vec, 1, MAGMA_Z_ZERO, spout, 1);//in colour-major format
       }
#endif
       return;
}

void BlasMagmaArgs::MagmaRightNotrUNMQR(const int clen, const int qrlen, const int nrefls, void *QR, const int ldqr, void *Vm, const int cldn)
{

     magma_int_t m = clen; 
     magma_int_t n = qrlen; 
     magma_int_t k = nrefls;

     magma_int_t lwork = -1;

#ifdef MAGMA_LIB
     if(prec == 4)
     {

     }
     else
     {
        magmaDoubleComplex *dQR  = NULL;

        magmaDoubleComplex *dtau = NULL;

        magmaDoubleComplex *htau = NULL;

        magmaDoubleComplex *hW   = NULL;

        magmaDoubleComplex qW;

        magma_malloc_pinned((void**)&dQR, ldqr*k*sizeof(magmaDoubleComplex));

        magma_malloc_pinned((void**)&htau, k*sizeof(magmaDoubleComplex));
        //
        magma_malloc((void**)&dTau,  k*sizeof(magmaDoubleComplex));

        cudaMemcpy(dQR, QR, ldqr*k*sizeof(magmaDoubleComplex), cudaMemcpyDefault);

        magma_int_t nb = magma_get_zgeqrf_nb(m);//ldm
        //
        magma_zgeqrf_gpu(n, k, (magmaDoubleComplex *)dQR, ldqr, (magmaDoubleComplex *)htau, (magmaDoubleComplex *)dtau, &info);//identical to zgeqrf?

        magma_zunmqr_gpu(_cR, _cN, m, n, k, dQR, ldqr, htau, (magmaDoubleComplex *)Vm, cldn, &qW, lwork, dtau, nb, &info); 
        if(info != 0) printf("\nError in MagmaORTH_2nev (magma_zunmqr_gpu), exit ...\n"), exit(-1);

        lwork = (magma_int_t) MAGMA_Z_REAL(qW);

        magma_malloc_cpu((void**)&hW, lwork*sizeof(magmaDoubleComplex));

        //get TQ product:
        magma_zunmqr_gpu(_cR, _cN, m, n, k, dQR, ldqr, htau, (magmaDoubleComplex *)Vm, cldn, hW, lwork, dtau, nb, &info); 
        if(info != 0) printf("\nError in MagmaORTH_2nev (magma_zunmqr_gpu), exit ...\n"), exit(-1);

        magma_free_cpu(hW);

        magma_free(dtau);

        magma_free_pinned(htau);

        magma_free_pinned(dQR);

     }
#endif

  return;
}

//WARNING: experimental stuff -> modification of magma_zunmqr library routines (see zunmqr.cpp file)
//pQR, pTau : host arrays
//pVm : device (cuda) arrays
//computes pVm * pQR
void BlasMagmaArgs::MagmaRightNotrUNMQR(const int clen, const int qrlen, const int nrefls, void *pQR, const int ldqr, void *pTau, void *pVm, const int cldn)
{
    #define  A(i_,j_) ( A + (i_) + (j_)*lda)
    #define dC(i_,j_) (dC + (i_) + (j_)*ldc)

    cudaHostRegister(pQR, ldqr*nrefls*sizeof(magmaDoubleComplex), cudaHostRegisterMapped);

    magmaDoubleComplex *A   = (magmaDoubleComplex *)pQR;
    magmaDoubleComplex *dC  = (magmaDoubleComplex *)pVm;
    magmaDoubleComplex *tau = (magmaDoubleComplex *)pTau;

    magma_int_t m = clen;//The number of rows of the matrix dC
    magma_int_t n = qrlen;//The number of columns of the matrix C
    magma_int_t k = nrefls;//The number of elementary reflectors whose product defines the matrix A, as returned by ZGEQRF.

    magma_int_t lda = ldqr;//leading dimension of A
    magma_int_t ldc = cldn;//WARNING: must be ((m+31)/32)*32;

    
    magmaDoubleComplex *T, *T2;
    magma_int_t i, i1, i2, ib, ic, jc, nb, mi, ni, nq_i;

    magma_int_t nq = n;
    magma_int_t nw = m;

    magma_int_t ldwork = nw;

    nb = magma_get_zgelqf_nb( ( m < n ? m : n) ); //=64 for m < 1024, and 128 otherwise

    if (nb >= nrefls) printf("\nError: number of reflectors must be bigger then 64. (%d : %d)\n", nb, nrefls), exit(-1);

    /* Use hybrid CPU-GPU code */
    /* Allocate work space on the GPU.
     * nw*nb  for dwork (m or n) by nb
     * nq*nb  for dV    (n or m) by nb
     * nb*nb  for dT
     */
     magmaDoubleComplex *dwork, *dV, *dT;
     magma_zmalloc( &dwork, (nw + nq + nb)*nb+nb);//the last nb just padding

     if ( dwork == NULL ) printf("\nError: cannot allocate device memory!\n"), exit(-1);

     dV = dwork + nw*nb;
     dT = dV    + nq*nb;
        
     /* work space on CPU.
      * nb*nb for T
      * nb*nb for T2, used to save and restore diagonal block of panel */
     magma_zmalloc_cpu( &T, 2*nb*nb );
     if ( T == NULL ) printf("\nError: cannot allocate host memory!\n"), exit(-1);

     T2 = T + nb*nb;
        
     i1 = 0;
     i2 = k;

     // silence "uninitialized" warnings
     ni = 0;
     mi = m;
     ic = 0;

     for (i = i1; i < i2; i += nb) {
         ib = (nb < (k - i)) ? nb : (k - i);
         /* Form the triangular factor of the block reflector
            H = H(i) H(i+1) . . . H(i+ib-1) */
         nq_i = nq - i;
         //zlarft("Forward", "Columnwise", &nq_i, &ib, A(i,i), &lda, &tau[i], T, &ib);
         LAPACK(zlarft)("Forward", "Columnwise", &nq_i, &ib, (_Complex double*)(A(i,i)), &lda, (_Complex double*)&tau[i], (_Complex double*)T, &ib);
            /* 1) set upper triangle of panel in A to identity,
               2) copy the panel from A to the GPU, and
               3) restore A                                      */
         zpanel_to_q( MagmaUpper, ib, A(i,i), lda, T2 );
         magma_zsetmatrix( nq_i,  ib, A(i,i), lda, dV, nq_i );
         zq_to_panel( MagmaUpper, ib, A(i,i), lda, T2 );

         /* H or H**H is applied to C(1:m,i:n) */
         ni = n - i;
         jc = i;

         /* Apply H or H**H; First copy T to the GPU */
         magma_zsetmatrix( ib, ib, T, ib, dT, ib );

         magma_zlarfb_gpu( MagmaRight, MagmaNoTrans, MagmaForward, MagmaColumnwise,
                              mi, ni, ib,
                              dV, nq_i,
                              dT, ib,
                              dC(ic,jc), ldc,
                              dwork, ldwork );
     }

     magma_free( dwork );
     magma_free_cpu( T );

     cudaHostUnregister(pQR);

     return;   
} 

void BlasMagmaArgs::LapackRightNotrUNMQR(const int nrowsMat, const int ncolsMat, const int nref, void *QRM, const int ldqr, void *tau, void *Mat, const int ldm)
{
  if (prec == 4) printf("\nSingle precision is currently not supported.\n"), exit(-1);


  magma_int_t _m   = ncolsMat;//matrix size

  magma_int_t _k   = nref;

  magma_int_t _mp1 = nrowsMat;
 
  magma_int_t _ldm = ldm;

  magma_int_t _ldp = ldqr;

  //Lapack parameters:   
  char _r = 'R';//apply P-matrix from the right

  char _n = 'N';//no left eigenvectors 

  magma_int_t info  = 0;

  magma_int_t lwork = -1; 

  magmaDoubleComplex *work = NULL;

  magmaDoubleComplex qwork; //parameter to extract optimal size of work

  //bar{H}_{m} P_{k}

  lwork = -1;

  LAPACK(zunmqr)(&_r, &_n, &_mp1, &_m, &_k, (_Complex double *)QRM, &_ldp, (_Complex double *)tau, (_Complex double *)Mat, &_ldm, (_Complex double *)&qwork, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  lwork = (magma_int_t) MAGMA_Z_REAL(qwork);

  magma_zmalloc_cpu( &work, lwork );
#ifdef VERBOSE
  printf("\nAdjusted lwork  ( ZUNMQR ## 2): %d\n", lwork);
#endif
  LAPACK(zunmqr)(&_r, &_n, &_mp1, &_m, &_k, (_Complex double *)QRM, &_ldp, (_Complex double *)tau, (_Complex double *)Mat, &_ldm, (_Complex double *)work, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  magma_free_cpu( work );

  return; 
}

//pure Lapack-based methods
void BlasMagmaArgs::LapackLeftConjUNMQR(const int dh /*# of rows*/,  const int n /*# of columns of H*/, const int k /*# of reflectors*/, void *H, const int ldh, void * QR,  const int ldqr, void *tau)//for vectors: n =1
{

  if (prec == 4) printf("\nSingle precision is currently not supported.\n"), exit(-1);

//Note: # rows of QR = # rows of H.
  magma_int_t _h   = dh;//matrix size

  magma_int_t _n   = n;//vector size

  magma_int_t _k   = k;
 
  magma_int_t _ldh = ldh;

  magma_int_t _ldqr = ldqr;

  //Lapack parameters:   
  char _s = 'L';//apply QR-matrix from the left

  char _t = 'C';//conjugate 

  magma_int_t info  = 0;

  magma_int_t lwork = -1; 

  magmaDoubleComplex *work = NULL;

  magmaDoubleComplex qwork; //parameter to extract optimal size of work

  //Pdagger_{k+1} PrevRes

  LAPACK(zunmqr)(&_s, &_t, &_h, &_n, &_k, (_Complex double*)QR, &_ldqr, (_Complex double*)tau, (_Complex double*)H, &_ldh, (_Complex double*)&qwork, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  lwork = (magma_int_t) MAGMA_Z_REAL(qwork);
  //
  magma_zmalloc_cpu( &work, lwork );

  LAPACK(zunmqr)(&_s, &_t, &_h, &_n, &_k, (_Complex double*)QR, &_ldqr, (_Complex double*)tau, (_Complex double*)H, &_ldh, (_Complex double*)work, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  magma_free_cpu( work );

  return;
}

void BlasMagmaArgs::LapackGESV(void* rhs, const int ldn, const int n, void* H, const int ldh)
{
  if (prec == 4) printf("\nSingle precision is currently not supported.\n"), exit(-1);
  //Lapack parameters:
  magma_int_t _n    = n;
  //
  magma_int_t _ldh  = ldh;
  //
  magma_int_t _ldn  = ldn; 
  //
  magma_int_t info  = 0;
  //
  magma_int_t LAPACK_ONE = 1;//just for one rhs
  //
  magma_int_t *ipiv = NULL;

  magma_malloc_cpu((void**)&ipiv, ldh*sizeof(magma_int_t));
  //
  LAPACK(zgesv)(&_n, &LAPACK_ONE, (_Complex double*)H, &_ldh, ipiv, (_Complex double*)rhs, &_ldn, &info);

  if( (info != 0 ) ) printf( "Error: DGESV, info %d\n",info), exit(-1);

  magma_free_cpu(ipiv);

  return;
}

void BlasMagmaArgs::LapackGEQR(const int n, void *Mat, const int m, const int ldm, void *tau)
{
  if (prec == 4) printf("\nSingle precision is currently not supported.\n"), exit(-1);

  magma_int_t _m   = m;

  magma_int_t _n   = n;
 
  magma_int_t _ldm = ldm;

  //Lapack parameters:   
  magma_int_t info  = 0;

  magma_int_t lwork = -1; 

  magmaDoubleComplex *work = NULL;

  magmaDoubleComplex qwork; //parameter to extract optimal size of work

  LAPACK(zgeqrf)(&_m, &_n, (_Complex double*)Mat, &_ldm, (_Complex double*)tau, (_Complex double*)&qwork, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEQRF, info %d\n",info), exit(-1);

  lwork = (magma_int_t) MAGMA_Z_REAL(qwork);
  //
  magma_zmalloc_cpu( &work, lwork );
#ifdef VERBOSE
  printf("\nAdjusted lwork : %d\n", lwork);
#endif
  LAPACK(zgeqrf)(&_m, &_n, (_Complex double*)Mat, &_ldm, (_Complex double*)tau, (_Complex double*)work, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEQRF, info %d\n",info), exit(-1);

  magma_free_cpu( work );

  return;
}

void BlasMagmaArgs::LapackRightEV(const int m,  const int ldm, void *Mat, void *harVals, void *harVecs, const int ldv)
{
  if (prec == 4) printf("\nSingle precision is currently not supported.\n"), exit(-1);

  magma_int_t _m   = m;//matrix size
 
  magma_int_t _ldm = ldm;

  magma_int_t _ldv = ldv;

  //Lapack parameters:   
  magma_int_t info = 0;
  //
  char _r = 'V';

  char _l = 'N';//no left eigenvectors

  magma_int_t lwork = -1;
 
  magmaDoubleComplex *work = NULL;

  magmaDoubleComplex qwork; //parameter to extract optimal size of work
  
  double *rwork       = (double*)calloc(2*_m, sizeof(double));

  //_Complex double *vr = (_Complex double*) calloc(ldv*m, sizeof(_Complex double));
#ifdef USE_ZGEEVX

  char _b = 'N';//balance

  char _s = 'N';//sense

  magma_int_t ilo, ihi;

  double abnrm = 0.0;

  double *scale  = calloc(_m, sizeof(double));

  double *rconde = calloc(_m, sizeof(double));

  double *rcondv = calloc(_m, sizeof(double));

  //Get optimal work:
  LAPACK(zgeevx)(&_b, &_l, &_r, &_s, &_m, (_Complex double*)Mat, &_ldm, (_Complex double*)harVals, NULL, &_ldv, (_Complex double*)harVecs, &_ldv, &ilo, &ihi, scale, &abnrm, rconde, rcondv, (_Complex double*)&qwork, &lwork, rwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEEVX, info %d\n",info), exit(-1);

  lwork = (magma_int_t) MAGMA_Z_REAL(qwork);
  //
  magma_zmalloc_cpu( &work, lwork );
  //now get eigenpairs:
  LAPACK(zgeevx)(&_b, &_l, &_r, &_s, &_m, (_Complex double*)Mat, &_ldm, (_Complex double*)harVals, NULL, &_ldv, (_Complex double*)harVecs, &_ldv, &ilo, &ihi, scale, &abnrm, rconde, rcondv, (_Complex double*)work, &lwork, rwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEEVX, info %d\n",info), exit(-1);

  if(scale)  free(scale);
  //
  if(rcondv) free(rcondv);
  //
  if(rconde) free(rconde);

#else

  //Get optimal work:
  LAPACK(zgeev)(&_l, &_r, &_m, (_Complex double*)Mat, &_ldm, (_Complex double*)harVals, NULL, &_ldv, (_Complex double*)harVecs, &_ldv, (_Complex double*)&qwork, &lwork, rwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEEVX, info %d\n",info), exit(-1);

  lwork = (magma_int_t) MAGMA_Z_REAL(qwork);
  //
  magma_zmalloc_cpu( &work, lwork );

  //now get eigenpairs:
  LAPACK(zgeev)(&_l, &_r, &_m, (_Complex double*)Mat, &_ldm, (_Complex double*)harVals, NULL, &_ldv, (_Complex double*)harVecs, &_ldv, (_Complex double*)work, &lwork, rwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEEVX, info %d\n",info), exit(-1);


#endif

  if(rwork)  free(rwork);
  //
  magma_free_cpu( work );


  return;
}

#if 0
void BlasMagmaArgs::Sort(const int m, const int ldm, void *eVecs, const int nev, void *unsorted_eVecs, void *eVals)
{
//use std::sort?
  if (prec == 4) printf("\nSingle precision is currently not supported.\n"), exit(-1);

  double* eval_nrm = (double*)calloc(m, sizeof(double));
  int*    eval_idx = (int*)calloc(m, sizeof(int)); 

  for(int e = 0; e < m; e++) 
  {
    eval_nrm[e] = abs(((std::complex<double>*)eVals)[e]);
    eval_idx[e] = e;
  }

 double v, td;
 int i, j, ti; 
 int stack[32];
  
 int l = 0; int r = nev-1; int tos = -1;

 for (;;){
    while (r > l){ 
	v = eval_nrm[r]; i = l; j = r-1;
	for (;;){ 
	    while (eval_nrm[i] < v) i ++;
	    /* j > l prevents underflow */
	    while (eval_nrm[j] >= v && j > l) j --;
	    if (i >= j) break;
	    td = eval_nrm[i]; eval_nrm[i] = eval_nrm[j]; eval_nrm[j] = td;
	    ti = eval_idx[i]; eval_idx[i] = eval_idx[j]; eval_idx[j] = ti;
	}
	td = eval_nrm[i]; eval_nrm[i] = eval_nrm[r]; eval_nrm[r] = td;
	ti = eval_idx[i]; eval_idx[i] = eval_idx[r]; eval_idx[r] = ti;
	if (i-l > r-i){ 
	    stack[++tos] = l; stack[++tos] = i-1; l = i+1; 
	}
	else{	    
	    stack[++tos] = i+1; stack[++tos] = r; r = i-1; 
	}
	if(tos > 31)  printf("Error in quicksort! Aborting...!"),   exit(-1);
	
    } 
    if (tos == -1) break;
    r = stack[tos--]; l = stack[tos--]; 
 }

 for(int e = 0; e < nev; e++)
 {  
   memcpy(&(((std::complex<double>*)eVecs)[ldm*e]), &(((std::complex<double>*)unsorted_eVecs)[ldm*(eval_idx[e])]), (ldm)*sizeof(std::complex<double>));
   //set zero in m+1 element:
   ((std::complex<double>*)eVecs)[ldm*e+m] = std::complex<double>(0.0, 0.0);
 }

}
#endif

//STL based version:
//

struct SortedEval{

   double eval_nrm;
   int    eval_idx;

   SortedEval(double val, int idx) : eval_nrm(val), eval_idx(idx) {}; 
};

bool cmp_eigen_nrms (SortedEval v1, SortedEval v2)
{
  return (v1.eval_nrm < v2.eval_nrm);
}

void BlasMagmaArgs::Sort(const int m, const int ldm, void *eVecs, const int nev, void *unsorted_eVecs, void *eVals)
{
  if (prec == 4) printf("\nSingle precision is currently not supported.\n"), exit(-1);

  std::vector<SortedEval> sorted_evals_cntr;

  for(int e = 0; e < m; e++) sorted_evals_cntr.push_back( SortedEval( abs(((std::complex<double>*)eVals)[e]), e ));

  std::stable_sort(sorted_evals_cntr.begin(), sorted_evals_cntr.end(), cmp_eigen_nrms);
  

  for(int e = 0; e < nev; e++)
  {  
    memcpy(&(((std::complex<double>*)eVecs)[ldm*e]), &(((std::complex<double>*)unsorted_eVecs)[ldm*( sorted_evals_cntr[e].eval_idx)]), (ldm)*sizeof(std::complex<double>));
    //set zero in m+1 element:
    ((std::complex<double>*)eVecs)[ldm*e+m] = std::complex<double>(0.0, 0.0);
  }

  return;
}


///NEW STUFF:

#include <complex.h>


void BlasMagmaArgs::ComputeQR(const int nev, Complex * evmat, const int m, const int ldm, Complex  *tau)
{
  int _m   = m;//matrix size

  int _nev = nev;//matrix size
 
  int _ldm = ldm;

  //Lapack parameters:   
  int info  = 0;

  int lwork = -1; 

  _Complex double *work = NULL;

  _Complex double qwork; //parameter to extract optimal size of work

  LAPACK(zgeqrf)(&_m, &_nev, (_Complex double *)evmat, &_ldm, (_Complex double *)tau, &qwork, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEQRF, info %d\n",info), exit(-1);

  lwork = (int)(creal(qwork));
  //
  work = (_Complex double*)calloc(lwork, sizeof(_Complex double));
#ifdef VERBOSE
  printf("\nAdjusted lwork : %d\n", lwork);
#endif
  LAPACK(zgeqrf)(&_m, &_nev, (_Complex double *)evmat, &_ldm, (_Complex double *)tau, work, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEQRF, info %d\n",info), exit(-1);

  if(work) free(work);

  return;
}

//temporary hack: need to extract Q-matrix.

void restoreOrthVectors(Complex * vnev,  const int nev, Complex  *QR, const int n, const int ldn, Complex *tau)
{
  int _n   = n;//vector size

  int _k   = nev;

  int _ldn = ldn;

  //Lapack parameters:   
  char _s = 'R';//apply P-matrix from the right

  char _t = 'N';//no left eigenvectors 

  int info  = 0;

  int lwork = -1; 

  _Complex double *work = NULL;

  _Complex double qwork; //parameter to extract optimal size of work

  //Construct unit matrix to populate this later by the basis vectors (nxn matrix):
  memset(vnev, 0, n*ldn*sizeof(Complex));

  for(int d = 0; d < n; d++) vnev[ldn*d+d] = Complex(1.0, 0.0);

  //First compute V_{nev} = V_{nev}*QR

  LAPACK(zunmqr)(&_s, &_t, &_n, &_n, &_k, (_Complex double *)QR, &_ldn, (_Complex double *)tau, (_Complex double *)vnev, &_ldn, &qwork, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  lwork = (int)(creal(qwork));
  //
  work = (_Complex double*)calloc(lwork, sizeof(_Complex double));
#ifdef VERBOSE
  printf("\nAdjusted lwork ( ZUNMQR ) : %d\n", lwork);
#endif
  LAPACK(zunmqr)(&_s, &_t, &_n, &_n, &_k, (_Complex double *)QR, &_ldn, (_Complex double *)tau, (_Complex double *)vnev, &_ldn, work, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  return;
}


void BlasMagmaArgs::LeftConjZUNMQR(const int k /*number of reflectors*/, const int n /*number of columns of H*/, Complex *H, const int dh /*number of rows*/, 
const int ldh, Complex * QR,  const int ldqr, Complex *tau)//for vectors: n =1
{
//Note: # rows of QR = # rows of H.
  int _h   = dh;//matrix size

  int _n   = n;//vector size

  int _k   = k;
 
  int _ldh = ldh;

  int _ldqr = ldqr;

  //Lapack parameters:   
  char _s = 'L';//apply QR-matrix from the left

  char _t = 'C';//conjugate 

  int info  = 0;

  int lwork = -1; 

  _Complex double *work = NULL;

  _Complex double qwork; //parameter to extract optimal size of work

  //Pdagger_{k+1} PrevRes

  LAPACK(zunmqr)(&_s, &_t, &_h, &_n, &_k, (_Complex double *)QR, &_ldqr, (_Complex double *)tau, (_Complex double *)H, &_ldh, &qwork, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  lwork = (int)(creal(qwork));
  //
  free(work);
  //
  work = (_Complex double*)calloc(lwork, sizeof(_Complex double));
#ifdef VERBOSE
  printf("\nAdjusted lwork : %d\n", lwork);
#endif
  LAPACK(zunmqr)(&_s, &_t, &_h, &_n, &_k, (_Complex double *)QR, &_ldqr, (_Complex double *)tau, (_Complex double *)H, &_ldh, work, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  free(work);

  return;
}


void BlasMagmaArgs::Construct_harmonic_matrix(Complex * const harmH, Complex * const conjH, const double beta2, const int m, const int ldH)
{
  //Lapack parameters:
  int _m    = m;
  //
  int _ldH  = ldH;
  //
  int info  = 0;
  //
  int I_ONE = 1;
  //
  int *ipiv = (int* )calloc(ldH, sizeof(int));
  //
  //Construct H + beta*H^{-H} e_m*e_m^{T}
  // 1. need to solve H^{H}y = e_m;
  Complex *em = new Complex[m];
  
  em[m-1] = beta2;//in fact, we construct beta*em,

  LAPACK(zgesv)(&_m, &I_ONE, (_Complex double *)conjH, &_ldH, ipiv, (_Complex double *)em, &_ldH, &info);

  if( (info != 0 ) ) printf( "Error: DGESV, info %d\n",info), exit(-1);

//make this cleaner!
  //check solution:
  for (int j = 0; j < m; j++)
  {
    Complex accum = 0.0;

    for (int i = 0; i < m; i++) accum = (accum + harmH[ldH*j+i]*em[(ipiv[i])-1]);
  } 

  printf("\nDone for: stage 1\n"); 

  // 2. Construct matrix for harmonic Ritz vectors:
  //    Adjust last column with KroneckerProd((H^{-H}*beta*em)=em, em^{T}=[0,....,1]):

  for(int i = 0; i < m; i++) harmH[ldH*(m-1)+i] += em[i]; 

  printf("\nDone for: stage 2\n");

  free(ipiv);
  //
  delete [] em; 
}

void BlasMagmaArgs::Compute_harmonic_matrix_eigenpairs(Complex *harmH, const int m, const int ldH, Complex *vr, Complex *evalues, const int ldv) 
{
  int _m   = m;//matrix size
 
  int _ldH = ldH;

  int _ldv = ldv;

  //Lapack parameters:   
  int info = 0;
  //
  char _r = 'V';

  char _l = 'N';//no left eigenvectors

  int lwork = -1;
 
  _Complex double *work = NULL;

  _Complex double qwork; //parameter to extract optimal size of work
  
  double *rwork       = (double*)calloc(2*_m, sizeof(double));

  //_Complex double *vr = (_Complex double*) calloc(ldv*m, sizeof(_Complex double));
#ifdef USE_ZGEEVX

  char _b = 'N';//balance

  char _s = 'N';//sense

  int ilo, ihi;

  double abnrm = 0.0;

  double *scale  = calloc(_m, sizeof(double));

  double *rconde = calloc(_m, sizeof(double));

  double *rcondv = calloc(_m, sizeof(double));

  //Get optimal work:
  LAPACK(zgeevx)(&_b, &_l, &_r, &_s, &_m, (_Complex double *)harmH, &_ldH, (_Complex double *)evalues, NULL, &_ldv, (_Complex double *)vr, &_ldv, &ilo, &ihi, scale, &abnrm, rconde, rcondv, &qwork, &lwork, rwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEEVX, info %d\n",info), exit(-1);

  lwork = (int)(creal(qwork));
  //
  work = (_Complex double*)calloc(lwork, sizeof(_Complex double));
#ifdef VERBOSE
  printf("\nAdjusted lwork : %d\n", lwork);
#endif
  //now get eigenpairs:
  LAPACK(zgeevx)(&_b, &_l, &_r, &_s, &_m, (_Complex double *)harmH, &_ldH, (_Complex double *)evalues, NULL, &_ldv, (_Complex double *)vr, &_ldv, &ilo, &ihi, scale, &abnrm, rconde, rcondv, work, &lwork, rwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEEVX, info %d\n",info), exit(-1);

  if(scale)  free(scale);
  //
  if(rcondv) free(rcondv);
  //
  if(rconde) free(rconde);

#else

  //Get optimal work:
  LAPACK(zgeev)(&_l, &_r, &_m, (_Complex double *)harmH, &_ldH, (_Complex double *)evalues, NULL, &_ldv, (_Complex double *)vr, &_ldv, &qwork, &lwork, rwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEEVX, info %d\n",info), exit(-1);

  lwork = (int)(creal(qwork));
  //
  work = (_Complex double*)calloc(lwork, sizeof(_Complex double));
#ifdef VERBOSE
  printf("\nAdjusted lwork : %d\n", lwork);
#endif
  //now get eigenpairs:
  LAPACK(zgeev)(&_l, &_r, &_m, (_Complex double *)harmH, &_ldH, (_Complex double *)evalues, NULL, &_ldv, (_Complex double *)vr, &_ldv, work, &lwork, rwork, &info);

  if( (info != 0 ) ) printf( "Error: ZGEEVX, info %d\n",info), exit(-1);


#endif

  if(rwork)  free(rwork);
  //
  if(work)   free(work);
  //
  return;
}


//in fact ldh = ldm, but let's keep it for a moment.
void BlasMagmaArgs::RestartVH(void *dV, const int vlen, const int vld, const int vprec, void *sortedHarVecs, void *H, const int ldh)
{
    if(prec == 4 || (prec != vprec))
    {
       printf("\nError: single precision and mixing single/double precisions is not currently supported\n");
       exit(-1);
    }

    int nev  = (max_nev - 1); //(nev+1) - 1 for GMRESDR

    int _m   = m;//matrix size

    int _k   = nev;

    int _kp1 = max_nev;

    int _mp1 = (m+1);
 
    int _ldm = ldh;

  //Lapack parameters:   
    char _s = 'R';//apply P-matrix from the right

    char _t = 'N';//no left eigenvectors 

    int info  = 0;

    int lwork = -1; 

    _Complex double *work = NULL;

    _Complex double qwork; //parameter to extract optimal size of work

    const int cprec = 2*prec;//currently: sizeof(Complex)
    int lbsize           = 2*nev;

    const int bufferSize = 2*vld+lbsize*lbsize;
      
    int bufferBlock = bufferSize / lbsize;//or: lbsize = (nev+1)

    int l = max_nev;

    void  *buffer = NULL;
    void  *dQmat  = NULL;

    magma_malloc(&buffer, bufferSize*cprec);
    cudaMemset(buffer, 0, bufferSize*cprec);

    magma_malloc(&dQmat, (max_nev)*ldh*cprec);

    //GPU code:
    Complex *tau  = new Complex[max_nev];//nev+1 =>max_nev

    Complex *Qmat = new Complex[ldh*(m+1)];//need (m+1)x(m+1) matrix on input...

    ComputeQR(max_nev, (Complex*)sortedHarVecs, (m+1), ldh, tau);//lapack version 
#if 1    
    //max_nev vectors are stored in Qmat (output):
    //restoreOrthVectors(Qmat, max_nev, (Complex*)sortedHarVecs, (m+1), ldh, tau);
    //Load diagonal units
    for(int d = 0; d < (m+1); d++) Qmat[ldh*d+d] = Complex(1.0, 0.0);
   
    LAPACK(zunmqr)(&_s, &_t, &_mp1, &_mp1, &_kp1, (_Complex double *)sortedHarVecs, &_ldm, (_Complex double *)tau, (_Complex double *)Qmat, &_ldm, &qwork, &lwork, &info);

    if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

    lwork = (int)(creal(qwork));
  
    work = (_Complex double*)calloc(lwork, sizeof(_Complex double));

    LAPACK(zunmqr)(&_s, &_t, &_mp1, &_mp1, &_kp1, (_Complex double *)sortedHarVecs, &_ldm, (_Complex double *)tau, (_Complex double *)Qmat, &_ldm, work, &lwork, &info);

    if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

    free(work);

    //Copy (nev+1) vectors on the device:
    cudaMemcpy(dQmat, Qmat, (max_nev)*ldh*cprec, cudaMemcpyDefault);

    for (int blockOffset = 0; blockOffset < vlen; blockOffset += bufferBlock) 
    {
      if (bufferBlock > (vlen-blockOffset)) bufferBlock = (vlen-blockOffset);

//printf("\nBuffer block : %d\n", bufferBlock);

      magmaDoubleComplex *ptrV = &(((magmaDoubleComplex*)dV)[blockOffset]);

      magmablas_zgemm(_cN, _cN, bufferBlock, l, _mp1, MAGMA_Z_ONE, ptrV, vld, (magmaDoubleComplex*)dQmat, ldh, MAGMA_Z_ZERO, (magmaDoubleComplex*)buffer, bufferBlock);

      cudaMemcpy2D(ptrV, vld*cprec, buffer, bufferBlock*cprec,  bufferBlock*cprec, l, cudaMemcpyDefault);//make this async!
    }
//ups! make on GPU:
    cudaMemset(&(((magmaDoubleComplex*)dV)[vld*max_nev]), 0, (m+1-max_nev)*vld*sizeof(magmaDoubleComplex));//= m - nev

    printf("\nDone...\n");

#else
/////////////////////////////////START DEBUG:

  printf("\nPrint vld , vlen: %d, %d, %d, %d, %d, %d\n", vld, vlen, nev, _mp1, _ldm, _m);

  int _ldn = vld;

  int _n   = vlen;

  Complex *hVm = new Complex[vld*(m+1)];

  cudaMemcpy(hVm , dV, vld*(m+1)* sizeof(Complex), cudaMemcpyDefault);

  LAPACK(zunmqr)(&_s, &_t, &_n, &_mp1, &_kp1, (_Complex double *)sortedHarVecs, &_ldm, (_Complex double *)tau, (_Complex double *)hVm, &_ldn, &qwork, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  lwork = (int)(creal(qwork));

  work = (_Complex double*)calloc(lwork, sizeof(_Complex double));

  LAPACK(zunmqr)(&_s, &_t, &_n, &_mp1, &_kp1, (_Complex double *)sortedHarVecs, &_ldm, (_Complex double *)tau, (_Complex double *)hVm, &_ldn, work, &lwork, &info);

  if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

  memset(&hVm[vld*(nev+1)], 0, (m-nev)*vld*sizeof(_Complex double));

  cudaMemcpy(dV, hVm , vld*(m+1)* sizeof(Complex), cudaMemcpyDefault);

  free(work);

  printf("\nDone...\n");

  delete [] hVm;
/////////////////////////////////STOP DEBUG
#endif

    //Construct H_new = Pdagger_{k+1} \bar{H}_{m} P_{k}  

    //bar{H}_{m} P_{k}

    lwork = -1;

    LAPACK(zunmqr)(&_s, &_t, &_mp1, &_m, &_k, (_Complex double *)sortedHarVecs, &_ldm, (_Complex double *)tau, (_Complex double *)H, &_ldm, &qwork, &lwork, &info);

    if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

    lwork = (int)(creal(qwork));
    //
    work = (_Complex double*)calloc(lwork, sizeof(_Complex double));

    LAPACK(zunmqr)(&_s, &_t, &_mp1, &_m, &_k, (_Complex double *)sortedHarVecs, &_ldm, (_Complex double *)tau, (_Complex double *)H, &_ldm, work, &lwork, &info);

    if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

    //Pdagger_{k+1} PrevRes

    lwork = -1;

    _s = 'L';

    _t = 'C';

    LAPACK(zunmqr)(&_s, &_t, &_mp1, &_k, &_kp1, (_Complex double *)sortedHarVecs, &_ldm, (_Complex double *)tau, (_Complex double *)H, &_ldm, &qwork, &lwork, &info);

    if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

    lwork = (int)(creal(qwork));
    //
    free(work);
    //
    work = (_Complex double*)calloc(lwork, sizeof(_Complex double));

    LAPACK(zunmqr)(&_s, &_t, &_mp1, &_k, &_kp1, (_Complex double *)sortedHarVecs, &_ldm, (_Complex double *)tau, (_Complex double *)H, &_ldm, work, &lwork, &info);

    if( (info != 0 ) ) printf( "Error: ZUNMQR, info %d\n",info), exit(-1);

    const int len = ldh - nev-1;
    for(int i = 0; i < nev; i++) memset(&(((Complex*)H)[ldh*i+nev+1]), 0, len*sizeof(Complex) );

    //
    memset(&(((Complex*)H)[ldh*(nev)]), 0, (m-nev)*ldh*sizeof(Complex));

    free(work);

    magma_free(buffer);
    magma_free(dQmat);

    delete [] Qmat;
    delete [] tau ;

    return; 
}


#ifdef MAGMA_LIB

#undef _cV
#undef _cU
#undef _cR
#undef _cL
#undef _cC
#undef _cN

#endif

