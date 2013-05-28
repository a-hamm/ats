/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/*
  Evaluates the porosity, given a small compressibility of rock.

  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#ifndef AMANZI_FLOWRELATIONS_COMPRESSIBLE_POROSITY_MODEL_HH_
#define AMANZI_FLOWRELATIONS_COMPRESSIBLE_POROSITY_MODEL_HH_

#include "Teuchos_ParameterList.hpp"

namespace Amanzi {
namespace Flow {
namespace FlowRelations {

class CompressiblePorosityModel {
 public:
  explicit
  CompressiblePorosityModel(Teuchos::ParameterList& plist) :
      plist_(plist) {
    InitializeFromPlist_();
  }

  virtual double Porosity(double base_poro, double pres, double patm) {
    return pres > patm ? (pres-patm)*compressibility_ + base_poro : base_poro;
  }

  virtual double DPorosityDPressure(double base_poro, double pres, double patm) {
    return pres > patm ? compressibility_ : 0.;
  }

  virtual double DPorosityDBasePorosity(double base_poro, double pres, double patm) {
    return 1.;
  }

 protected:
  void InitializeFromPlist_() {
    compressibility_ = plist_.get<double>("pore compressibility");
  }

 protected:

  Teuchos::ParameterList plist_;
  double compressibility_;

};

} // namespace
} // namespace
} // namespace

#endif
