/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/*
  Evaluates the conductivity of surface flow as a function of ponded
  depth and surface slope.

  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#ifndef AMANZI_FLOWRELATIONS_OVERLAND_CONDUCTIVITY_MODEL_
#define AMANZI_FLOWRELATIONS_OVERLAND_CONDUCTIVITY_MODEL_

namespace Amanzi {
namespace Flow {
namespace FlowRelations {

class OverlandConductivityModel {
 public:

  virtual double Conductivity(double depth, double slope, double coef) = 0;
  virtual double DConductivityDDepth(double depth, double slope, double coef) = 0;

  //Add for the subgrid model -- Not pure virtual
  virtual double Conductivity(double depth, double slope, double coef, double d, double frac) {};
  virtual double DConductivityDDepth(double depth, double slope, double coef, double p, double frac) {};
};

} // namespace
} // namespace
} // namespace

#endif
