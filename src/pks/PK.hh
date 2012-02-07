/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/* -------------------------------------------------------------------------
ATS

License: see $ATS_DIR/COPYRIGHT
Author: Ethan Coon

Nearly purely virtual interface for Process Kernels.  All physical kernels and
MPCs must implement this interface for use within weak and strongly coupled
hierarchies.
------------------------------------------------------------------------- */


#ifndef PKS_PK_HH_
#define PKS_PK_HH_

#include "Teuchos_RCP.hpp"
#include "Teuchos_VerboseObject.hpp"
#include "Teuchos_ParameterList.hpp"

#include "State.hh"
#include "Vector.hh"
#include "TreeVector.hh"

namespace Amanzi {

class PK : public Teuchos::VerboseObject<PK> {

public:
  // Constructor should populate state with independent and dependent variables,
  // and have a signature of:
  // PK(Teuchos::ParameterList& plist, Teuchos::RCP<State>& S, Teuchos::RCP<TreeVector>& soln);

  // virtual component
  // -- Initialize owned (dependent) variables.
  virtual void initialize(Teuchos::RCP<State>& S) = 0;

  // -- transfer operators -- ONLY COPIES POINTERS
  virtual void state_to_solution(Teuchos::RCP<State>& S,
                                 Teuchos::RCP<TreeVector>& soln) = 0;
  virtual void state_to_solution(Teuchos::RCP<State>& S, Teuchos::RCP<TreeVector>& soln,
                                 Teuchos::RCP<TreeVector>& soln_dot) = 0;
  virtual void solution_to_state(Teuchos::RCP<TreeVector>& soln,
                                 Teuchos::RCP<State>& S) = 0;
  virtual void solution_to_state(Teuchos::RCP<TreeVector>& soln,
                                 Teuchos::RCP<TreeVector>& soln_dot,
                                 Teuchos::RCP<State>& S) = 0;

  // EVIL CODE ALERT!!!
  // While these routines themselves do not modify the const input object,
  // they DO copy pointers to the const object's data into the non-const
  // output object's realm.  While this cheats const, I don't have a good way
  // around it. --etc
  void solution_to_state(Teuchos::RCP<const TreeVector>& soln,
                         Teuchos::RCP<const TreeVector>& soln_dot,
                         Teuchos::RCP<State>& S) {
    Teuchos::RCP<TreeVector> nc_soln = Teuchos::rcp_const_cast<TreeVector>(soln);
    Teuchos::RCP<TreeVector> nc_soln_dot = Teuchos::rcp_const_cast<TreeVector>(soln_dot);
    return solution_to_state(nc_soln, nc_soln_dot, S);
  }

  // -- Choose a time step compatible with physics.
  virtual double get_dt() = 0;

  // -- Advance from state S0 to state S1 at time S0.time + dt.
  virtual bool advance(double dt) = 0;

  // -- Commit any secondary (dependent) variables.
  virtual void commit_state(double dt, Teuchos::RCP<State>& S) = 0;

  // -- Calculate any diagnostics prior to doing vis
  virtual void calculate_diagnostics(Teuchos::RCP<State>& S) = 0;

  // Non-purely virtual component for a few base data structures.
  // -- set pointers to State, and point the solution vector to the data in S_next
  virtual void set_states(Teuchos::RCP<const State>& S, Teuchos::RCP<State>& S_next) {
    S_ = S;
    S_next_ = S_next;
  }

  // -- get and set name
  std::string name() { return name_; }
  virtual void set_name(std::string name) { name_ = name; }

protected:
  std::string name_;
  Teuchos::RCP<const State> S_; // note const, PKs cannot write the committed current state
  Teuchos::RCP<State> S_next_; // instead PKs write to the uncommitted next state
  Teuchos::RCP<TreeVector> solution_; // view into S_next containing just the dependent
                                      // variables in a tree-like data structure which
                                      // follows the PK tree
};
} // namespace

#endif
