#include <transfer.h>
#include <blas_quda.h>

#include <transfer.h>
#include <multigrid.h>
#include <malloc_quda.h>

#include <iostream>
#include <algorithm>
#include <vector>

#include <nvToolsExt.h>

namespace quda {

  // this determines where the prolongation / restriction will take place
  static bool gpu_transfer = false;

  void setTransferGPU(bool use_gpu) { gpu_transfer = use_gpu; }
  /*
  * for the staggered case, there is no spin blocking, 
  * however we do even-odd to preserve chirality (that is straightforward)
  * spin_bs = 2 for the top level and = 1 otherwise. For staggered it's always 1, however.
  */

  Transfer::Transfer(const std::vector<ColorSpinorField*> &B, int Nvec, int *geo_bs, int spin_bs = 1) 
    : B(B), Nvec(Nvec), V(0), V_h(0), V_d(0), tmp2(0), tmp3(0), geo_bs(0), 
      fine_to_coarse_h(0), coarse_to_fine_h(0), 
      fine_to_coarse_d(0), coarse_to_fine_d(0), 
      spin_bs(spin_bs), spin_map(0), block_parity_h(0), block_parity_d(0), block_parity(0)
  {
    int ndim = B[0]->Ndim();
    this->geo_bs = new int[ndim];
    for (int d = 0; d < ndim; d++)
    {
      this->geo_bs[d] = geo_bs[d];
      if(B[0]->Nspin() == 1 && geo_bs[d] < 4)
         errorQuda("Too small block size in %d direction for spin=1 field (must be >= 4) %d\n", d, geo_bs[d]);
    }

    if (B[0]->X(0) == geo_bs[0]) 
      errorQuda("X-dimension length %d cannot block length %d\n", B[0]->X(0), geo_bs[0]);

    for (int d = 0; d < ndim; d++) 
      if ( (B[0]->X(d)/geo_bs[d]+1)%2 == 0)
	errorQuda("Indexing does not (yet) support odd coarse dimensions: X(%d) = %d\n", d, B[d]->X(d)/geo_bs[d]);

    printfQuda("Transfer: using block size %d", geo_bs[0]);
    for (int d=1; d<ndim; d++) printfQuda(" x %d", geo_bs[d]);
    printfQuda("\n");

    // create the storage for the final block orthogonal elements
    ColorSpinorParam param(*B[0]); // takes the geometry from the null-space vectors

    // the ordering of the V vector is defined by these parameters and
    // the Packed functions in ColorSpinorFieldOrder

    param.nSpin = B[0]->Nspin(); // spin has direct mapping
    param.nColor = B[0]->Ncolor()*Nvec; // nColor = number of colors * number of vectors
    param.create = QUDA_ZERO_FIELD_CREATE;
    // the V field is defined on all sites regardless of B field (maybe the B fields are always full?)
    if (param.siteSubset == QUDA_PARITY_SITE_SUBSET) {
      //keep it the same for staggered:
      param.siteSubset = QUDA_FULL_SITE_SUBSET;
      param.x[0] *= 2;
    }

    if (param.nSpin == 1){
      //param.fieldOrder = QUDA_FLOAT2_FIELD_ORDER;//host orthogonalization is implemented for SPIN_COLOR order only!
      printfQuda("Transfer: creating V field with location %d\n", param.location);
    }
    else{
      printfQuda("Transfer: creating V field with basis %d with location %d\n", param.gammaBasis, param.location);    
    }

    // for cpu transfer this is the V field, for gpu it's just a temporary until we port the block orthogonalization
    V_h = ColorSpinorField::Create(param);

    if (gpu_transfer == true) {
      param.location = QUDA_CUDA_FIELD_LOCATION;
      param.fieldOrder = QUDA_FLOAT2_FIELD_ORDER;//ok for staggered
    } 

    V_d = gpu_transfer ? ColorSpinorField::Create(param) : 0;
    V = gpu_transfer ? V_d : V_h;

    printfQuda("Transfer: filling V field with zero\n");
    fillV(*V_h); // copy the null space vectors into V

    // create temporaries we use to enable us to change basis and for cpu<->gpu transfers
    if (gpu_transfer) {
      param = ColorSpinorParam(*B[0]);
      param.location = QUDA_CUDA_FIELD_LOCATION;
      param.fieldOrder = QUDA_FLOAT2_FIELD_ORDER;
      param.create = QUDA_NULL_FIELD_CREATE;
      tmp2 = ColorSpinorField::Create(param);

      // used for basis changing
      tmp3 = tmp2->CreateCoarse(geo_bs, spin_bs, Nvec);//Remark: spin_bs = 1 for staggered, and even-odd blocking presumed
    } else {
      param = ColorSpinorParam(*B[0]);

      // used for cpu<->gpu transfers
      param.create = QUDA_NULL_FIELD_CREATE;
      tmp2 = ColorSpinorField::Create(param);

      // useful to have around
      tmp3 = tmp2->CreateCoarse(geo_bs, spin_bs, Nvec);//Remark: spin_bs = 1 for staggered, and even-odd blocking presumed
    }

    // allocate and compute the fine-to-coarse and coarse-to-fine site maps
    fine_to_coarse_h = static_cast<int*>(safe_malloc(B[0]->Volume()*sizeof(int)));
    coarse_to_fine_h = static_cast<int*>(safe_malloc(B[0]->Volume()*sizeof(int)));

    if (gpu_transfer) {
      fine_to_coarse_d = static_cast<int*>(device_malloc(B[0]->Volume()*sizeof(int)));
      coarse_to_fine_d = static_cast<int*>(device_malloc(B[0]->Volume()*sizeof(int)));
      fine_to_coarse = fine_to_coarse_d;
      coarse_to_fine = coarse_to_fine_d;
    } else {
      fine_to_coarse = fine_to_coarse_h;
      coarse_to_fine = coarse_to_fine_h;
    }

    createGeoMap(geo_bs);

    // allocate the fine-to-coarse spin map (don't need it for the top level staggered lattice.)
    //if (param.nSpin == 1 && spin_bs != 1 ) { errorQuda("Error: wrong spin block size for the top level staggered lattice! (%d) ", spin_bs);}

    if ( param.nSpin != 1 )
    {
      spin_map = static_cast<int*>(safe_malloc(B[0]->Nspin()*sizeof(int)));
      createSpinMap(spin_bs);
    }

    // orthogonalize the blocks
    printfQuda("Transfer: block orthogonalizing\n");
    BlockOrthogonalize(*V_h, Nvec, geo_bs, fine_to_coarse_h, spin_bs);
    printfQuda("Transfer: V block orthonormal check %g\n", blas::norm2(*V_h));
    //for (int x=0; x<Vh->Volume(); x++) static_cast<cpuColorSpinorField*>(Vh)->PrintVector(x);
    //printfQuda("Vh->Volume() = %d Vh->Nspin() = %d Vh->Ncolor = %d Vh->Length() = %d\n", Vh->Volume(), Vh->Nspin(), Vh->Ncolor(), Vh->Length());

    if (gpu_transfer) *V_d = *V_h;
  }

  Transfer::~Transfer() {
    if (spin_map) host_free(spin_map);
    if (coarse_to_fine_d) device_free(coarse_to_fine_d);
    if (fine_to_coarse_d) device_free(fine_to_coarse_d);
    if (coarse_to_fine_h) host_free(coarse_to_fine_h);
    if (fine_to_coarse_h) host_free(fine_to_coarse_h);
    if (V_h) delete V_h;
    if (V_d && gpu_transfer) delete V_d;
    if (tmp2) delete tmp2;
    if (tmp3) delete tmp3;
  }

  void Transfer::fillV(ColorSpinorField &V) { 
    FillV(V, B, Nvec);  //printfQuda("V fill check %e\n", norm2(*V));
  }

  struct Int2 {
    int x, y;
    Int2() : x(0), y(0) { } 
    Int2(int x, int y) : x(x), y(y) { } 
    
    bool operator<(const Int2 &a) const {
      return (x < a.x) ? true : (x==a.x && y<a.y) ? true : false;
    }
  };

  // compute the fine-to-coarse site map
  void Transfer::createGeoMap(int *geo_bs) {

    int x[QUDA_MAX_DIM];

    // use tmp3 since it is a spinor with coarse geometry, and use its OffsetIndex member function
    ColorSpinorField &coarse(*tmp3);

    // compute the coarse grid point for every site (assuming parity ordering currently)
    for (int i=0; i<tmp2->Volume(); i++) {
      // compute the lattice-site index for this offset index
      tmp2->LatticeIndex(x, i);
      
      //printfQuda("fine idx %d = fine (%d,%d,%d,%d), ", i, x[0], x[1], x[2], x[3]);

      // compute the corresponding coarse-grid index given the block size
      for (int d=0; d<tmp2->Ndim(); d++) x[d] /= geo_bs[d];

      // compute the coarse-offset index and store in fine_to_coarse
      int k;
      coarse.OffsetIndex(k, x); // this index is parity ordered
      fine_to_coarse_h[i] = k;
      //printfQuda("coarse after (%d,%d,%d,%d), coarse idx %d\n", x[0], x[1], x[2], x[3], k);
    }

    // now create an inverse-like variant of this

    std::vector<Int2> geo_sort(B[0]->Volume());
    for (unsigned int i=0; i<geo_sort.size(); i++) geo_sort[i] = Int2(fine_to_coarse_h[i], i);
    std::sort(geo_sort.begin(), geo_sort.end());
    for (unsigned int i=0; i<geo_sort.size(); i++) coarse_to_fine_h[i] = geo_sort[i].y;

    if (gpu_transfer) {
      cudaMemcpy(fine_to_coarse_d, fine_to_coarse_h, B[0]->Volume()*sizeof(int), cudaMemcpyHostToDevice);
      cudaMemcpy(coarse_to_fine_d, coarse_to_fine_h, B[0]->Volume()*sizeof(int), cudaMemcpyHostToDevice);
      checkCudaError();
    }

  }

  // compute the fine spin to coarse spin map
  void Transfer::createSpinMap(int spin_bs) {

    for (int s=0; s<B[0]->Nspin(); s++) {
      spin_map[s] = s / spin_bs;//Transfer from coarse to coarsecoarse grid : spin_bs = 1 => direct mapping
    }

  }

  // apply the prolongator
  void Transfer::P(ColorSpinorField &out, const ColorSpinorField &in) const {


    ColorSpinorField *input = const_cast<ColorSpinorField*>(&in);
    ColorSpinorField *output = &out;

    const ColorSpinorField *V = gpu_transfer ? V_d : V_h;

    if (gpu_transfer) {
      if (in.Location() == QUDA_CPU_FIELD_LOCATION) input = tmp3;
      if (out.Location() == QUDA_CPU_FIELD_LOCATION ||
	  out.GammaBasis() != V->GammaBasis()) output = tmp2;
    } else {
      output = (out.Location() == QUDA_CUDA_FIELD_LOCATION) ? tmp2 : &out;
    }

    *input = in; // copy result to input field (aliasing handled automatically)
    
    if ((V->Nspin() != 1) && ((output->GammaBasis() != V->GammaBasis()) || (input->GammaBasis() != V->GammaBasis()))){
      errorQuda("Cannot apply prolongator using fields in a different basis from the null space (%d,%d) != %d",
		output->GammaBasis(), in.GammaBasis(), V->GammaBasis());
    }

    Prolongate(*output, *input, *V, Nvec, fine_to_coarse, spin_map);

    out = *output; // copy result to out field (aliasing handled automatically)

  }

  // apply the restrictor
  void Transfer::R(ColorSpinorField &out, const ColorSpinorField &in) const {


    ColorSpinorField *input = &const_cast<ColorSpinorField&>(in);
    ColorSpinorField *output = &out;

    if (gpu_transfer) {
      if (out.Location() == QUDA_CPU_FIELD_LOCATION) output = tmp3;
      if (in.Location() == QUDA_CPU_FIELD_LOCATION ||
	  in.GammaBasis() != V->GammaBasis()) input = tmp2;
    } else {
      if (in.Location() == QUDA_CUDA_FIELD_LOCATION) input = tmp2;
    }

    *input = in; // copy result to input field (aliasing handled automatically)  
    
    if ((V->Nspin() != 1) && (output->GammaBasis() != V->GammaBasis()) || (input->GammaBasis() != V->GammaBasis())))
      errorQuda("Cannot apply restrictor using fields in a different basis from the null space (%d,%d) != %d",
		out.GammaBasis(), input->GammaBasis(), V->GammaBasis());

    Restrict(*output, *input, *V, Nvec, fine_to_coarse, coarse_to_fine, spin_map);

    out = *output; // copy result to out field (aliasing handled automatically)

  }

} // namespace quda
