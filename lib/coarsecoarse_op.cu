#include <transfer.h>
#include <color_spinor_field.h>
#include <color_spinor_field_order.h>
#include <gauge_field.h>
#include <gauge_field_order.h>
#include <complex_quda.h>

namespace quda {

  //Compute "coarse coarse" UV
  //FIXME: Should be merged with computeUV to avoid code duplication.  Use C++ traits.
  template<typename Float, int dir, typename F, typename fineGauge>
  void computeUVcoarse(F &UV, const F &V, const fineGauge &G, int ndim, const int *x_size, int s_col) {
        
    int coord[QUDA_MAX_DIM];
    for (int parity=0; parity<2; parity++) {
      int x_cb = 0;
      for (coord[3]=0; coord[3]<x_size[3]; coord[3]++) {
        for (coord[2]=0; coord[2]<x_size[2]; coord[2]++) {
          for (coord[1]=0; coord[1]<x_size[1]; coord[1]++) {
            for (coord[0]=0; coord[0]<x_size[0]/2; coord[0]++) {
              int coord_tmp = coord[dir];

              //Shift the V field w/respect to G (must be on full field coords)
              int oddBit = (parity + coord[1] + coord[2] + coord[3])&1;
              //if (dir==0) coord[0] = 2*coord[0] + oddBit;
              coord[0] = 2*coord[0] + oddBit;
	      int offset_index = 0;	
	      UV.OffsetIndex(offset_index,coord);
              //printfQuda("parity = %d x_cb = %d UV.OffsetIndex = %d\n",parity, x_cb, offset_index);
              coord[dir] = (coord[dir]+1)%x_size[dir];
              V.OffsetIndex(offset_index,coord);
              //if (dir==0) coord[0] /= 2;
              coord[0] /= 2;
              int y_cb = ((coord[3]*x_size[2]+coord[2])*x_size[1]+coord[1])*(x_size[0]/2) + coord[0];
              //printfQuda("parity = %d y_cb = %d V.OffsetIndex = %d\n",(parity+1)&1, y_cb,offset_index);


                for(int s = 0; s < V.Nspin(); s++) {  //Fine Spin row
                    for(int ic_c = 0; ic_c < V.Nvec(); ic_c++) {  //Coarse Color
                      for(int ic = 0; ic < G.NcolorCoarse(); ic++) { //Fine Color rows of gauge field
                        for(int jc = 0; jc < G.NcolorCoarse(); jc++) {  //Fine Color columns of gauge field
			 //printfQuda("UV(%d,%d,%d,%d,%d)= %e %e\n",parity, x_cb, s, ic, ic_c, UV(parity, x_cb, s, ic, ic_c).real(), UV(parity, x_cb, s, ic, ic_c).imag());
                         //printfQuda("V(%d,%d,%d,%d,%d)= %e %e\n",(parity+1)&1, y_cb, s_col, jc, ic_c, V((parity+1)&1, y_cb, s, jc, ic_c).real(), V((parity+1)&1, y_cb, s, jc, ic_c).imag());
			 //printfQuda("G(%d,%d,%d,%d,%d,%d,%d)= %e %e\n",dir,parity,x_cb,ic,jc,s,s_col,G(dir,parity,x_cb,ic,jc,s,s_col).real(),G(dir,parity,x_cb,ic,jc,s,s_col).imag());
                          UV(parity, x_cb, s, ic, ic_c) += G(dir, parity, x_cb, s, s_col, ic, jc) * V((parity+1)&1, y_cb, s_col, jc, ic_c);
                         //printfQuda("UV(%d,%d,%d,%d,%d)= %e %e\n",parity, x_cb, s, ic, ic_c, UV(parity, x_cb, s, ic, ic_c).real(), UV(parity, x_cb, s, ic, ic_c).imag());
                        }  //Fine color columns
                      }  //Fine color rows
                    }  //Coarse color
                }  //Fine Spin row

              coord[dir] = coord_tmp; //restore
              x_cb++;
            }
          }
        }
      }
    } // parity

  }  //UV

  template<typename Float, int dir, typename F, typename coarseGauge, typename fineGauge>
  void computeVUVcoarse(coarseGauge &Y, coarseGauge &X, const F &UV, const F &V, 
                  const fineGauge &G, const int *x_size, 
                  const int *xc_size, const int *geo_bs, int spin_bs, int s_col) {

    const int nDim = 4;
    const Float half = 0.5;
    int coord[QUDA_MAX_DIM];
    int coord_coarse[QUDA_MAX_DIM];
    int coarse_size = 1;
    for(int d = 0; d<nDim; d++) coarse_size *= xc_size[d];
    //for(int d = 0; d<nDim; d++) printfQuda("geo_bs[%d] = %d\n",d,geo_bs[d]);
    for (int parity=0; parity<2; parity++) {
      int x_cb = 0;
      for (coord[3]=0; coord[3]<x_size[3]; coord[3]++) {
        for (coord[2]=0; coord[2]<x_size[2]; coord[2]++) {
          for (coord[1]=0; coord[1]<x_size[1]; coord[1]++) {
            for (coord[0]=0; coord[0]<x_size[0]/2; coord[0]++) {

              int oddBit = (parity + coord[1] + coord[2] + coord[3])&1;
              coord[0] = 2*coord[0] + oddBit;
              for(int d = 0; d < nDim; d++) coord_coarse[d] = coord[d]/geo_bs[d];

              //Check to see if we are on the edge of a block, i.e.
              //if this color matrix connects adjacent blocks.  If
              //adjacent site is in same block, M = X, else M = Y
              const bool isDiagonal = ((coord[dir]+1)%x_size[dir])/geo_bs[dir] == coord_coarse[dir] ? true : false;
              coarseGauge &M =  isDiagonal ? X : Y;
              const int dim_index = isDiagonal ? 0 : dir;
              
              int coarse_parity = 0;
              for (int d=0; d<nDim; d++) coarse_parity += coord_coarse[d];
              coarse_parity &= 1;
              coord_coarse[0] /= 2;
              int coarse_x_cb = ((coord_coarse[3]*xc_size[2]+coord_coarse[2])*xc_size[1]+coord_coarse[1])*(xc_size[0]/2) + coord_coarse[0];
              //printfQuda("x_cb = %d parity = %d coarse_x_cb = %d, coarse_parity = %d isDiagonal=%d\n",x_cb, parity, coarse_x_cb, coarse_parity, isDiagonal);
              //printf("(%d,%d)\n", coarse_x_cb, coarse_parity);

              coord[0] /= 2;
	      //printfQuda("V.Nspin() = %d, Y.NcolorCoarse() = %d, G.NcolorCoarse() = %d\n", V.Nspin(), Y.NcolorCoarse(), G.NcolorCoarse());

                for(int s = 0; s < V.Nspin(); s++) { //Loop over fine spin row

                    for(int ic_c = 0; ic_c < Y.NcolorCoarse(); ic_c++) { //Coarse Color row
                      for(int jc_c = 0; jc_c < Y.NcolorCoarse(); jc_c++) { //Coarse Color column
		      	//printfQuda("s=%d s_col=%d ic_c = %d jc_c = %d G.NcolorCoarse() = %d\n",s,s_col,ic_c,jc_c,G.NcolorCoarse());

                        for(int ic = 0; ic < G.NcolorCoarse(); ic++) { //Sum over fine color
			//printfQuda("M(%d,%d,%d,%d,%d,%d,%d) = %e %e\n", dim_index,coarse_parity,coarse_x_cb, s, s_col, ic_c, jc_c, M(dim_index,coarse_parity,coarse_x_cb,s,s_col,ic_c,jc_c).real(), M(dim_index,coarse_parity,coarse_x_cb,s,s_col,ic_c,jc_c).imag());
			//printfQuda("isDiagonal = %d\n",isDiagonal);

                        M(dim_index,coarse_parity,coarse_x_cb,s,s_col,ic_c,jc_c) +=
                          conj(V(parity, x_cb, s, ic, ic_c)) * UV(parity, x_cb, s, ic, jc_c);
                        //printfQuda("M(%d,%d,%d,%d,%d,%d,%d) = %e %e\n", dim_index,coarse_parity,coarse_x_cb, s, s_col, ic_c, jc_c, M(dim_index,coarse_parity,coarse_x_cb,s,s_col,ic_c,jc_c).real(), M(dim_index,coarse_parity,coarse_x_cb,s,s_col,ic_c,jc_c).imag());
                        //printfQuda("UV(%d,%d,%d,%d,%d)= %e %e\n",parity, x_cb, s, ic, jc_c, UV(parity, x_cb, s, ic, jc_c).real(), UV(parity, x_cb, s, ic, jc_c).imag());
			//printfQuda("V(%d,%d,%d,%d,%d)= %e %e\n",parity, x_cb, s, ic, ic_c, V(parity, x_cb, s, ic, ic_c).real(), V(parity, x_cb, s, ic, ic_c).imag());

                        } //Fine color
                      } //Coarse Color column
                    } //Coarse Color row
                } //Fine spin
                 
              x_cb++;
            } // coord[0]
          } // coord[1]
        } // coord[2]
      } // coord[3]
    } // parity

  }

  //FIXME: This is the same as the version in coarse_op.cu, duplicated here.
  //Adds the reverse links to the coarse diagonal term, which is just
  //the conjugate of the existing coarse diagonal term but with
  //plus/minus signs for off-diagonal spin components
  template<typename Float, typename Gauge>
  void createCoarseLocal(Gauge &X, int ndim, const int *xc_size, double kappa) {
    const int nColor = X.NcolorCoarse();
    const int nSpin = X.NspinCoarse();
    Float kap = (Float) kappa;
    complex<Float> *Xlocal = new complex<Float>[nSpin*nSpin*nColor*nColor];
	
    for (int parity=0; parity<2; parity++) {
      for (int x_cb=0; x_cb<X.Volume()/2; x_cb++) {

	for(int s_row = 0; s_row < nSpin; s_row++) { //Spin row
	  for(int s_col = 0; s_col < nSpin; s_col++) { //Spin column
	    
	    //Copy the Hermitian conjugate term to temp location 
	    for(int ic_c = 0; ic_c < nColor; ic_c++) { //Color row
	      for(int jc_c = 0; jc_c < nColor; jc_c++) { //Color column
		//Flip s_col, s_row on the rhs because of Hermitian conjugation.  Color part left untransposed.
		Xlocal[((nSpin*s_col+s_row)*nColor+ic_c)*nColor+jc_c] = X(0,parity,x_cb,s_row, s_col, ic_c, jc_c);
	      }	
	    }
	  }
	}
	      
	for(int s_row = 0; s_row < nSpin; s_row++) { //Spin row
	  for(int s_col = 0; s_col < nSpin; s_col++) { //Spin column
	    
	    const Float sign = (s_row == s_col) ? 1.0 : -1.0;
		  
	    for(int ic_c = 0; ic_c < nColor; ic_c++) { //Color row
	      for(int jc_c = 0; jc_c < nColor; jc_c++) { //Color column
		//Transpose color part
		X(0,parity,x_cb,s_row,s_col,ic_c,jc_c) =  
		  -2*kap*(sign*X(0,parity,x_cb,s_row,s_col,ic_c,jc_c)+conj(Xlocal[((nSpin*s_row+s_col)*nColor+jc_c)*nColor+ic_c]));
	      } //Color column
	    } //Color row
	  } //Spin column
	} //Spin row

      } // x_cb
    } //parity

    delete []Xlocal;

  }

  //Zero out a field, using the accessor.
  template<typename Float, typename F>
  void setZero(F &f) {
    for(int parity = 0; parity < 2; parity++) {
      for(int x_cb = 0; x_cb < f.Volume()/2; x_cb++) {
	for(int s = 0; s < f.Nspin(); s++) {
	  for(int c = 0; c < f.Ncolor(); c++) {
	    for(int v = 0; v < f.Nvec(); v++) {
	      f(parity,x_cb,s,c,v) = (Float) 0.0;
	    }
	  }
	}
      }
    }
  }

  //Restrict the local clover term from the coarse lattice to the "coarse-coarse" lattice
  template<typename Float, typename coarseGauge, typename F, typename fineGauge>
  void createCoarseClover(coarseGauge &X, F &V, fineGauge &C, int ndim, const int *x_size, const int *xc_size, const int *geo_bs, int spin_bs)  {

    const int nDim = 4;
    const Float half = 0.5;
    int coord[QUDA_MAX_DIM];
    int coord_coarse[QUDA_MAX_DIM];
    int coarse_size = 1;
    for(int d = 0; d<nDim; d++) coarse_size *= xc_size[d];

    for (int parity=0; parity<2; parity++) {
      int x_cb = 0;
      for (coord[3]=0; coord[3]<x_size[3]; coord[3]++) {
        for (coord[2]=0; coord[2]<x_size[2]; coord[2]++) {
          for (coord[1]=0; coord[1]<x_size[1]; coord[1]++) {
            for (coord[0]=0; coord[0]<x_size[0]/2; coord[0]++) {

              int oddBit = (parity + coord[1] + coord[2] + coord[3])&1;
              coord[0] = 2*coord[0] + oddBit;
              for(int d = 0; d < nDim; d++) coord_coarse[d] = coord[d]/geo_bs[d];
              int coarse_parity = 0;
              for (int d=0; d<nDim; d++) coarse_parity += coord_coarse[d];
              coarse_parity &= 1;
              coord_coarse[0] /= 2;
              int coarse_x_cb = ((coord_coarse[3]*xc_size[2]+coord_coarse[2])*xc_size[1]+coord_coarse[1])*(xc_size[0]/2) + coord_coarse[0];
              
              coord[0] /= 2;

              //If Nspin = 4, then the clover term has structure C_{\mu\nu} = \gamma_{\mu\nu}C^{\mu\nu} 
	      //FIXME Not implemented yet.
              if(V.Nspin() == 4) {
		        errorQuda("Fine-> coarse clover not yet implemented");
              }
              //If Nspin != 4, then spin structure is a dense matrix
              //N.B. assumes that no further spin blocking is done in this case.
              else {
	        //printf("C.Ncolor() = %d C.NcolorCoarse() = %d\n",C.Ncolor(), C.NcolorCoarse());
                for(int s = 0; s < V.Nspin(); s++) { //Loop over fine spin row
                  for(int s_col = 0; s_col < V.Nspin(); s_col++) { //Loop over fine spin column

                    for(int ic_c = 0; ic_c < X.NcolorCoarse(); ic_c++) { //Coarse Color row
                      for(int jc_c = 0; jc_c < X.NcolorCoarse(); jc_c++) { //Coarse Color column
		      
                        for(int ic = 0; ic < C.NcolorCoarse(); ic++) { //Sum over fine color row
                          for(int jc = 0; jc < C.NcolorCoarse(); jc++) {  //Sum over fine color column
			  //printfQuda("coord[0] = %d, coord[1] = %d, coord[2] = %d, coord[3] = %d, x_size[0] = %d, x_size[1] = %d, x_size[2] = %d, x_size[3] = %d, coarse_parity=%d, coarse_x_cb = %d, s=%d, s_col=%d, ic_c = %d, jc_c = %d, parity = %d, x_cb = %d, ic = %d, jc = %d, V.Nspin() = %d\n",coord[0], coord[1], coord[2], coord[3], x_size[0], x_size[1], x_size[2], x_size[3], coarse_parity, coarse_x_cb, s, s_col, ic_c, jc_c, parity, x_cb, ic, jc, V.Nspin());
                          X(0,coarse_parity,coarse_x_cb,s,s_col,ic_c,jc_c) += conj(V(parity, x_cb, s, ic, ic_c)) * C(0, parity, x_cb, s, s_col, ic, jc) * V(parity, x_cb, s_col, jc, jc_c);
                          } //Fine color column
                        }  //Fine color row
                      } //Coarse Color column
                    } //Coarse Color row
                  }  //Fine spin column
                } //Fine spin
                 
              }

              x_cb++;
            } // coord[0]
          } // coord[1]
        } // coord[2]
      } // coord[3]
    } // parity

  }

  template<typename Float, typename F, typename coarseGauge, typename fineGauge>
  void calculateYcoarse(coarseGauge &Y, coarseGauge &X, F &UV, F &V, fineGauge &G, fineGauge &C, const int *x_size, double kappa) {
    if (UV.GammaBasis() != QUDA_DEGRAND_ROSSI_GAMMA_BASIS) errorQuda("Gamma basis not supported");
    const QudaGammaBasis basis = QUDA_DEGRAND_ROSSI_GAMMA_BASIS;

    if (G.Ndim() != 4) errorQuda("Number of dimensions not supported");
    const int nDim = 4;

    const int *xc_size = Y.Field().X();
    int geo_bs[QUDA_MAX_DIM]; 
    for(int d = 0; d < nDim; d++) geo_bs[d] = x_size[d]/xc_size[d];
    int spin_bs = V.Nspin()/Y.NspinCoarse();

    for(int d = 0; d < nDim; d++) {
      for(int s = 0; s < V.Nspin(); s++) {
        //First calculate UV
        setZero<Float,F>(UV);

        printfQuda("Computing %d UV and VUV s=%d\n", d, s);
        //Calculate UV and then VUV for this direction, accumulating directly into the coarse gauge field Y
        if (d==0) {
          computeUVcoarse<Float,0>(UV, V, G, nDim, x_size, s);
          computeVUVcoarse<Float,0>(Y, X, UV, V, G, x_size, xc_size, geo_bs, spin_bs, s);
        } else if (d==1) {
          computeUVcoarse<Float,1>(UV, V, G, nDim, x_size, s);
          computeVUVcoarse<Float,1>(Y, X, UV, V, G, x_size, xc_size, geo_bs, spin_bs, s);
        } else if (d==2) {
          computeUVcoarse<Float,2>(UV, V, G, nDim, x_size, s);
          computeVUVcoarse<Float,2>(Y, X, UV, V, G, x_size, xc_size, geo_bs, spin_bs, s);
        } else {
          computeUVcoarse<Float,3>(UV, V, G, nDim, x_size, s);
          computeVUVcoarse<Float,3>(Y, X, UV, V, G, x_size, xc_size, geo_bs, spin_bs, s);
        }
      }
      printf("UV2[%d] = %e\n", d, UV.norm2());
      printf("Y2[%d] = %e\n", d, Y.norm2(d));
    }

    printfQuda("Computing coarse diagonal\n");
    createCoarseLocal<Float>(X, nDim, xc_size, kappa);
   #if 0
    for(int parity = 0; parity <= 1; parity++) {
    for(int i = 0; i < X.Volume()/2; i++) {
      for(int s = 0; s < X.NspinCoarse(); s++) {
        for(int s_col = 0; s_col < X.NspinCoarse(); s_col++) {
          for(int c = 0; c < X.NcolorCoarse(); c++) {
            for(int c_col = 0; c_col < X.NcolorCoarse(); c_col++) {
              printf("d=%d parity=%d i=%d s=%d s_col=%d c=%d c_col=%d X = %e %e\n",nDim,parity,i,s,s_col,c,c_col,
                     X(0,parity,i,s,s_col,c,c_col).real(),
                     X(0,parity,i,s,s_col,c,c_col).imag());
            }
          }
        }
      }
    }
  }
#endif
    createCoarseClover<Float>(X, V, C, nDim, x_size, xc_size, geo_bs, spin_bs);
    printf("X2 = %e\n", X.norm2(0));

    #if 0
     for(int d = 0; d < 4; d++) {
     for(int parity = 0; parity <= 1; parity++) {
      for(int i = 0; i < Y.Volume()/2; i++) {
        for(int s = 0; s < Y.NspinCoarse(); s++) {
          for(int s_col = 0; s_col < Y.NspinCoarse(); s_col++) {
            for(int c = 0; c < Y.NcolorCoarse(); c++) {
              for(int c_col = 0; c_col < Y.NcolorCoarse(); c_col++) {
                printf("d=%d parity=%d i=%d s=%d s_col=%d c=%d c_col=%d Y(d) = %e %e\n",d,parity,i,s,s_col,c,c_col,Y(d,parity,i,s,s_col,c,c_col).real(),Y(d,parity,i,s,s_col,c,c_col).imag());
              }}}}}}}
    #endif
    #if 0
    for(int parity = 0; parity <= 1; parity++) {
    for(int i = 0; i < X.Volume()/2; i++) {
      for(int s = 0; s < X.NspinCoarse(); s++) {
        for(int s_col = 0; s_col < X.NspinCoarse(); s_col++) {
          for(int c = 0; c < X.NcolorCoarse(); c++) {
            for(int c_col = 0; c_col < X.NcolorCoarse(); c_col++) {
              printf("d=%d parity=%d i=%d s=%d s_col=%d c=%d c_col=%d X = %e %e\n",nDim,parity,i,s,s_col,c,c_col,
                     X(0,parity,i,s,s_col,c,c_col).real(),
                     X(0,parity,i,s,s_col,c,c_col).imag());
            }
          }
        }
      }
    }
  }
#endif


  }

  template <typename Float, QudaFieldOrder csOrder, QudaGaugeFieldOrder gOrder, 
            int fineColor, int fineSpin, int coarseColor, int coarseSpin>
  void calculateYcoarse(GaugeField &Y, GaugeField &X, ColorSpinorField &uv, const Transfer &T, const GaugeField &g, const GaugeField &clover, double kappa) {
    typedef typename colorspinor::FieldOrderCB<Float,fineSpin,fineColor,coarseColor,csOrder> F;
    typedef typename gauge::FieldOrder<Float,fineColor*fineSpin,fineSpin,gOrder> gFine;
    typedef typename gauge::FieldOrder<Float,coarseColor*coarseSpin,coarseSpin,gOrder> gCoarse;

    F vAccessor(const_cast<ColorSpinorField&>(T.Vectors()));
    F uvAccessor(const_cast<ColorSpinorField&>(uv));
    gFine gAccessor(const_cast<GaugeField&>(g));
    gFine cloverAccessor(const_cast<GaugeField&>(clover));
    gCoarse yAccessor(const_cast<GaugeField&>(Y));
    gCoarse xAccessor(const_cast<GaugeField&>(X)); 

    calculateYcoarse<Float>(yAccessor, xAccessor, uvAccessor, vAccessor, gAccessor, cloverAccessor, g.X(), kappa);
  }


  // template on the number of coarse degrees of freedom
  template <typename Float, QudaFieldOrder csOrder, QudaGaugeFieldOrder gOrder, int fineColor, int fineSpin>
  void calculateYcoarse(GaugeField &Y, GaugeField &X, ColorSpinorField &uv, const Transfer &T, const GaugeField &g, const GaugeField &clover, double kappa) {
    if (T.Vectors().Nspin()/T.Spin_bs() != 2) 
      errorQuda("Unsupported number of coarse spins %d\n",T.Vectors().Nspin()/T.Spin_bs());
    const int coarseSpin = 2;
    const int coarseColor = Y.Ncolor() / coarseSpin;

    if (coarseColor == 2) { 
      calculateYcoarse<Float,csOrder,gOrder,fineColor,fineSpin,2,coarseSpin>(Y, X, uv, T, g, clover, kappa);
    } else if (coarseColor == 24) {
      calculateYcoarse<Float,csOrder,gOrder,fineColor,fineSpin,24,coarseSpin>(Y, X, uv, T, g, clover, kappa);
    } else {
      errorQuda("Unsupported number of coarse dof %d\n", Y.Ncolor());
    }
  }

  // template on fine spin
  template <typename Float, QudaFieldOrder csOrder, QudaGaugeFieldOrder gOrder, int fineColor>
  void calculateYcoarse(GaugeField &Y, GaugeField &X, ColorSpinorField &uv, const Transfer &T, const GaugeField &g, const GaugeField &clover, double kappa) {
    if (uv.Nspin() == 2) {
      calculateYcoarse<Float,csOrder,gOrder,fineColor,2>(Y, X, uv, T, g, clover, kappa);
    } else {
      errorQuda("Unsupported number of spins %d\n", uv.Nspin());
    }
  }

  // template on fine colors
  template <typename Float, QudaFieldOrder csOrder, QudaGaugeFieldOrder gOrder>
  void calculateYcoarse(GaugeField &Y, GaugeField &X, ColorSpinorField &uv, const Transfer &T, const GaugeField &g, const GaugeField &clover, double kappa) {
    if (g.Ncolor()/uv.Nspin() == 24) {
      calculateYcoarse<Float,csOrder,gOrder,24>(Y, X, uv, T, g, clover, kappa);
    } else if (g.Ncolor()/uv.Nspin() == 2) {
      calculateYcoarse<Float,csOrder,gOrder,2>(Y, X, uv, T, g, clover, kappa);
    } else {
      errorQuda("Unsupported number of colors %d\n", g.Ncolor());
    }
  }

  template <typename Float, QudaFieldOrder csOrder>
  void calculateYcoarse(GaugeField &Y, GaugeField &X, ColorSpinorField &uv, const Transfer &T, const GaugeField &g, const GaugeField &clover, double kappa) {
    if (g.FieldOrder() == QUDA_QDP_GAUGE_ORDER) {
      calculateYcoarse<Float,csOrder,QUDA_QDP_GAUGE_ORDER>(Y, X, uv, T, g, clover, kappa);
    } else {
      errorQuda("Unsupported field order %d\n", g.FieldOrder());
    }
  }

  template <typename Float>
  void calculateYcoarse(GaugeField &Y, GaugeField &X, ColorSpinorField &uv, const Transfer &T, const GaugeField &g, const GaugeField &clover, double kappa) {
    if (T.Vectors().FieldOrder() == QUDA_SPACE_SPIN_COLOR_FIELD_ORDER) {
      calculateYcoarse<Float,QUDA_SPACE_SPIN_COLOR_FIELD_ORDER>(Y, X, uv, T, g, clover, kappa);
    } else {
      errorQuda("Unsupported field order %d\n", T.Vectors().FieldOrder());
    }
  }

  //Does the heavy lifting of creating the coarse color matrices Y
  void calculateYcoarse(GaugeField &Y, GaugeField &X, ColorSpinorField &uv, const Transfer &T, const GaugeField &g, const GaugeField &clover, double kappa) {
    if (X.Precision() != Y.Precision() || Y.Precision() != uv.Precision() || 
        Y.Precision() != T.Vectors().Precision() || Y.Precision() != g.Precision())
      errorQuda("Unsupported precision mix");

    printfQuda("Computing Y field......\n");
    if (Y.Precision() == QUDA_DOUBLE_PRECISION) {
      calculateYcoarse<double>(Y, X, uv, T, g, clover, kappa);
    } else if (Y.Precision() == QUDA_SINGLE_PRECISION) {
      calculateYcoarse<float>(Y, X, uv, T, g, clover, kappa);
    } else {
      errorQuda("Unsupported precision %d\n", Y.Precision());
    }
    printfQuda("....done computing Y field\n");
  }

  //Calculates the coarse color matrix and puts the result in Y.
  //N.B. Assumes Y, X have been allocated.
  void CoarseCoarseOp(const Transfer &T, GaugeField &Y, GaugeField &X, const cpuGaugeField &gauge, const cpuGaugeField &clover, double kappa) {
    QudaPrecision precision = Y.Precision();
    //First make a cpu gauge field from the cuda gauge field

    int pad = 0;
    #if 0
    GaugeFieldParam gf_param(gauge.X(), precision, gauge.Reconstruct(), pad, gauge.Geometry());
    gf_param.order = QUDA_QDP_GAUGE_ORDER;
    gf_param.fixed = gauge.GaugeFixed();
    gf_param.link_type = gauge.LinkType();
    gf_param.t_boundary = gauge.TBoundary();
    gf_param.anisotropy = gauge.Anisotropy();
    gf_param.gauge = NULL;
    gf_param.create = QUDA_NULL_FIELD_CREATE;
    gf_param.siteSubset = QUDA_FULL_SITE_SUBSET;

    cpuGaugeField g(gf_param);

    //Copy the cuda gauge field to the cpu
    gauge.saveCPUField(g, QUDA_CPU_FIELD_LOCATION);
    #endif


    //Create a field UV which holds U*V.  Has the same structure as V.
    ColorSpinorParam UVparam(T.Vectors());
    UVparam.create = QUDA_ZERO_FIELD_CREATE;
    cpuColorSpinorField uv(UVparam);

    calculateYcoarse(Y, X, uv, T, gauge, clover, kappa);
  }
  
} //namespace quda