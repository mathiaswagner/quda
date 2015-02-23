#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <quda_internal.h>
#include <color_spinor_field.h>
#include <blas_quda.h>
#include <dslash_quda.h>
#include <invert_quda.h>
#include <util_quda.h>
#include <sys/time.h>
#include <string.h>

#include <face_quda.h>

#include <iostream>

#include <blas_magma.h>
#include <algorithm>

#define DEBUG_MODE

#define MAX_EIGENVEC_WINDOW 64

/*
Based on  GMRES-DR algorithm:
R. B. Morgan, "GMRES with deflated restarting"
Code design based on: A.Frommer et al, ArXiv hep-lat/1204.5463
*/

namespace quda {
//Notes:
//GmresDR does not require large m (and esp. nev), so this will use normal LAPACK routines.

    struct SortEvals{

      double eval_nrm;
      int    eval_idx;

      SortEvals(double val, int idx) : eval_nrm(val), eval_idx(idx) {}; 

      static bool CmpEigenNrms (SortEvals v1, SortEvals v2) { return (v1.eval_nrm < v2.eval_nrm);}

    };

    class GmresDRArgs{
     
      private:
      BlasMagmaArgs *gmresdr_magma_args;
      //
      Complex *harVecs;//array of harmonic eigenvectors
      Complex *harVals;//array of harmonic Ritz values
      //
      Complex *sortedHarVecs;//array of harmonic eigenvectors: (nev+1) length
      Complex *sortedHarVals;//array of harmonic Ritz values: nev+1 length
      //
      Complex *harMat;//harmonic matrix
      Complex *H;//Hessenberg matrix
      Complex *cH;//conjugate Hess. matrix

      //aux array to keep QR decomposed "restarted Hessenberg":
      Complex *qrH;
      Complex *tauH;//nev->m : for DEBUGING only!

      //auxilary object
      Complex *srtRes;//the "short residual" is in fact an alias pointer to &sortedHarVecs[nev*m] !
      Complex *lsqSol;//ls problem solution
   
      public:

      int m;
      int ldm;//leading dimension (must be >= m+1)
      int nev;//number of harmonic eigenvectors used for the restart

      //int k; //current number of the basis vectors (usually: nev, note that Vm contains k+1 vectors, including the last residual) 
      bool first_cycle_flag;
      
      //public:

      GmresDRArgs( ) { };

      GmresDRArgs(int m, int ldm, int nev, bool fc_flag = true);

      ~GmresDRArgs();
      //more implementations here:

      void ResetHessenberg();
 
      void ConjugateH();//rows = cols+1

      void SetHessenbergElement(const int i, const int j, const Complex h_ij);
      //
      void ComputeHarmonicRitzPairs();

      void RestartVH(cudaColorSpinorField &res, cudaColorSpinorField *Vm);

      void PrepareDeflatedRestart(Complex *givensH, Complex *g);

      void PrepareGivens(Complex *givensH, const int col);

      void UpdateSolution(cudaColorSpinorField &x, cudaColorSpinorField *Vm, Complex *givensH, const int ldH, Complex *g, const int j);
   };

  GmresDRArgs::GmresDRArgs(int m, int ldm, int nev, bool fc_flag): m(m), ldm(ldm), nev(nev), first_cycle_flag(fc_flag){

     int mp1 = m+1;    

     if(ldm < mp1) errorQuda("\nError: the leading dimension is not correct.\n");   

     if(first_cycle_flag && nev == 0) errorQuda("\nError: incorrect deflation size..\n");
     //magma library initialization:

     gmresdr_magma_args = new BlasMagmaArgs(m, nev+1, ldm, sizeof(double));

     harVecs  = new Complex[ldm*m];//(m+1)xm (note that ldm >= m+1)
     harVals  = new Complex[m];//

     sortedHarVecs  = new Complex[ldm*(nev+1)];//
     sortedHarVals  = new Complex[(nev+1)];//

     //ALL this in column major format (for using with LAPACK or MAGMA)
     harMat   = new Complex[ldm*m];
     H        = new Complex[ldm*m];//Hessenberg matrix
     cH       = new Complex[ldm*mp1];//conjugate Hessenberg matrix

     qrH      = new Complex[ldm*m];
     tauH     = new Complex[m];//nev->m : for DEBUGING only!

     srtRes   = &sortedHarVecs[ldm*(nev)];
     lsqSol   = new Complex[(m+1)];
   
     return;
  }

  void GmresDRArgs::ResetHessenberg()
  {
    memset(H,  0, ldm*m*sizeof(Complex));
    //
    memset(cH, 0, ldm*(m+1)*sizeof(Complex));

    return;
  }

  void GmresDRArgs::ConjugateH()//rows = cols+1
  {
    for(int c = 0; c < nev; c++ )
    {
      for(int r = 0; r < (nev+1); r++ ) cH[r*ldm+c] = conj(H[c*ldm+r]);
    }

    return;
  }

  //helper method
  void GmresDRArgs::SetHessenbergElement(const int row, const int col, const Complex el)
  {
    H[col*ldm+row]  = el;

    cH[row*ldm+col] = conj(el); 

    return;
  }


  GmresDRArgs::~GmresDRArgs() {

    delete gmresdr_magma_args;

    delete[] cH;
    delete[] H;
    delete[] harMat;

    delete[] harVals;
    delete[] harVecs;

    delete[] sortedHarVals;
    delete[] sortedHarVecs;

    delete[] qrH;
    delete[] tauH;

    delete[] lsqSol;

    return;
  }


  GmresDR::GmresDR(DiracMatrix &mat, DiracMatrix &matSloppy, DiracMatrix &matDefl, SolverParam &param, TimeProfile &profile) :
    DeflatedSolver(param, profile), mat(mat), matSloppy(matSloppy), matDefl(matDefl), gmres_space_prec(QUDA_INVALID_PRECISION), 
    Vm(0), profile(profile), args(0), gmres_alloc(false)
  {
     if(param.nev > MAX_EIGENVEC_WINDOW )
     { 
          warningQuda("\nWarning: the eigenvector window is too big, using default value %d.\n", MAX_EIGENVEC_WINDOW);
          param.nev = MAX_EIGENVEC_WINDOW;
     }

     if(param.precision != param.precision_sloppy) errorQuda("\nMixed precision GMRESDR is not currently supported.\n");
     //
     gmres_space_prec = param.precision_sloppy;//in fact it currently uses full precision

     //create GMRESDR objects:
     int mp1    = param.m + 1;

     int ldm    = mp1;//((mp1+15)/16)*16;//leading dimension

     args = new GmresDRArgs(param.m, ldm, param.nev);//first_cycle_flag=true by default  

     return;
  }

  GmresDR::~GmresDR() {

    delete args;

    if(gmres_alloc)   delete Vm;

  }

//#include <complex.h>
//new methods:

 void GmresDRArgs::UpdateSolution(cudaColorSpinorField &x, cudaColorSpinorField *Vm, Complex *givensH, const int ldH, Complex *g, const int j)
 {
   memset(lsqSol, 0, (m+1)*sizeof(Complex));
   //Get LS solution:
   for(int l = (j-1); l >= 0; l--)
   {
     Complex accum = 0.0;

     for(int k = (l+1); k <= (j-1); k++)
     {
        Complex cdtp = givensH[ldH*k+l]*lsqSol[k];
        accum = accum+cdtp; 
     }
  
     lsqSol[l] = (g[l] - accum) / givensH[ldH*l+l]; 
   }
   //
   //compute x = x0+Vm*lsqSol
   for(int l = 0; l < j; l++)  caxpyCuda(lsqSol[l], Vm->Eigenvec(l), x);

   return;
 }

 void GmresDRArgs::ComputeHarmonicRitzPairs()
 {
   memcpy(harMat, H, ldm*m*sizeof(Complex));

   const double beta2 = norm(H[ldm*(m-1)+m]);

   gmresdr_magma_args->Construct_harmonic_matrix(harMat, cH, beta2, m, ldm);

   // Computed eigenpairs for harmonic matrix:
   // Save harmonic matrix :
   memset(cH, 0, ldm*m*sizeof(Complex));

   memcpy(cH, harMat, ldm*m*sizeof(Complex));

   gmresdr_magma_args->Compute_harmonic_matrix_eigenpairs(harMat, m, ldm, harVecs, harVals, ldm);//check it!

   //for(int e = 0; e < m; e++) printf("\nEigenval #%d: %le, %le, %le\n", e, creal(evals[e]), cimag(evals[e]), cabs(evals[e]));
   //do sort:
   std::vector<SortEvals> sorted_evals_cntr;

   for(int e = 0; e < m; e++) sorted_evals_cntr.push_back( SortEvals( abs(harVals[e]), e ));

   std::stable_sort(sorted_evals_cntr.begin(), sorted_evals_cntr.end(), SortEvals::CmpEigenNrms);
  
   for(int e = 0; e < nev; e++) memcpy(&sortedHarVecs[ldm*e], &harVecs[ldm*( sorted_evals_cntr[e].eval_idx)], (ldm)*sizeof(Complex));

   return;
 }

 void GmresDRArgs::RestartVH(cudaColorSpinorField &res, cudaColorSpinorField *Vm)
 {
   int cldn = Vm->EigvTotalLength() >> 1; //complex leading dimension
   int clen = Vm->EigvLength()      >> 1; //complex vector length

   for(int j = 0; j <= m; j++) //row index
   {
     Complex accum = 0.0;

     for(int i = 0; i < m; i++) //column index
     {
        accum += (H[ldm*i + j]*lsqSol[i]);
     }
     
     srtRes[j] -= accum;
   }

   zeroCuda(res);

   for(int i = 0; i <= m; i++) caxpyCuda(srtRes[i], Vm->Eigenvec(i), res);

   /**** Update Vm and H ****/
//   memcpy(&sortedHarVecs[ldm*(nev)], resC, ldm*sizeof(Complex)); 

   gmresdr_magma_args->RestartVH(Vm->V(), clen, cldn, Vm->Precision(), sortedHarVecs, H, ldm);//check ldm with internal (magma) ldm

   /*****REORTH V_{nev+1}:****/
   for(int j = 0; j < nev; j++)
   {
     Complex calpha = cDotProductCuda(Vm->Eigenvec(j), Vm->Eigenvec(nev));//
     caxpyCuda(-calpha, Vm->Eigenvec(j), Vm->Eigenvec(nev));
   }
         
   double dalpha = norm2(Vm->Eigenvec(nev));

   double tmpd=1.0e+00/sqrt(dalpha);

   axCuda(tmpd, Vm->Eigenvec(nev));

   //done.

   return;
 }


 void GmresDRArgs::PrepareDeflatedRestart(Complex *givensH, Complex *g)
 {
   //update initial values for the "short residual"
   memcpy(srtRes, g, (m+1)*sizeof(Complex));

   if(first_cycle_flag == false)
   { 
     memset(cH, 0, ldm*(m+1)*sizeof(Complex));
     //
     memset(harMat, 0, ldm*m*sizeof(Complex));
     //
     //now update conjugate matrix
     for(int j = 0 ; j < nev; j++)
     {
       for(int i = 0 ; i < nev+1; i++)
       {
         cH[ldm*i+j] = conj(H[ldm*j+i]);
       }
     }

     memcpy(qrH, H, ldm*nev*sizeof(Complex));

     gmresdr_magma_args->ComputeQR(nev, qrH, (nev+1), ldm, tauH);

     //extract triangular part to the givens matrix:
     for(int i = 0; i < nev; i++) memcpy(&givensH[ldm*i], &qrH[ldm*i], (i+1)*sizeof(Complex));

     gmresdr_magma_args->LeftConjZUNMQR(nev /*number of reflectors*/, 1 /*number of columns of mat*/, g, (nev+1) /*number of rows*/, ldm, qrH, ldm, tauH);
   }

   return;
 }

 void GmresDRArgs::PrepareGivens(Complex *givensH, const int col)
 {
   memcpy(&givensH[ldm*col], &H[ldm*col], (nev+1)*sizeof(Complex) );
   //
   gmresdr_magma_args->LeftConjZUNMQR(nev /*number of reflectors*/, 1 /*number of columns of mat*/, &givensH[ldm*col], (nev+1) /*number of rows*/, ldm, qrH, ldm, tauH);

   return;
 }

 void GmresDR::operator()(cudaColorSpinorField *out, cudaColorSpinorField *in) 
 {
     cudaColorSpinorField r(*in); //high precision residual 

     ColorSpinorParam csParam(*in);//create spinor parameters

     csParam.create = QUDA_ZERO_FIELD_CREATE;
     // 
     cudaColorSpinorField tmp(*in, csParam);
     //
     cudaColorSpinorField *tmp2_p;

     // tmp only needed for multi-gpu Wilson-like kernels
     tmp2_p = new cudaColorSpinorField(*in, csParam);

     cudaColorSpinorField &tmp2 = *tmp2_p;

     cudaColorSpinorField &x = *out;

     //Allocate Vm array:
     if(gmres_alloc == false)
     {
       printfQuda("\nAllocating resources for the GMRESDR solver...\n");

       //Create an eigenvector set:
       csParam.create   = QUDA_ZERO_FIELD_CREATE;
   
       csParam.setPrecision(gmres_space_prec);//GMRESDR internal Krylov subspace precision: may or may not coincide with eigenvector precision!.

       csParam.eigv_dim = param.m+1; // basis dimension (abusive notations!)

       Vm = new cudaColorSpinorField(csParam); //search space for Ritz vectors

       checkCudaError();
       printfQuda("\n..done.\n");
       
       gmres_alloc = true;
     }

   int m         = param.m;
   int ldH       = (m+1);
   int nev       = args->nev;
   //GMRES objects:
   //Givens rotated matrix (m+1, m):
   Complex *givensH = new Complex[ldH*m];//complex
    
   //Auxilary objects: 
   Complex *g = new Complex[(m+1)];

   //Givens coefficients:
   Complex *Cn = new Complex[m];
   //
   double *Sn = (double *) calloc(m, sizeof(double));//in fact, it's real
   //
   cudaColorSpinorField res(r);
   //
   zeroCuda(res);

   //Compute initial residual:
   const double normb = norm2(*in);  
   double stop = param.tol*param.tol* normb;	/* Relative to b tolerance */

   mat(r, *out, tmp, tmp2);
   //
   double r2 = xmyNormCuda(*in, r);//compute residual

   printf("\nInitial residual squred: %1.16e, source %1.16e, tolerance %1.16e\n", sqrt(r2), sqrt(normb), param.tol);

   //copy the first vector:
   double beta = sqrt(r2);
   //
   double r2_inv = 1.0 / beta;//check beta!
   //note that r2_inv is real..
   zeroCuda(Vm->Eigenvec(0));
   axpyCuda(r2_inv, r, Vm->Eigenvec(0));

   //set g-vector:
   g[0] = beta;

   args->PrepareDeflatedRestart(givensH, g);

   //Main GMRES loop:
   int j = 0;//here also a column index of H

   while((j < m) && (r2 > stop))
   {
     cudaColorSpinorField *Av = &Vm->Eigenvec(j+1);

     matSloppy(*Av, Vm->Eigenvec(j), tmp, tmp2);

     ///////////
     Complex h0 = cDotProductCuda(Vm->Eigenvec(0), *Av);//
     args->SetHessenbergElement(0, j, h0);
     //scale w:
     caxpyCuda(-h0, Vm->Eigenvec(0), *Av);
     //
#if 0 //to unify:
    //k = 0; //(nev = 0 for the first cycle)
    Complex h0;
    for (int i = 0; i <= k; i++)//i is a row index
    {
       h0 = cDotProductCuda(Vm->Eigenvec(i), *Av);//
       //Warning: column major format!!! (m+1) size of a column.
       //load columns:
       H[ldH*j+i] = h0;
       //
       conjH[ldH*i+j] = conj(h0);
       //
       caxpyCuda(-h0, Vm->Eigenvec(i), *Av);
    }

    if(not first_cycle, i.e. k != 0 )   
    {
    //Let's do Givens rotation:
       memcpy(&givensH[ldH*j], &H[ldH*j], (nev+1)*sizeof(Complex) );
       //
       leftConjZUNMQR(nev /*number of reflectors*/, 1 /*number of columns of mat*/, &givensH[ldH*j], (nev+1) /*number of rows*/, ldH, qrH, ldH, tauH);

       h0 = givensH[ldH*j+nev];
    }
#endif
     //
     for (int i = 1; i <= j; i++)//i is a row index
     {
       Complex h1 = cDotProductCuda(Vm->Eigenvec(i), *Av);//
       //load columns:
       args->SetHessenbergElement(i, j, h1);
       //scale:
       caxpyCuda(-h1, Vm->Eigenvec(i), *Av);

       //lets do Givens rotations:
       givensH[ldH*j+(i-1)]   =  conj(Cn[i-1])*h0 + Sn[i-1]*h1;
       //compute (j, i+1) element:
       h0          = -Sn[i-1]*h0 + Cn[i-1]*h1;
     }
     //Compute h(j+1,j):
     double h_jp1j = sqrt(norm2(*Av)); 

     //Update H-matrix (j+1) element:
     args->SetHessenbergElement((j+1), j, Complex(h_jp1j, 0.0));
     //
     axCuda(1. / h_jp1j, *Av);
    
     double inv_denom = 1.0 / sqrt(norm(h0)+h_jp1j*h_jp1j);
     //
     Cn[j] = h0 * inv_denom; 
     //
     Sn[j] = h_jp1j * inv_denom; 
     //produce diagonal element in G:
     givensH[ldH*j+j] = conj(Cn[j])*h0 + Sn[j]*h_jp1j; 
   
     //compute this in opposite direction:
     g[j+1] = -Sn[j]*g[j];  
     //
     g[j]   =  g[j] * conj(Cn[j]);
     //
     r2 = norm(g[j+1]);//stopping criterio
     //
     j += 1;

     printfQuda("GMRES residual: %1.15e ( iteration = %d)\n", sqrt(r2), j); 
   }//end of GMRES loop

   printf("\nDone for: %d iters, %1.15e last residual\n", j, sqrt(r2)); 

   //j -> m+1
   //update solution and final residual:
   args->UpdateSolution(x, Vm, givensH, ldH, g, j);

   mat(r, *out, tmp, tmp2);
   //
   r2 = xmyNormCuda(*in, r);//compute full precision residual

   printf("\nDone for: stage 0 (m = %d), true residual %1.15e\n", j, sqrt(r2));

//START RESTARTS:
   const int max_cycles = param.deflation_grid;

   int cycle_idx = 1;

   args->first_cycle_flag = false;

   while(cycle_idx < max_cycles /*&& !convergence(r2, stop)*/)
   {
     printf("\nRestart #%d\n", cycle_idx);

     args->ComputeHarmonicRitzPairs();

     args->RestartVH(res, Vm);

     memset(givensH, 0, ldH*m*sizeof(Complex));

     memset(g, 0 , (m+1)*sizeof(Complex));

     memset(Cn, 0 , m*sizeof(Complex));
     memset(Sn, 0 , m*sizeof(double));

     for(int i = 0; i <= nev ; i++ ) g[i] = cDotProductCuda(Vm->Eigenvec(i), res);//

     args->PrepareDeflatedRestart(givensH, g);

     //Main GMRES loop:
     int j = nev;//here also a column index of H

     while((j < m) && (r2 > stop))
     {
       //pointer aliasing:
       cudaColorSpinorField *Av = &Vm->Eigenvec(j+1);

       matSloppy(*Av, Vm->Eigenvec(j), tmp, tmp2);
       //
       Complex h0(0.0, 0.0);
       //
       for (int i = 0; i <= nev; i++)//i is a row index
       {
          h0 = cDotProductCuda(Vm->Eigenvec(i), *Av);//
          //Warning: column major format!!! (m+1) size of a column.
          //load columns:
          args->SetHessenbergElement(i, j, h0);
          //
          caxpyCuda(-h0, Vm->Eigenvec(i), *Av);
       }

       //Let's do Givens rotation:
       args->PrepareGivens(givensH, j);

       h0 = givensH[ldH*j+nev];

       for(int i = (nev+1); i <= j; i++)
       {
          Complex h1 = cDotProductCuda(Vm->Eigenvec(i), *Av);//
          //Warning: column major format!!! (m+1) size of a column.
          //load columns:
          args->SetHessenbergElement(i, j, h1);
          //
          caxpyCuda(-h1, Vm->Eigenvec(i), *Av);

          //Lets do Givens rotations:
          givensH[ldH*j+(i-1)]   =  conj(Cn[i-1])*h0 + Sn[i-1]*h1;
          //compute (j, i+1) element:
          h0  = -Sn[i-1]*h0 + Cn[i-1]*h1;//G[(m+1)*j+i+1]
       }
       //Compute h(j+1,j):
       //
       double h_jp1j = sqrt(norm2(*Av)); 

       //Update H-matrix (j+1) element:
       args->SetHessenbergElement((j+1), j, Complex(h_jp1j,0.0));
       //
       axCuda(1. / h_jp1j, *Av);
 
       double inv_denom = 1.0 / sqrt(norm(h0)+h_jp1j*h_jp1j);
       //
       Cn[j] = h0 * inv_denom; 
       //
       Sn[j] = h_jp1j * inv_denom; 
       //produce diagonal element in G:
       givensH[ldH*j+j] = conj(Cn[j])*h0 + Sn[j]*h_jp1j; 
   
       //compute this in opposite direction:
       g[j+1] = -Sn[j]*g[j];  
       //
       g[j]   =  g[j] * conj(Cn[j]);
       //
       r2 = norm(g[j+1]);//stop criterio
       //
       j += 1;
 
       printfQuda("GMRES residual: %1.15e ( iteration = %d)\n", sqrt(r2), j); 
     }//end of main loop.

     args->UpdateSolution(x, Vm, givensH, ldH, g, j);

     mat(r, *out, tmp, tmp2);
     //
     r2 = xmyNormCuda(*in, r);//compute full precision residual

     printf("\nDone for: stage 0 (m = %d), true residual %1.15e\n", j, sqrt(r2));

     cycle_idx += 1;

   }//end of deflated restarts

   printfQuda("\nClean main resources\n");

   delete [] givensH;

   delete [] g;

   delete [] Cn;

   free(Sn);

   printfQuda("\n..done.\n");

   delete tmp2_p;
 
 }//end of operator()

} // namespace quda