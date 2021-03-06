// Copyright 2010-2014 Google
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "flatzinc/flatzinc_constraints.h"

#include "base/commandlineflags.h"
#include "constraint_solver/constraint_solveri.h"
#include "flatzinc/model.h"
#include "flatzinc/sat_constraint.h"
#include "util/string_array.h"

DECLARE_bool(cp_trace_search);
DECLARE_bool(cp_trace_propagation);
DECLARE_bool(use_sat);
DECLARE_bool(fz_verbose);

namespace operations_research {
namespace {
class BooleanSumOdd : public Constraint {
 public:
  BooleanSumOdd(Solver* const s, const std::vector<IntVar*>& vars)
      : Constraint(s),
        vars_(vars),
        num_possible_true_vars_(0),
        num_always_true_vars_(0) {}

  virtual ~BooleanSumOdd() {}

  virtual void Post() {
    for (int i = 0; i < vars_.size(); ++i) {
      if (!vars_[i]->Bound()) {
        Demon* const u = MakeConstraintDemon1(
            solver(), this, &BooleanSumOdd::Update, "Update", i);
        vars_[i]->WhenBound(u);
      }
    }
  }

  virtual void InitialPropagate() {
    int num_always_true = 0;
    int num_possible_true = 0;
    int possible_true_index = -1;
    for (int i = 0; i < vars_.size(); ++i) {
      const IntVar* const var = vars_[i];
      if (var->Min() == 1) {
        num_always_true++;
        num_possible_true++;
      } else if (var->Max() == 1) {
        num_possible_true++;
        possible_true_index = i;
      }
    }
    if (num_always_true == num_possible_true && num_possible_true % 2 == 0) {
      solver()->Fail();
    } else if (num_possible_true == num_always_true + 1) {
      DCHECK_NE(-1, possible_true_index);
      if (num_possible_true % 2 == 1) {
        vars_[possible_true_index]->SetMin(1);
      } else {
        vars_[possible_true_index]->SetMax(0);
      }
    }
    num_possible_true_vars_.SetValue(solver(), num_possible_true);
    num_always_true_vars_.SetValue(solver(), num_always_true);
  }

  void Update(int index) {
    DCHECK(vars_[index]->Bound());
    const int64 value = vars_[index]->Min();  // Faster than Value().
    if (value == 0) {
      num_possible_true_vars_.Decr(solver());
    } else {
      DCHECK_EQ(1, value);
      num_always_true_vars_.Incr(solver());
    }
    if (num_always_true_vars_.Value() == num_possible_true_vars_.Value() &&
        num_possible_true_vars_.Value() % 2 == 0) {
      solver()->Fail();
    } else if (num_possible_true_vars_.Value() ==
               num_always_true_vars_.Value() + 1) {
      int possible_true_index = -1;
      for (int i = 0; i < vars_.size(); ++i) {
        if (!vars_[i]->Bound()) {
          possible_true_index = i;
          break;
        }
      }
      if (possible_true_index != -1) {
        if (num_possible_true_vars_.Value() % 2 == 1) {
          vars_[possible_true_index]->SetMin(1);
        } else {
          vars_[possible_true_index]->SetMax(0);
        }
      }
    }
  }

  virtual std::string DebugString() const {
    return StringPrintf("BooleanSumOdd([%s])",
                        JoinDebugStringPtr(vars_, ", ").c_str());
  }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->BeginVisitConstraint(ModelVisitor::kSumEqual, this);
    visitor->VisitIntegerVariableArrayArgument(ModelVisitor::kVarsArgument,
                                               vars_);
    visitor->EndVisitConstraint(ModelVisitor::kSumEqual, this);
  }

 private:
  const std::vector<IntVar*> vars_;
  NumericalRev<int> num_possible_true_vars_;
  NumericalRev<int> num_always_true_vars_;
};

class BoundModulo : public Constraint {
 public:
  BoundModulo(Solver* const s, IntVar* x, IntVar* const m, int64 r)
      : Constraint(s), var_(x), mod_(m), residual_(r) {}

  virtual ~BoundModulo() {}

  virtual void Post() {
    Demon* const d = solver()->MakeConstraintInitialPropagateCallback(this);
    var_->WhenRange(d);
    mod_->WhenBound(d);
  }

  virtual void InitialPropagate() {
    if (mod_->Bound()) {
      const int64 d = std::abs(mod_->Min());
      if (d == 0) {
        solver()->Fail();
      } else {
        const int64 emin = var_->Min();
        const int64 emax = var_->Max();
        const int64 new_min = PosIntDivUp(emin - residual_, d) * d + residual_;
        const int64 new_max =
            PosIntDivDown(emax - residual_, d) * d + residual_;
        var_->SetRange(new_min, new_max);
      }
    }
  }

  virtual std::string DebugString() const {
    return StringPrintf("(%s %% %s == %" GG_LL_FORMAT "d)",
                        var_->DebugString().c_str(),
                        mod_->DebugString().c_str(), residual_);
  }

 private:
  IntVar* const var_;
  IntVar* const mod_;
  const int64 residual_;
};

class VariableParity : public Constraint {
 public:
  VariableParity(Solver* const s, IntVar* const var, bool odd)
      : Constraint(s), var_(var), odd_(odd) {}

  virtual ~VariableParity() {}

  virtual void Post() {
    if (!var_->Bound()) {
      Demon* const u = solver()->MakeConstraintInitialPropagateCallback(this);
      var_->WhenRange(u);
    }
  }

  virtual void InitialPropagate() {
    const int64 vmax = var_->Max();
    const int64 vmin = var_->Min();
    int64 new_vmax = vmax;
    int64 new_vmin = vmin;
    if (odd_) {
      if (vmax % 2 == 0) {
        new_vmax--;
      }
      if (vmin % 2 == 0) {
        new_vmin++;
      }
    } else {
      if (vmax % 2 == 1) {
        new_vmax--;
      }
      if (vmin % 2 == 1) {
        new_vmin++;
      }
    }
    var_->SetRange(new_vmin, new_vmax);
  }

  virtual std::string DebugString() const {
    return StringPrintf("VarParity(%s, %d)", var_->DebugString().c_str(), odd_);
  }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->BeginVisitConstraint("VarParity", this);
    visitor->VisitIntegerExpressionArgument(ModelVisitor::kVariableArgument,
                                            var_);
    visitor->VisitIntegerArgument(ModelVisitor::kValuesArgument, odd_);
    visitor->EndVisitConstraint("VarParity", this);
  }

 private:
  IntVar* const var_;
  const bool odd_;
};

class IsBooleanSumInRange : public Constraint {
 public:
  IsBooleanSumInRange(Solver* const s, const std::vector<IntVar*>& vars,
                      int64 range_min, int64 range_max, IntVar* const target)
      : Constraint(s),
        vars_(vars),
        range_min_(range_min),
        range_max_(range_max),
        target_(target),
        num_possible_true_vars_(0),
        num_always_true_vars_(0) {}

  virtual ~IsBooleanSumInRange() {}

  virtual void Post() {
    for (int i = 0; i < vars_.size(); ++i) {
      if (!vars_[i]->Bound()) {
        Demon* const u = MakeConstraintDemon1(
            solver(), this, &IsBooleanSumInRange::Update, "Update", i);
        vars_[i]->WhenBound(u);
      }
    }
    if (!target_->Bound()) {
      Demon* const u = MakeConstraintDemon0(
          solver(), this, &IsBooleanSumInRange::UpdateTarget, "UpdateTarget");
      target_->WhenBound(u);
    }
  }

  virtual void InitialPropagate() {
    int num_always_true = 0;
    int num_possible_true = 0;
    for (int i = 0; i < vars_.size(); ++i) {
      const IntVar* const var = vars_[i];
      if (var->Min() == 1) {
        num_always_true++;
        num_possible_true++;
      } else if (var->Max() == 1) {
        num_possible_true++;
      }
    }
    num_possible_true_vars_.SetValue(solver(), num_possible_true);
    num_always_true_vars_.SetValue(solver(), num_always_true);
    UpdateTarget();
  }

  void UpdateTarget() {
    if (num_always_true_vars_.Value() > range_max_ ||
        num_possible_true_vars_.Value() < range_min_) {
      inactive_.Switch(solver());
      target_->SetValue(0);
    } else if (num_always_true_vars_.Value() >= range_min_ &&
               num_possible_true_vars_.Value() <= range_max_) {
      inactive_.Switch(solver());
      target_->SetValue(1);
    } else if (target_->Min() == 1) {
      if (num_possible_true_vars_.Value() == range_min_) {
        PushAllUnboundToOne();
      } else if (num_always_true_vars_.Value() == range_max_) {
        PushAllUnboundToZero();
      }
    } else if (target_->Max() == 0) {
      if (num_possible_true_vars_.Value() == range_max_ + 1 &&
          num_always_true_vars_.Value() >= range_min_) {
        PushAllUnboundToOne();
      } else if (num_always_true_vars_.Value() == range_min_ - 1 &&
                 num_possible_true_vars_.Value() <= range_max_) {
        PushAllUnboundToZero();
      }
    }
  }

  void Update(int index) {
    if (!inactive_.Switched()) {
      DCHECK(vars_[index]->Bound());
      const int64 value = vars_[index]->Min();  // Faster than Value().
      if (value == 0) {
        num_possible_true_vars_.Decr(solver());
      } else {
        DCHECK_EQ(1, value);
        num_always_true_vars_.Incr(solver());
      }
      UpdateTarget();
    }
  }

  virtual std::string DebugString() const {
    return StringPrintf(
        "Sum([%s]) in [%" GG_LL_FORMAT "d..%" GG_LL_FORMAT "d] == %s",
        JoinDebugStringPtr(vars_, ", ").c_str(), range_min_, range_max_,
        target_->DebugString().c_str());
  }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->BeginVisitConstraint(ModelVisitor::kSumEqual, this);
    visitor->VisitIntegerVariableArrayArgument(ModelVisitor::kVarsArgument,
                                               vars_);
    visitor->EndVisitConstraint(ModelVisitor::kSumEqual, this);
  }

 private:
  void PushAllUnboundToZero() {
    inactive_.Switch(solver());
    int true_vars = 0;
    for (int i = 0; i < vars_.size(); ++i) {
      if (vars_[i]->Min() == 0) {
        vars_[i]->SetValue(0);
      } else {
        true_vars++;
      }
    }
    target_->SetValue(true_vars >= range_min_ && true_vars <= range_max_);
  }

  void PushAllUnboundToOne() {
    inactive_.Switch(solver());
    int true_vars = 0;
    for (int i = 0; i < vars_.size(); ++i) {
      if (vars_[i]->Max() == 1) {
        vars_[i]->SetValue(1);
        true_vars++;
      }
    }
    target_->SetValue(true_vars >= range_min_ && true_vars <= range_max_);
  }

  const std::vector<IntVar*> vars_;
  const int64 range_min_;
  const int64 range_max_;
  IntVar* const target_;
  NumericalRev<int> num_possible_true_vars_;
  NumericalRev<int> num_always_true_vars_;
  RevSwitch inactive_;
};

class BooleanSumInRange : public Constraint {
 public:
  BooleanSumInRange(Solver* const s, const std::vector<IntVar*>& vars,
                    int64 range_min, int64 range_max)
      : Constraint(s),
        vars_(vars),
        range_min_(range_min),
        range_max_(range_max),
        num_possible_true_vars_(0),
        num_always_true_vars_(0) {}

  virtual ~BooleanSumInRange() {}

  virtual void Post() {
    for (int i = 0; i < vars_.size(); ++i) {
      if (!vars_[i]->Bound()) {
        Demon* const u = MakeConstraintDemon1(
            solver(), this, &BooleanSumInRange::Update, "Update", i);
        vars_[i]->WhenBound(u);
      }
    }
  }

  virtual void InitialPropagate() {
    int num_always_true = 0;
    int num_possible_true = 0;
    int possible_true_index = -1;
    for (int i = 0; i < vars_.size(); ++i) {
      const IntVar* const var = vars_[i];
      if (var->Min() == 1) {
        num_always_true++;
        num_possible_true++;
      } else if (var->Max() == 1) {
        num_possible_true++;
        possible_true_index = i;
      }
    }
    num_possible_true_vars_.SetValue(solver(), num_possible_true);
    num_always_true_vars_.SetValue(solver(), num_always_true);
    Check();
  }

  void Check() {
    if (num_always_true_vars_.Value() > range_max_ ||
        num_possible_true_vars_.Value() < range_min_) {
      solver()->Fail();
    } else if (num_always_true_vars_.Value() >= range_min_ &&
               num_possible_true_vars_.Value() <= range_max_) {
      // Inhibit.
    } else {
      if (num_possible_true_vars_.Value() == range_min_) {
        PushAllUnboundToOne();
      } else if (num_always_true_vars_.Value() == range_max_) {
        PushAllUnboundToZero();
      }
    }
  }

  void Update(int index) {
    DCHECK(vars_[index]->Bound());
    const int64 value = vars_[index]->Min();  // Faster than Value().
    if (value == 0) {
      num_possible_true_vars_.Decr(solver());
    } else {
      DCHECK_EQ(1, value);
      num_always_true_vars_.Incr(solver());
    }
    Check();
  }

  virtual std::string DebugString() const {
    return StringPrintf("Sum([%s]) in [%" GG_LL_FORMAT "d..%" GG_LL_FORMAT "d]",
                        JoinDebugStringPtr(vars_, ", ").c_str(), range_min_,
                        range_max_);
  }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->BeginVisitConstraint(ModelVisitor::kSumEqual, this);
    visitor->VisitIntegerVariableArrayArgument(ModelVisitor::kVarsArgument,
                                               vars_);
    visitor->EndVisitConstraint(ModelVisitor::kSumEqual, this);
  }

 private:
  void PushAllUnboundToZero() {
    for (int i = 0; i < vars_.size(); ++i) {
      if (vars_[i]->Min() == 0) {
        vars_[i]->SetValue(0);
      } else {
      }
    }
  }

  void PushAllUnboundToOne() {
    for (int i = 0; i < vars_.size(); ++i) {
      if (vars_[i]->Max() == 1) {
        vars_[i]->SetValue(1);
      }
    }
  }

  const std::vector<IntVar*> vars_;
  const int64 range_min_;
  const int64 range_max_;
  NumericalRev<int> num_possible_true_vars_;
  NumericalRev<int> num_always_true_vars_;
};

// ----- Variable duration interval var -----

class StartVarDurationVarPerformedIntervalVar : public IntervalVar {
 public:
  StartVarDurationVarPerformedIntervalVar(Solver* const s,
                                          IntVar* const start,
                                          IntVar* const  duration,
                                          const std::string& name);
  virtual ~StartVarDurationVarPerformedIntervalVar() {}

  virtual int64 StartMin() const;
  virtual int64 StartMax() const;
  virtual void SetStartMin(int64 m);
  virtual void SetStartMax(int64 m);
  virtual void SetStartRange(int64 mi, int64 ma);
  virtual int64 OldStartMin() const { return start_->OldMin(); }
  virtual int64 OldStartMax() const { return start_->OldMax(); }
  virtual void WhenStartRange(Demon* const d) { start_->WhenRange(d); }
  virtual void WhenStartBound(Demon* const d) { start_->WhenBound(d); }

  virtual int64 DurationMin() const;
  virtual int64 DurationMax() const;
  virtual void SetDurationMin(int64 m);
  virtual void SetDurationMax(int64 m);
  virtual void SetDurationRange(int64 mi, int64 ma);
  virtual int64 OldDurationMin() const { return duration_->Min(); }
  virtual int64 OldDurationMax() const { return duration_->Max(); }
  virtual void WhenDurationRange(Demon* const d) { duration_->WhenRange(d); }
  virtual void WhenDurationBound(Demon* const d) { duration_->WhenBound(d); }

  virtual int64 EndMin() const;
  virtual int64 EndMax() const;
  virtual void SetEndMin(int64 m);
  virtual void SetEndMax(int64 m);
  virtual void SetEndRange(int64 mi, int64 ma);
  virtual int64 OldEndMin() const {
    return start_->OldMin() + duration_->OldMin();
  }
  virtual int64 OldEndMax() const {
    return start_->OldMax() + duration_->OldMax();
  }
  virtual void WhenEndRange(Demon* const d) {
    start_->WhenRange(d);
    duration_->WhenRange(d);
  }
  virtual void WhenEndBound(Demon* const d) {
    start_->WhenBound(d);
    duration_->WhenBound(d);
  }

  virtual bool MustBePerformed() const;
  virtual bool MayBePerformed() const;
  virtual void SetPerformed(bool val);
  virtual bool WasPerformedBound() const { return true; }
  virtual void WhenPerformedBound(Demon* const d) {}
  virtual std::string DebugString() const;

  virtual IntExpr* StartExpr() { return start_; }
  virtual IntExpr* DurationExpr() { return duration_; }
  virtual IntExpr* EndExpr() {
    return solver()->MakeSum(start_, duration_);
  }
  virtual IntExpr* PerformedExpr() { return solver()->MakeIntConst(1); }
  virtual IntExpr* SafeStartExpr(int64 unperformed_value) {
    return StartExpr();
  }
  virtual IntExpr* SafeDurationExpr(int64 unperformed_value) {
    return DurationExpr();
  }
  virtual IntExpr* SafeEndExpr(int64 unperformed_value) { return EndExpr(); }

  virtual void Accept(ModelVisitor* const visitor) const {
    visitor->VisitIntervalVariable(this, "", 0, nullptr);
  }

 private:
  IntVar* const start_;
  IntVar* const duration_;
};

// TODO(user): Take care of overflows.
StartVarDurationVarPerformedIntervalVar::
StartVarDurationVarPerformedIntervalVar(Solver* const s,
                                        IntVar* const var,
                                        IntVar* const duration,
                                        const std::string& name)
    : IntervalVar(s, name), start_(var), duration_(duration) {}

int64 StartVarDurationVarPerformedIntervalVar::StartMin() const {
  return start_->Min();
}

int64 StartVarDurationVarPerformedIntervalVar::StartMax() const {
  return start_->Max();
}

void StartVarDurationVarPerformedIntervalVar::SetStartMin(int64 m) {
  start_->SetMin(m);
}

void StartVarDurationVarPerformedIntervalVar::SetStartMax(int64 m) {
  start_->SetMax(m);
}

void StartVarDurationVarPerformedIntervalVar::SetStartRange(int64 mi, int64 ma) {
  start_->SetRange(mi, ma);
}

int64 StartVarDurationVarPerformedIntervalVar::DurationMin() const {
  return duration_->Min();
}

int64 StartVarDurationVarPerformedIntervalVar::DurationMax() const {
  return duration_->Max();
}

void StartVarDurationVarPerformedIntervalVar::SetDurationMin(int64 m) {
  duration_->SetMin(m);
}

void StartVarDurationVarPerformedIntervalVar::SetDurationMax(int64 m) {
  duration_->SetMax(m);
}

void StartVarDurationVarPerformedIntervalVar::SetDurationRange(
    int64 mi, int64 ma) {
  duration_->SetRange(mi, ma);
}

int64 StartVarDurationVarPerformedIntervalVar::EndMin() const {
  return start_->Min() + duration_->Min();
}

int64 StartVarDurationVarPerformedIntervalVar::EndMax() const {
  return start_->Max() + duration_->Max();
}

void StartVarDurationVarPerformedIntervalVar::SetEndMin(int64 m) {
  start_->SetMin(m - duration_->Max());
  duration_->SetMin(m - start_->Max());
}

void StartVarDurationVarPerformedIntervalVar::SetEndMax(int64 m) {
  start_->SetMax(m - duration_->Min());
  duration_->SetMax(m - start_->Min());
}

void StartVarDurationVarPerformedIntervalVar::SetEndRange(int64 mi, int64 ma) {
  start_->SetRange(mi - duration_->Max(), ma - duration_->Min());
  duration_->SetRange(mi - start_->Max(), ma - start_->Min());
}

bool StartVarDurationVarPerformedIntervalVar::MustBePerformed() const {
  return true;
}

bool StartVarDurationVarPerformedIntervalVar::MayBePerformed() const {
  return true;
}

void StartVarDurationVarPerformedIntervalVar::SetPerformed(bool val) {
  if (!val) {
    solver()->Fail();
  }
}

std::string StartVarDurationVarPerformedIntervalVar::DebugString() const {
  std::string out;
  const std::string& var_name = name();
  if (!var_name.empty()) {
    out = var_name + "(start = ";
  } else {
    out = "IntervalVar(start = ";
  }
  StringAppendF(&out, "%s, duration = %s, performed = true)",
                start_->DebugString().c_str(),
                duration_->DebugString().c_str());
  return out;
}
}  // namespace

Constraint* MakeIsBooleanSumInRange(Solver* const solver,
                                    const std::vector<IntVar*>& variables,
                                    int64 range_min, int64 range_max,
                                    IntVar* const target) {
  return solver->RevAlloc(
      new IsBooleanSumInRange(solver, variables, range_min, range_max, target));
}

Constraint* MakeBooleanSumInRange(Solver* const solver,
                                  const std::vector<IntVar*>& variables,
                                  int64 range_min, int64 range_max) {
  return solver->RevAlloc(
      new BooleanSumInRange(solver, variables, range_min, range_max));
}
Constraint* MakeBooleanSumOdd(Solver* const solver,
                              const std::vector<IntVar*>& variables) {
  return solver->RevAlloc(new BooleanSumOdd(solver, variables));
}

Constraint* MakeStrongScalProdEquality(Solver* const solver,
                                       const std::vector<IntVar*>& variables,
                                       const std::vector<int64>& coefficients,
                                       int64 rhs) {
  const bool trace = FLAGS_cp_trace_search;
  const bool propag = FLAGS_cp_trace_propagation;
  FLAGS_cp_trace_search = false;
  FLAGS_cp_trace_propagation = false;
  const int size = variables.size();
  IntTupleSet tuples(size);
  Solver s("build");
  std::vector<IntVar*> copy_vars(size);
  for (int i = 0; i < size; ++i) {
    copy_vars[i] = s.MakeIntVar(variables[i]->Min(), variables[i]->Max());
  }
  s.AddConstraint(s.MakeScalProdEquality(copy_vars, coefficients, rhs));
  s.NewSearch(s.MakePhase(copy_vars, Solver::CHOOSE_FIRST_UNBOUND,
                          Solver::ASSIGN_MIN_VALUE));
  while (s.NextSolution()) {
    std::vector<int64> one_tuple(size);
    for (int i = 0; i < size; ++i) {
      one_tuple[i] = copy_vars[i]->Value();
    }
    tuples.Insert(one_tuple);
  }
  s.EndSearch();
  FLAGS_cp_trace_search = trace;
  FLAGS_cp_trace_propagation = propag;
  return solver->MakeAllowedAssignments(variables, tuples);
}

Constraint* MakeVariableOdd(Solver* const s, IntVar* const var) {
  return s->RevAlloc(new VariableParity(s, var, true));
}

Constraint* MakeVariableEven(Solver* const s, IntVar* const var) {
  return s->RevAlloc(new VariableParity(s, var, false));
}

Constraint* MakeBoundModulo(Solver* const s, IntVar* const var,
                            IntVar* const mod, int64 residual) {
  return s->RevAlloc(new BoundModulo(s, var, mod, residual));
}

void PostBooleanSumInRange(SatPropagator* sat, Solver* solver,
                           const std::vector<IntVar*>& variables,
                           int64 range_min, int64 range_max) {
  const int64 size = variables.size();
  range_min = std::max(0LL, range_min);
  range_max = std::min(size, range_max);
  int true_vars = 0;
  std::vector<IntVar*> alt;
  for (int i = 0; i < size; ++i) {
    if (!variables[i]->Bound()) {
      alt.push_back(variables[i]);
    } else if (variables[i]->Min() == 1) {
      true_vars++;
    }
  }
  const int possible_vars = alt.size();
  range_min -= true_vars;
  range_max -= true_vars;

  if (range_max < 0 || range_min > possible_vars) {
    Constraint* const ct = solver->MakeFalseConstraint();
    FZVLOG << "  - posted " << ct->DebugString() << FZENDL;
    solver->AddConstraint(ct);
  } else if (range_min <= 0 && range_max >= possible_vars) {
    FZVLOG << "  - ignore true constraint" << FZENDL;
  } else if (FLAGS_use_sat && AddSumInRange(sat, alt, range_min, range_max)) {
    FZVLOG << "  - posted to sat" << FZENDL;
  } else if (FLAGS_use_sat && range_min == 0 && range_max == 1 &&
             AddAtMostOne(sat, alt)) {
    FZVLOG << "  - posted to sat" << FZENDL;
  } else if (FLAGS_use_sat && range_min == 0 && range_max == size - 1 &&
             AddAtMostNMinusOne(sat, alt)) {
    FZVLOG << "  - posted to sat" << FZENDL;
  } else if (FLAGS_use_sat && range_min == 1 && range_max == 1 &&
             AddBoolOrArrayEqualTrue(sat, alt) && AddAtMostOne(sat, alt)) {
    FZVLOG << "  - posted to sat" << FZENDL;
  } else if (FLAGS_use_sat && range_min == 1 && range_max == possible_vars &&
             AddBoolOrArrayEqualTrue(sat, alt)) {
    FZVLOG << "  - posted to sat" << FZENDL;
  } else {
    Constraint* const ct =
        MakeBooleanSumInRange(solver, alt, range_min, range_max);
    FZVLOG << "  - posted " << ct->DebugString() << FZENDL;
    solver->AddConstraint(ct);
  }
}

void PostIsBooleanSumInRange(SatPropagator* sat, Solver* solver,
                             const std::vector<IntVar*>& variables,
                             int64 range_min, int64 range_max, IntVar* target) {
  const int64 size = variables.size();
  range_min = std::max(0LL, range_min);
  range_max = std::min(size, range_max);
  int true_vars = 0;
  int possible_vars = 0;
  for (int i = 0; i < size; ++i) {
    if (variables[i]->Max() == 1) {
      possible_vars++;
      if (variables[i]->Min() == 1) {
        true_vars++;
      }
    }
  }
  if (true_vars > range_max || possible_vars < range_min) {
    target->SetValue(0);
    FZVLOG << "  - set target to 0" << FZENDL;
  } else if (true_vars >= range_min && possible_vars <= range_max) {
    target->SetValue(1);
    FZVLOG << "  - set target to 1" << FZENDL;
  } else if (FLAGS_use_sat && range_min == size &&
             AddBoolAndArrayEqVar(sat, variables, target)) {
    FZVLOG << "  - posted to sat" << FZENDL;
  } else if (FLAGS_use_sat && range_max == 0 &&
             AddBoolOrArrayEqVar(sat, variables,
                                 solver->MakeDifference(1, target)->Var())) {
    FZVLOG << "  - posted to sat" << FZENDL;
  } else if (FLAGS_use_sat && range_min == 1 && range_max == size &&
             AddBoolOrArrayEqVar(sat, variables, target)) {
    FZVLOG << "  - posted to sat" << FZENDL;
  } else {
    Constraint* const ct = MakeIsBooleanSumInRange(solver, variables, range_min,
                                                   range_max, target);
    FZVLOG << "  - posted " << ct->DebugString() << FZENDL;
    solver->AddConstraint(ct);
  }
}

void PostIsBooleanSumDifferent(SatPropagator* sat, Solver* solver,
                               const std::vector<IntVar*>& variables,
                               int64 value, IntVar* target) {
  const int64 size = variables.size();
  if (value == 0) {
    PostIsBooleanSumInRange(sat, solver, variables, 1, size, target);
  } else if (value == size) {
    PostIsBooleanSumInRange(sat, solver, variables, 0, size - 1, target);
  } else {
    Constraint* const ct =
        solver->MakeIsDifferentCstCt(solver->MakeSum(variables), value, target);
    FZVLOG << "  - posted " << ct->DebugString() << FZENDL;
    solver->AddConstraint(ct);
  }
}

IntervalVar* MakePerformedIntervalVar(Solver* const solver,
                                      IntVar* const start,
                                      IntVar* const duration,
                                      const std::string& n) {
  CHECK(start != nullptr);
  CHECK(duration != nullptr);
  return solver->RegisterIntervalVar(solver->RevAlloc(
      new StartVarDurationVarPerformedIntervalVar(solver, start, duration, n)));
}

}  // namespace operations_research
