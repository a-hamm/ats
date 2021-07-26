/* -*-  mode: c++; indent-tabs-mode: nil -*- */

/* -----------------------------------------------------------------------------
This is the overland flow component of ATS.
License: BSD
Authors: Daniil Svyatsky (dasvyat@lanl.gov), Xu Chonggang
----------------------------------------------------------------------------- */

#ifndef PK_FATES_HH_
#define PK_FATES_HH_

#include "PK_Factory.hh"
#include "pk_physical_default.hh"
#include "ISO_Fortran_binding.h"

#include "Teuchos_ParameterList.hpp"
#include "Teuchos_RCP.hpp"
#include "Epetra_SerialDenseVector.h"

#include "VerboseObject.hh"
#include "TreeVector.hh"

#include <string.h>

namespace Amanzi {
namespace BGC {

  typedef struct {  
    int nlevbed;
    int nlevdecomp;
    int patchno;
    int altmax_lastyear_indx_col;
    double temp_veg24_patch;
    double latdeg, londeg;
  } site_info;

  typedef struct {  
    double dayl_factor;  // scalar (0-1) for daylength
    double esat_tv;      // saturation vapor pressure at t_veg (Pa)
    double eair;         // vapor pressure of canopy air (Pa)
    double oair;         // Atmospheric O2 partial pressure (Pa)
    double cair;         // Atmospheric CO2 partial pressure (Pa)
    double rb;           // boundary layer resistance (s/m)
    double t_veg;        // vegetation temperature (Kelvin)
    double tgcm;         // air temperature at agcm reference height (Kelvin)
    double solad[2]; //direct radiation (W/m**2); 1=visible lights; 2=near infrared radition
    double solai[2]; //diffuse radiation (W/m**2); 1=visible lights; 2=near infrared radition
    double albgrd[2]; //!ground albedo (direct) 1=visiable; 2=near infrared (nir)
    double albgri[2]; //ground albedo (diffuse) 1=visiable; 2=near infrared (nir)    
  } PhotoSynthesisInput;
  
  typedef struct {  
     int current_year;      //Current year
     int current_month;    //day of month
     int current_day;      //day of month
     int current_tod;     // time of day (seconds past 0Z)
     int current_date;    // YYYYMMDD
     int reference_date;  // YYYYMMDD
     double  model_day;    // elapsed days between current date and ref
     int day_of_year;     // The integer day of the year
     int days_per_year;   // The HLM controls time, some HLMs may 
                           // include a leap    
  } TimeInput;


#ifdef __cplusplus
  extern "C"
  {
#endif
    void init_ats_fates(int*, site_info*);
    void init_soil_depths(int*, int*, site_info*, double*, double*, double*, double*);
    void init_coldstart(int* );
    void fatessetmasterproc(int*);
    void fatessetinputfiles(CFI_cdesc_t * clm, CFI_cdesc_t * fates);    
    void fatesreadparameters();
    void fatesreadpfts();
    //void fatesreportparameters(int* proc);
    void set_fates_global_elements();
    void get_nlevsclass(int*);
    void get_numpft(int*);
    void get_nlevage(int*);
    void dynamics_driv_per_site(int*, int*, site_info*, TimeInput*, double*,
                                double*, double*, double*, double*,
                                double*);
    void wrap_btran(int*, int*, double*, double*, double*, double*, double*, double*);
    void wrap_photosynthesis(int*, double*, double*, int*, double*, PhotoSynthesisInput*);
    void wrap_sunfrac(int*, int* array_size, double *forc_solad, double *forc_solai);
    void wrap_canopy_radiation(int*, double* jday, int* array_size, double* albgrd, double *albgri);    
    void wrap_accumulatefluxes(int*, double*);
    void prep_canopyfluxes(int*); 
        
    void calculate_biomass(int* nc, double*  ats_biomass_array, double* ats_bstore_pa_array, int nsites, int num_scls);
    void calculate_gpp(int* nc, double* ats_gpp_pa_array, double *ats_gpp_si_array, int nsites, int num_scls, double* dt_tstep) ;
    void calculate_mortality(int *nc, double *ats_mortality_si_pft_array, int nsites, int num_pft, int nlevsclass);
    void calculate_lai(int *nc, double *ats_lai_si_age_array, int nsites, int num_scls);
      
  
#ifdef __cplusplus
  } // extern "C"
#endif 

  class FATES_PK : public PK_Physical_Default  {

  public:
    FATES_PK (Teuchos::ParameterList& FElist,
              const Teuchos::RCP<Teuchos::ParameterList>& plist,
              const Teuchos::RCP<State>& S,
              const Teuchos::RCP<TreeVector>& solution);
  
    // Virtual destructor
    virtual ~FATES_PK() {}

    // main methods
    // -- Initialize owned (dependent) variables.
    //virtual void setup(const Teuchos::Ptr<State>& S);
    virtual void Setup(const Teuchos::Ptr<State>& S);

    // -- Initialize owned (dependent) variables.
    //virtual void initialize(const Teuchos::Ptr<State>& S);
    virtual void Initialize(const Teuchos::Ptr<State>& S);


    // -- Update diagnostics for vis.
    virtual void CalculateDiagnostics(const Teuchos::RCP<State>& S){}; 

    virtual bool AdvanceStep(double t_old, double t_new, bool reinit);
    
    // -- Commit any secondary (dependent) variables.
    virtual void CommitStep(double t_old, double t_new, const Teuchos::RCP<State>& S);

    // -- provide a timestep size
    virtual double get_dt();

    virtual void set_dt(double dt) {
      dt_ = dt;
    }

  protected:


    void FieldToColumn_(AmanziMesh::Entity_ID col, const Epetra_Vector& vec,
                      double* col_vec, int ncol);
    void ColDepthDz_(AmanziMesh::Entity_ID col,
                     Teuchos::Ptr<Epetra_SerialDenseVector> depth,
                     Teuchos::Ptr<Epetra_SerialDenseVector> dz);


    double dt_, dt_photosynthesis_, dt_site_dym_;
    double t_photosynthesis_, t_site_dym_;
    
    bool surface_only_, salinity_on_, compute_avr_ponded_depth_;
    Teuchos::RCP<const AmanziMesh::Mesh> mesh_surf_, mesh_domain_;
    Key domain_surf_;
    Key trans_key_;
    Key precip_key_, air_temp_key_, humidity_key_, wind_key_, co2a_key_;
    Key poro_key_, sat_key_, suc_key_, soil_temp_key_;
    Key met_decomp_key_, cel_decomp_key_, lig_decomp_key_;
    Key longwave_key_, incident_rad_key_;
    Key salinity_key_, ponded_depth_key_;
    Key transpiration_beta_factor_key_;
    Key gross_primary_prod_key_pa_, gross_primary_prod_key_si_;
    Key leaf_area_key_;
    Key storage_biomass_key_;
    Key mortality_key_;
    int ncomp_salt_;

    std::vector<double> t_soil_;  // soil temperature
    std::vector<double> vsm_; // volumetric soil moisture vsm_ = S * poro;
    std::vector<double> poro_; // porosity
    std::vector<double> eff_poro_; //effective porosity  = porosity - vol_ice 
    std::vector<double> suc_; //suction head
    std::vector<double> salinity_; //suction head

    int patchno_, nlevdecomp_, nlevsclass_, numpft_, nlevage_;
    int ncells_owned_, ncells_per_col_, clump_;
    int masterproc_;
    std::vector<site_info> site_;
    TimeInput time_input_;

  // factory registration
  static RegisteredPKFactory<FATES_PK> reg_;
};

}  // namespace Vegetation
}  // namespace Amanzi

#endif
