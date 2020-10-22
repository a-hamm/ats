/* -*-  mode: c++; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------
 * ATS
 *
 * License: see $ATS_DIR/COPYRIGHT
 * Author: Daniil Svyatsky, Xu Chonggang
 *
 * DOCUMENT ME:

 *
 *
 * ------------------------------------------------------------------------- */


#include "fates_pk.hh"
#include "bgc_simple.hh"
#include "vegetation.hh"

namespace Amanzi {
namespace BGC {


FATES_PK::FATES_PK(Teuchos::ParameterList& pk_tree,
                   const Teuchos::RCP<Teuchos::ParameterList>& global_list,
                   const Teuchos::RCP<State>& S,
                   const Teuchos::RCP<TreeVector>& solution):
  PK(pk_tree, global_list,  S, solution),
  PK_Physical_Default(pk_tree, global_list,  S, solution),
  ncells_per_col_(-1)
{
  
  domain_surf_ = plist_->get<std::string>("surface domain name", "surface");

  Teuchos::ParameterList& FElist = S->FEList();

  //timestep size
  dt_ = plist_->get<double>("max time step", 1.e99);  
  dt_photosynthesis_ = plist_->get<double>("photosynthesis time step", 1800);
  dt_site_dym_ = plist_->get<double>("veg dynamics time step", 86400);
  surface_only_ = plist_->get<bool>("surface only", false);
  salinity_on_ = plist_->get<bool>("salinity", false);

}

void FATES_PK::Setup(const Teuchos::Ptr<State>& S){

  PK_Physical_Default::Setup(S);

  dt_ = plist_->get<double>("initial time step", 1.);

  // my mesh is the subsurface mesh, but we need the surface mesh, index by column, as well
  mesh_surf_ = S->GetMesh("surface");
  if (!surface_only_) mesh_=S->GetMesh();


  mesh_->build_columns();
  //  soil_part_name_ = plist_->get<std::string>("soil partition name");

  int iulog=10;
  int masterproc = 0;
  CFI_cdesc_t fatesdesc, clmdesc;
  int retval;

  if (!plist_->isParameter("fates parameter file")) {
    Errors::Message msg;
    msg << "No fates parameter file found in the parameter list for 'FATES'.\n";
    Exceptions::amanzi_throw(msg);
  }
  if (!plist_->isParameter("clm parameter file")) {
    Errors::Message msg;
    msg << "No clm parameter file found in the parameter list for 'FATES'.\n";
    Exceptions::amanzi_throw(msg);
  }
   
  std::string fates_file_str = plist_->get<std::string>("fates parameter file");
  char *fates_file_chr = &fates_file_str[0];
  std::string clm_file_str = plist_->get<std::string>("clm parameter file");
  char *clm_file_chr = &clm_file_str[0];

  retval = CFI_establish(&fatesdesc, fates_file_chr, CFI_attribute_other, CFI_type_char, fates_file_str.length(), 0, NULL);
  retval = CFI_establish(&clmdesc, clm_file_chr, CFI_attribute_other, CFI_type_char, clm_file_str.length(), 0, NULL); 
  fatessetmasterproc(&masterproc);
  fatessetinputfiles(&clmdesc, &fatesdesc);    
  fatesreadparameters();
  /*  ! Read in FATES parameter values early in the call sequence as well
    ! The PFT file, specifically, will dictate how many pfts are used
    ! in fates, and this will influence the amount of memory we
    ! request from the model, which is relevant in set_fates_global_elements()*/
  
  fatesreadpfts();

  /*   ------------------------------------------------------------------------
       Ask Fates to evaluate its own dimensioning needs.
       This determines the total amount of space it requires in its largest
       dimension.  We are currently calling that the "cohort" dimension, but
       it is really a utility dimension that captures the models largest
       size need.
       Sets:
       fates_maxElementsPerPatch
       fates_maxElementsPerSite (where a site is roughly equivalent to a column)
     
       (Note: fates_maxELementsPerSite is the critical variable used by CLM
       to allocate space)
     ------------------------------------------------------------------------*/

  set_fates_global_elements();
  
  // Get from FATES the total number of cohort size class bins output
  get_nlevsclass(&nlevsclass_);
  
  // requirements: primary variable
  S->RequireField(key_, name_)->SetMesh(mesh_surf_)
    ->SetComponent("cell", AmanziMesh::CELL, nlevsclass_);

  patchno_ = plist_->get<int>("number of patches", 10);
  nlevdecomp_ = plist_->get<int>("number of decomposition levels", 1);

  // met_decomp_key_ = Keys::getKey(domain_surf_,"decomp_cpools_met");
  // if (!S->HasField(met_decomp_key_)){
  //   S->RequireField(met_decomp_key_, name_)->SetMesh(mesh_surf_)
  //     ->SetComponent("cell", AmanziMesh::CELL, nlevdecomp_);
  // }

  // cel_decomp_key_ = Keys::getKey(domain_surf_,"decomp_cpools_cel");
  // if (!S->HasField(cel_decomp_key_)){
  //   S->RequireField(cel_decomp_key_, name_)->SetMesh(mesh_surf_)
  //     ->SetComponent("cell", AmanziMesh::CELL, nlevdecomp_);
  // }

  // lig_decomp_key_ = Keys::getKey(domain_surf_,"decomp_cpools_lig");
  // if (!S->HasField(lig_decomp_key_)){
  //   S->RequireField(lig_decomp_key_, name_)->SetMesh(mesh_surf_)
  //     ->SetComponent("cell", AmanziMesh::CELL, nlevdecomp_);
  // }


  precip_key_ = Keys::getKey(domain_surf_,"precipitation_rain");
  if (!S->HasField(precip_key_)){    
    S->RequireField(precip_key_, "state")->SetMesh(mesh_surf_)
      ->SetComponent("cell", AmanziMesh::CELL, 1);
    S->RequireFieldEvaluator(precip_key_);
  }

  air_temp_key_ = Keys::getKey(domain_surf_,"air_temperature");
  if (!S->HasField(air_temp_key_)){    
    S->RequireField(air_temp_key_, "state")->SetMesh(mesh_surf_)
      ->SetComponent("cell", AmanziMesh::CELL, 1);
    S->RequireFieldEvaluator(air_temp_key_);
  }

  humidity_key_ = Keys::getKey(domain_surf_,"relative_humidity");
  if (!S->HasField(humidity_key_)){    
    S->RequireField(humidity_key_, "state")->SetMesh(mesh_surf_)
      ->SetComponent("cell", AmanziMesh::CELL, 1);
    S->RequireFieldEvaluator(humidity_key_);
  }

  wind_key_ = Keys::getKey(domain_surf_,"wind");
  if (!S->HasField(wind_key_)){    
    S->RequireField(wind_key_, "state")->SetMesh(mesh_surf_)
      ->SetComponent("cell", AmanziMesh::CELL, 1);
    S->RequireFieldEvaluator(wind_key_);
  }

  co2a_key_ = Keys::getKey(domain_surf_,"co2a");
  if (!S->HasField(co2a_key_)){    
    S->RequireField(co2a_key_, "state")->SetMesh(mesh_surf_)
      ->SetComponent("cell", AmanziMesh::CELL, 1);
    S->RequireFieldEvaluator(co2a_key_);
  }

  longwave_key_ = Keys::getKey(domain_surf_,"longwave_radiation");
  if (!S->HasField(longwave_key_)){    
    S->RequireField(longwave_key_, "state")->SetMesh(mesh_surf_)
      ->SetComponent("cell", AmanziMesh::CELL, 1);
    S->RequireFieldEvaluator(longwave_key_);
  }

  incident_rad_key_ = Keys::getKey(domain_surf_,"incident_radiation");
  if (!S->HasField(incident_rad_key_)){    
    S->RequireField(incident_rad_key_, "state")->SetMesh(mesh_surf_)
      ->SetComponent("cell", AmanziMesh::CELL, 1);
    S->RequireFieldEvaluator(incident_rad_key_);
  }
  
  if (!surface_only_){
    poro_key_ = Keys::readKey(*plist_, "domain", "porosity", "porosity");
    if (!S->HasField(poro_key_)){    
      S->RequireField(poro_key_, "state")->SetMesh(mesh_)
      ->AddComponent("cell", AmanziMesh::CELL, 1);
      S->RequireFieldEvaluator(poro_key_);
    }
    soil_temp_key_ = Keys::getKey("domain","temperature");
    if (!S->HasField(soil_temp_key_)){    
      S->RequireField(soil_temp_key_, "state")->SetMesh(mesh_)
      ->AddComponent("cell", AmanziMesh::CELL, 1);
      S->RequireFieldEvaluator(soil_temp_key_);
    }    
    sat_key_ = Keys::readKey(*plist_, "domain", "saturation", "saturation_liquid");
    if (!S->HasField(sat_key_)){    
      S->RequireField(sat_key_, "state")->SetMesh(mesh_)
      ->AddComponent("cell", AmanziMesh::CELL, 1);
      S->RequireFieldEvaluator(sat_key_);
    }        
    suc_key_ = Keys::readKey(*plist_, "domain", "suction", "suction_head");
    if (!S->HasField(suc_key_)){    
      S->RequireField(suc_key_, "state")->SetMesh(mesh_)
      ->AddComponent("cell", AmanziMesh::CELL, 1);
      S->RequireFieldEvaluator(suc_key_);
    }            
    salinity_key_ = Keys::readKey(*plist_, "domain", "concentration", "total_component_concentration");
    ncomp_salt_ = plist_->get<int>("salt component", 0);
    if (salinity_on_){
      if (!S->HasField(salinity_key_)){
        // Errors::Message msg;
        // msg << "There is no concentration field for salt in State.\n";
        // Exceptions::amanzi_throw(msg);
        S->RequireField(salinity_key_)->SetMesh(mesh_)->SetGhosted(true)
          ->AddComponent("cell", AmanziMesh::CELL, 1);
        S->RequireFieldEvaluator(salinity_key_);        
      }
    }

  }

  S->RequireScalar("atmospheric_pressure");
  
}

  
void FATES_PK::Initialize(const Teuchos::Ptr<State>& S){

  PK_Physical_Default::Initialize(S);

  ncells_owned_ = mesh_surf_->num_entities(AmanziMesh::CELL, AmanziMesh::Parallel_type::OWNED);
  site_.resize(ncells_owned_);

  t_photosynthesis_ = S->time();
  t_site_dym_ = S->time();
  
  time_input_.reference_date = 1900*10000+1*100+1; //Year 1990, month 1, day= 1
  time_input_.days_per_year=365; 
  
  // if (!S->GetField(met_decomp_key_, name_)->initialized()){
  //   S->GetFieldData(met_decomp_key_, name_)->PutScalar(0.0);
  //   S->GetField(met_decomp_key_, name_)->set_initialized();
  // }
  // if (!S->GetField(cel_decomp_key_, name_)->initialized()){
  //   S->GetFieldData(cel_decomp_key_, name_)->PutScalar(0.0);
  //   S->GetField(cel_decomp_key_, name_)->set_initialized();
  // }
  // if (!S->GetField(lig_decomp_key_, name_)->initialized()){
  //   S->GetFieldData(lig_decomp_key_, name_)->PutScalar(0.0);
  //   S->GetField(lig_decomp_key_, name_)->set_initialized();
  // }    


  
  if (surface_only_) {
    ncells_per_col_ = 1;
  }else{
    
    for (unsigned int col=0; col!=ncells_owned_; ++col) {
      
      int f = mesh_surf_->entity_get_parent(AmanziMesh::CELL, col);
      BGC::ColIterator col_iter(*mesh_, f);
      std::size_t ncol_cells = col_iter.size();
      if (ncells_per_col_ < 0) {
        ncells_per_col_ = ncol_cells;
      } else {
        AMANZI_ASSERT(ncol_cells == ncells_per_col_);
      }
      
    }
  }

  int array_size = ncells_per_col_*ncells_owned_;
  t_soil_.resize(array_size);
  vsm_.resize(array_size);
  eff_poro_.resize(array_size);
  poro_.resize(array_size);
  suc_.resize(array_size);
  salinity_.resize(array_size);
  
  clump_ = 1;
  
  for (int i=0; i<ncells_owned_; ++i){
    site_[i].nlevbed = ncells_per_col_;
    site_[i].nlevdecomp = nlevdecomp_;
    site_[i].patchno = patchno_;
    site_[i].temp_veg24_patch = 273;
    site_[i].altmax_lastyear_indx_col = 1;
    site_[i].latdeg = plist_->get<double>("latitude");
    site_[i].londeg = plist_->get<double>("longitude");;
  }

  /* Preliminary initialization of FATES */
  init_ats_fates(&ncells_owned_, site_.data());


  Teuchos::RCP<Epetra_SerialDenseVector> col_poro =
    Teuchos::rcp(new Epetra_SerialDenseVector(ncells_per_col_));
  Teuchos::RCP<Epetra_SerialDenseVector> col_depth =
    Teuchos::rcp(new Epetra_SerialDenseVector(ncells_per_col_));
  Teuchos::RCP<Epetra_SerialDenseVector> col_dz =
    Teuchos::rcp(new Epetra_SerialDenseVector(ncells_per_col_));
    
  std::vector<double> zi(ncells_per_col_+1), z(ncells_per_col_), dz(ncells_per_col_);
  std::vector<double> dzsoil_decomp(ncells_per_col_);
  /* Define soil layers */
  zi[0] = 0;  
  dzsoil_decomp[0] = 1;

  /* Initialize soil layers in FATES*/
  for (int col=0; col<ncells_owned_; col++){
    ColDepthDz_(col, col_depth.ptr(), col_dz.ptr());
    // std::cout<<*col_depth<<"\n";
    // std::cout<<*col_dz<<"\n";
    for (int i=0;i<ncells_per_col_;i++){
      dz[i] = (*col_dz)[i];
      z[i] = (*col_depth)[i];
      zi[i+1] = z[i] + dz[i];
    }
    int s = col+1;    
    init_soil_depths(&clump_, &s,  &(site_[col]), zi.data(), dz.data(), z.data(), dzsoil_decomp.data());
  }

  /* Init cold start of FATES */
  init_coldstart(&clump_);

  Epetra_MultiVector& biomass = *S->GetFieldData(key_, name_)->ViewComponent("cell", false);
  double* data_ptr;
  int data_dim;

  biomass.PutScalar(0.);
  biomass.ExtractView(&data_ptr, &data_dim);
  calculate_biomass(&clump_, data_ptr, data_dim, nlevsclass_);
  S->GetField(key_, name_)->set_initialized();

}

double FATES_PK::get_dt(){

  dt_ = std::min(t_photosynthesis_ + dt_photosynthesis_ - S_inter_->time(), dt_photosynthesis_);
  dt_ = std::min(t_site_dym_ +  dt_site_dym_ - S_inter_->time(), std::min(dt_, dt_site_dym_));

  return dt_;

}


bool FATES_PK::AdvanceStep(double t_old, double t_new, bool reinit){


  double dtime = t_new - t_old;

  S_next_->GetFieldEvaluator(precip_key_)->HasFieldChanged(S_next_.ptr(), name_);
  const Epetra_MultiVector& precip_rain = *S_next_->GetFieldData(precip_key_)->ViewComponent("cell", false);

  S_next_->GetFieldEvaluator(wind_key_)->HasFieldChanged(S_next_.ptr(), name_);
  const Epetra_MultiVector& wind = *S_next_->GetFieldData(wind_key_)->ViewComponent("cell", false);

  S_next_->GetFieldEvaluator(humidity_key_)->HasFieldChanged(S_next_.ptr(), name_);
  const Epetra_MultiVector& humidity = *S_next_->GetFieldData(humidity_key_)->ViewComponent("cell", false);
  
  S_next_->GetFieldEvaluator(air_temp_key_)->HasFieldChanged(S_next_.ptr(), name_);
  const Epetra_MultiVector& air_temp = *S_next_->GetFieldData(air_temp_key_)->ViewComponent("cell", false);

  S_next_->GetFieldEvaluator(co2a_key_)->HasFieldChanged(S_next_.ptr(), name_);
  const Epetra_MultiVector& co2a = *S_next_->GetFieldData(co2a_key_)->ViewComponent("cell", false);

  S_next_->GetFieldEvaluator(longwave_key_)->HasFieldChanged(S_next_.ptr(), name_);
  const Epetra_MultiVector& longwave_rad = *S_next_->GetFieldData(longwave_key_)->ViewComponent("cell", false);

  S_next_->GetFieldEvaluator(incident_rad_key_)->HasFieldChanged(S_next_.ptr(), name_);
  const Epetra_MultiVector& incident_rad = *S_next_->GetFieldData(incident_rad_key_)->ViewComponent("cell", false);

  
  bool run_photo = false;
  bool run_veg_dym = false;

  if (fabs(t_new - (t_photosynthesis_ + dt_photosynthesis_)) < 1e-12*t_new) run_photo = true;
  if (fabs(t_new - (t_site_dym_ + dt_site_dym_)) <  1e-12*t_new) run_veg_dym = true;

  // calculate fractional day length
  double t_days = S_inter_->time() / 86400.;

  int doy = std::floor(std::fmod(t_days, 365))+1;

  if (run_photo){

    for (unsigned int c=0; c<ncells_owned_; ++c){
      
      if (surface_only_){
        t_soil_[c] = air_temp[0][c];
        poro_[c] = 0.5;
        eff_poro_[c] = poro_[c];
        vsm_[c] = 1.*poro_[c];
        suc_[c] = 0.;
        salinity_[c] = 0.;
      }else{
        
        if (S_next_->HasField(soil_temp_key_)){
          S_next_->GetFieldEvaluator(soil_temp_key_)->HasFieldChanged(S_next_.ptr(), name_);
          const Epetra_Vector& temp_vec = *(*S_next_->GetFieldData(soil_temp_key_)->ViewComponent("cell", false))(0);        
          FieldToColumn_(c, temp_vec, t_soil_.data() + c*ncells_per_col_, ncells_per_col_);
        }
                
        if (S_next_->HasField(poro_key_)){
          S_next_->GetFieldEvaluator(poro_key_)->HasFieldChanged(S_next_.ptr(), name_);
          const Epetra_Vector& poro_vec = *(*S_next_->GetFieldData(poro_key_)->ViewComponent("cell", false))(0);        
          FieldToColumn_(c, poro_vec, poro_.data() + c*ncells_per_col_, ncells_per_col_);
        }
        eff_poro_.assign(poro_.begin(), poro_.end());

        if (S_next_->HasField(sat_key_)){
          S_next_->GetFieldEvaluator(sat_key_)->HasFieldChanged(S_next_.ptr(), name_);
          const Epetra_Vector& h2osoil_vec = *(*S_next_->GetFieldData(sat_key_)->ViewComponent("cell", false))(0);
          FieldToColumn_(c, h2osoil_vec, vsm_.data() + c*ncells_per_col_, ncells_per_col_);
          for (int i=0; i<vsm_.size(); ++i) vsm_[i] *= poro_[i]; // convert to water soil content
        }else{
          vsm_.assign(poro_.begin(), poro_.end());  // No saturation in state. Fully saturated assumption;
        }

        
        if (S_next_->HasField(suc_key_)){          
          S_next_->GetFieldEvaluator(suc_key_)->HasFieldChanged(S_next_.ptr(), name_);
          const Epetra_Vector& suc_vec = *(*S_next_->GetFieldData(suc_key_)->ViewComponent("cell", false))(0);        
          FieldToColumn_(c, suc_vec, suc_.data() + c*ncells_per_col_, ncells_per_col_);          
        }else{
          for(int i=0;i<suc_.size();i++) suc_[i] = 0.;  // No suction is defined in State;
        }

        if (S_next_->HasField(salinity_key_)){          
          S_next_->GetFieldEvaluator(salinity_key_)->HasFieldChanged(S_next_.ptr(), name_);
          const Epetra_Vector& salt_vec = *(*S_next_->GetFieldData(salinity_key_)->ViewComponent("cell", false))(ncomp_salt_);        
          FieldToColumn_(c, salt_vec, salinity_.data() + c*ncells_per_col_, ncells_per_col_);          
        }else{
          for(int i=0;i<salinity_.size();i++) salinity_[i] = 0.;  // No suction is defined in State;
        }
        
      }
    }

    // std::cout<<"t_soil_\n";
    // for (auto ent : t_soil_) std::cout<<ent<<" ";
    // std::cout<<"\n";
    std::cout<<"poro: ";
    for (auto ent : poro_) std::cout<<ent<<" ";
    std::cout<<"\n";
    std::cout<<"vsm: ";
    for (auto ent : vsm_) std::cout<<ent<<" ";
    std::cout<<"\n";
    std::cout<<"suc: ";
    for (auto ent : suc_) std::cout<<ent<<" ";
    std::cout<<"\n";
    
    int array_size = t_soil_.size();
    wrap_btran(&clump_, &array_size, t_soil_.data(), poro_.data(), eff_poro_.data(), vsm_.data(), suc_.data(), salinity_.data());    
    PhotoSynthesisInput photo_input;


    int   radnum = 2; //number of radiation bands
    double jday; //julian days (1-365)
    double patm = *S_next_->GetScalarData("atmospheric_pressure");
    QSat qsat;
    

    photo_input.dayl_factor = DayLength(site_[0].latdeg, doy)/(60.0*24); //0-1 factor

    // double es, esdT, qs, qsdT;
    // qsat(air_temp[0][c], patm, &es, &esdT, &qs, &qsdT);
    // photo_input.eair = es*humidity[0][c];
    // double o2a = 209460.0;
    // photo_input.cair = co2a[0][c] * patm * 1.e-6;
    // photo_input.oair = o2a * patm * 1.e-6;
    // photo_input.t_veg = air_temp[0][c];
    // photo_input.tgcm = air_temp[0][c];
    // photo_input.esat_tv = es;
    // photo_input.eair = patm;

    double es, esdT, qs, qsdT;
    qsat(air_temp[0][0], patm, &es, &esdT, &qs, &qsdT);
    
    photo_input.esat_tv = es;               // Saturated vapor pressure in leaves (Pa)
    //photo_input.esat_tv = 2300.;

    photo_input.eair = humidity[0][0] * es;                  // Air water vapor pressure (Pa)
    //photo_input.eair = 2000.;

    double o2a = 209460.0;
    photo_input.oair = o2a * patm * 1.e-6;                    // Oxygen partial pressure
    //photo_input.oair = 21280;
    
    photo_input.cair = co2a[0][0] * patm * 1.e-6;             // CO2 partial pressure
    //photo_input.cair = 5985;
    
    photo_input.rb = std::min(10., 1./wind[0][0]);             // Boundary layer resistance (s/m)
    //photo_input.rb = 3.;

    
    photo_input.t_veg = air_temp[0][0];        // Leaf temperature (K)
    photo_input.tgcm = air_temp[0][0];         // Air temperature (K)
    photo_input.albgrd[0] = 0.15;
    photo_input.albgrd[1] = 0.15;
    photo_input.albgri[0] = 0.1;
    photo_input.albgri[1] = 0.1;
    photo_input.solad[0] = 0.8*incident_rad[0][0];
    photo_input.solad[1] = 0.2*longwave_rad[0][0];
    photo_input.solai[0] = 0.8*incident_rad[0][0];
    photo_input.solai[1] = 0.2*longwave_rad[0][0];    
    jday = doy; //1.0 + (t_new - t_site_dym_)/dt_site_dym_;

    prep_canopyfluxes(&clump_);
    wrap_sunfrac(&clump_, &radnum, photo_input.solad, photo_input.solai);
    wrap_canopy_radiation(&clump_, &jday, &radnum, photo_input.albgrd, photo_input.albgri);           
    wrap_photosynthesis( &clump_, &dt_photosynthesis_, &patm, &array_size, t_soil_.data(), &photo_input);  
    wrap_accumulatefluxes(&clump_, &dt_photosynthesis_);
    
    t_photosynthesis_ = t_new;
    
  }

  
  std::vector<double> temp_veg24_patch(1);
  std::vector<double> prec24_patch(1);
  std::vector<double> rh24_patch(1);
  std::vector<double> wind24_patch(1);
  std::vector<double> days_month(12);
  std::vector<double> h2osoi_vol_col;
  
  days_month[0]=31;
  days_month[1]=28;
  days_month[2]=31;
  days_month[3]=30;
  days_month[4]=31;
  days_month[5]=30;
  days_month[6]=31;
  days_month[7]=31;
  days_month[8]=30;
  days_month[9]=31;
  days_month[10]=30;
  days_month[11]=31;
  
  for(int i=1;i<12;i++){
    days_month[i]=days_month[i]+days_month[i-1];
  }
  int mi=1;
  int dom=1;
  for(int i =11;i>=0;i--){
     if(doy<= days_month[i]) {
       mi=i+1;
       if(i>0){
          dom = doy-days_month[i-1];
       }else{
          dom = doy;
       }
     }
  }  
  time_input_.current_year = 1990+std::ceil(t_days/365.0); //cx: 1990 should a be starting year parameter
  time_input_.current_month = mi;
  time_input_.current_day = dom;
  time_input_.model_day=std::ceil(t_days);  
  time_input_.current_tod= 86400.0*(t_days-std::floor(t_days));
  time_input_.day_of_year= doy;
  time_input_.current_date=time_input_.current_year*10000+ mi*100+dom;

  if (run_veg_dym){

    
    for (int c=0; c<ncells_owned_; c++){
      int s=c+1;

      temp_veg24_patch[0] = air_temp[0][c];
      site_[c].temp_veg24_patch = air_temp[0][c];
      prec24_patch[0] = precip_rain[0][c];
      wind24_patch[0] = wind[0][c];
      rh24_patch[0] = humidity[0][c];
      
      // if (surface_only_){
      //   h2osoi_vol_col.resize(1);
      //   h2osoi_vol_col[0] = 0.5;
      // }else{
      //   h2osoi_vol_col.resize(ncells_per_col_);        
      //   S_next_->GetFieldEvaluator(poro_key_)->HasFieldChanged(S_next_.ptr(), name_);
      //   const Epetra_Vector& poro_vec = *(*S_next_->GetFieldData(poro_key_)->ViewComponent("cell", false))(0);        
      //   FieldToColumn_(c, poro_vec, h2osoi_vol_col.data(), ncells_per_col_);
      // }       
             
      dynamics_driv_per_site(&clump_, &s, &(site_[c]), &time_input_, &dtime,
                             vsm_.data() + c*ncells_per_col_,  // column data for volumetric soil moisture content
                             temp_veg24_patch.data(),
                             prec24_patch.data(),
                             rh24_patch.data(),
                             wind24_patch.data());
    
    }
    t_site_dym_ = t_new;
  }


  return false;
}

  

void FATES_PK::CommitStep(double t_old, double t_new, const Teuchos::RCP<State>& S){

  Epetra_MultiVector& biomass = *S->GetFieldData(key_, name_)->ViewComponent("cell", false);
  double* data_ptr;
  int data_dim;

  biomass.PutScalar(0.);

  biomass.ExtractView(&data_ptr, &data_dim);

  calculate_biomass(&clump_, data_ptr, data_dim, nlevsclass_);
  
}


// helper function for pushing field to column
void FATES_PK::FieldToColumn_(AmanziMesh::Entity_ID col, const Epetra_Vector& vec,
                               double* col_vec, int ncol) {
  ColIterator col_iter(*mesh_, mesh_surf_->entity_get_parent(AmanziMesh::CELL, col), ncol);
  for (std::size_t i=0; i!=col_iter.size(); ++i) {
    col_vec[i] = vec[col_iter[i]];
  }
}

// helper function for collecting column dz and depth
void FATES_PK::ColDepthDz_(AmanziMesh::Entity_ID col,
                            Teuchos::Ptr<Epetra_SerialDenseVector> depth,
                            Teuchos::Ptr<Epetra_SerialDenseVector> dz) {
  AmanziMesh::Entity_ID f_above = mesh_surf_->entity_get_parent(AmanziMesh::CELL, col);
  ColIterator col_iter(*mesh_, f_above, ncells_per_col_);

  AmanziGeometry::Point surf_centroid = mesh_->face_centroid(f_above);
  AmanziGeometry::Point neg_z(3);
  neg_z.set(0.,0.,-1);

  for (std::size_t i=0; i!=col_iter.size(); ++i) {
    // depth centroid
    (*depth)[i] = surf_centroid[2] - mesh_->cell_centroid(col_iter[i])[2];

    // dz
    // -- find face_below
    AmanziMesh::Entity_ID_List faces;
    std::vector<int> dirs;
    mesh_->cell_get_faces_and_dirs(col_iter[i], &faces, &dirs);

    // -- mimics implementation of build_columns() in Mesh
    double mindp = 999.0;
    AmanziMesh::Entity_ID f_below = -1;
    for (std::size_t j=0; j!=faces.size(); ++j) {
      AmanziGeometry::Point normal = mesh_->face_normal(faces[j]);
      if (dirs[j] == -1) normal *= -1;
      normal /= AmanziGeometry::norm(normal);

      double dp = -normal * neg_z;
      if (dp < mindp) {
        mindp = dp;
        f_below = faces[j];
      }
    }

    // -- fill the val
    (*dz)[i] = mesh_->face_centroid(f_above)[2] - mesh_->face_centroid(f_below)[2];
    AMANZI_ASSERT( (*dz)[i] > 0. );
    f_above = f_below;
  }

}



  
}
}
