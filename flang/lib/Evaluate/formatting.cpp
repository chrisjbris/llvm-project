//===-- lib/Evaluate/formatting.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "flang/Evaluate/formatting.h"
#include "flang/Evaluate/call.h"
#include "flang/Evaluate/constant.h"
#include "flang/Evaluate/expression.h"
#include "flang/Evaluate/fold.h"
#include "flang/Evaluate/tools.h"
#include "flang/Parser/characters.h"
#include "flang/Semantics/semantics.h"
#include "flang/Semantics/symbol.h"
#include "flang/Support/Fortran.h"
#include "llvm/Support/raw_ostream.h"

namespace Fortran::evaluate {

// Constant arrays can have non-default lower bounds, but this can't be
// expressed in Fortran syntax directly, only implied through the use of
// named constant (PARAMETER) definitions.  For debugging, setting this flag
// enables a non-standard %LBOUND=[...] argument to the RESHAPE intrinsic
// calls used to dumy constants.  It's off by default so that this syntax
// doesn't show up in module files.
static const bool printLbounds{false};

static void ShapeAsFortran(llvm::raw_ostream &o,
    const ConstantSubscripts &shape, const ConstantSubscripts &lbounds,
    bool hasNonDefaultLowerBound) {
  if (GetRank(shape) > 1 || hasNonDefaultLowerBound) {
    o << ",shape=";
    char ch{'['};
    for (auto dim : shape) {
      o << ch << dim;
      ch = ',';
    }
    o << ']';
    if (hasNonDefaultLowerBound) {
      o << ",%lbound=";
      ch = '[';
      for (auto lb : lbounds) {
        o << ch << lb;
        ch = ',';
      }
      o << ']';
    }
    o << ')';
  }
}

template <typename RESULT, typename VALUE>
llvm::raw_ostream &ConstantBase<RESULT, VALUE>::AsFortran(
    llvm::raw_ostream &o) const {
  bool hasNonDefaultLowerBound{printLbounds && HasNonDefaultLowerBound()};
  if (Rank() > 1 || hasNonDefaultLowerBound) {
    o << "reshape(";
  }
  if (Rank() > 0) {
    o << '[' << GetType().AsFortran() << "::";
  }
  bool first{true};
  for (const auto &value : values_) {
    if (first) {
      first = false;
    } else {
      o << ',';
    }
    if constexpr (Result::category == TypeCategory::Integer) {
      o << value.SignedDecimal() << '_' << Result::kind;
    } else if constexpr (Result::category == TypeCategory::Unsigned) {
      o << value.UnsignedDecimal() << "U_" << Result::kind;
    } else if constexpr (Result::category == TypeCategory::Real ||
        Result::category == TypeCategory::Complex) {
      value.AsFortran(o, Result::kind);
    } else if constexpr (Result::category == TypeCategory::Character) {
      o << Result::kind << '_' << parser::QuoteCharacterLiteral(value, true);
    } else if constexpr (Result::category == TypeCategory::Logical) {
      if (!value.IsCanonical()) {
        o << "transfer(" << value.word().ToInt64() << "_8,.false._"
          << Result::kind << ')';
      } else if (value.IsTrue()) {
        o << ".true." << '_' << Result::kind;
      } else {
        o << ".false." << '_' << Result::kind;
      }
    } else {
      StructureConstructor{result_.derivedTypeSpec(), value}.AsFortran(o);
    }
  }
  if (Rank() > 0) {
    o << ']';
  }
  ShapeAsFortran(o, shape(), lbounds(), hasNonDefaultLowerBound);
  return o;
}

template <int KIND>
llvm::raw_ostream &Constant<Type<TypeCategory::Character, KIND>>::AsFortran(
    llvm::raw_ostream &o) const {
  bool hasNonDefaultLowerBound{printLbounds && HasNonDefaultLowerBound()};
  if (Rank() > 1 || hasNonDefaultLowerBound) {
    o << "reshape(";
  }
  if (Rank() > 0) {
    o << '[' << GetType().AsFortran(std::to_string(length_)) << "::";
  }
  auto total{static_cast<ConstantSubscript>(size())};
  for (ConstantSubscript j{0}; j < total; ++j) {
    Scalar<Result> value{values_.substr(j * length_, length_)};
    if (j > 0) {
      o << ',';
    }
    if (Result::kind != 1) {
      o << Result::kind << '_';
    }
    o << parser::QuoteCharacterLiteral(value);
  }
  if (Rank() > 0) {
    o << ']';
  }
  ShapeAsFortran(o, shape(), lbounds(), hasNonDefaultLowerBound);
  return o;
}

llvm::raw_ostream &EmitVar(llvm::raw_ostream &o, const Symbol &symbol,
    std::optional<parser::CharBlock> name = std::nullopt) {
  const auto &renamings{symbol.owner().context().moduleFileOutputRenamings()};
  if (auto iter{renamings.find(&symbol.GetUltimate())};
      iter != renamings.end()) {
    return o << iter->second.ToString();
  } else if (name) {
    return o << name->ToString();
  } else {
    return o << symbol.name().ToString();
  }
}

llvm::raw_ostream &EmitVar(llvm::raw_ostream &o, const std::string &lit) {
  return o << parser::QuoteCharacterLiteral(lit);
}

llvm::raw_ostream &EmitVar(llvm::raw_ostream &o, const std::u16string &lit) {
  return o << parser::QuoteCharacterLiteral(lit);
}

llvm::raw_ostream &EmitVar(llvm::raw_ostream &o, const std::u32string &lit) {
  return o << parser::QuoteCharacterLiteral(lit);
}

template <typename A>
llvm::raw_ostream &EmitVar(llvm::raw_ostream &o, const A &x) {
  return x.AsFortran(o);
}

template <typename A>
llvm::raw_ostream &EmitVar(llvm::raw_ostream &o, common::Reference<A> x) {
  return EmitVar(o, *x);
}

template <typename A>
llvm::raw_ostream &EmitVar(
    llvm::raw_ostream &o, const A *p, const char *kw = nullptr) {
  if (p) {
    if (kw) {
      o << kw;
    }
    EmitVar(o, *p);
  }
  return o;
}

template <typename A>
llvm::raw_ostream &EmitVar(
    llvm::raw_ostream &o, const std::optional<A> &x, const char *kw = nullptr) {
  if (x) {
    if (kw) {
      o << kw;
    }
    EmitVar(o, *x);
  }
  return o;
}

template <typename A, bool COPY>
llvm::raw_ostream &EmitVar(llvm::raw_ostream &o,
    const common::Indirection<A, COPY> &p, const char *kw = nullptr) {
  if (kw) {
    o << kw;
  }
  EmitVar(o, p.value());
  return o;
}

template <typename A>
llvm::raw_ostream &EmitVar(llvm::raw_ostream &o, const std::shared_ptr<A> &p) {
  CHECK(p);
  return EmitVar(o, *p);
}

template <typename... A>
llvm::raw_ostream &EmitVar(llvm::raw_ostream &o, const std::variant<A...> &u) {
  common::visit([&](const auto &x) { EmitVar(o, x); }, u);
  return o;
}

llvm::raw_ostream &ActualArgument::AssumedType::AsFortran(
    llvm::raw_ostream &o) const {
  return EmitVar(o, *symbol_);
}

llvm::raw_ostream &ActualArgument::AsFortran(llvm::raw_ostream &o) const {
  if (keyword_) {
    o << keyword_->ToString() << '=';
  }
  if (isPercentVal()) {
    o << "%VAL(";
  } else if (isPercentRef()) {
    o << "%REF(";
  }
  common::visit(
      common::visitors{
          [&](const common::CopyableIndirection<Expr<SomeType>> &expr) {
            expr.value().AsFortran(o);
          },
          [&](const AssumedType &assumedType) { assumedType.AsFortran(o); },
          [&](const common::Label &label) { o << '*' << label; },
      },
      u_);
  if (isPercentVal() || isPercentRef()) {
    o << ')';
  }
  return o;
}

llvm::raw_ostream &SpecificIntrinsic::AsFortran(llvm::raw_ostream &o) const {
  return o << name;
}

llvm::raw_ostream &ProcedureRef::AsFortran(llvm::raw_ostream &o) const {
  for (const auto &arg : arguments_) {
    if (arg && arg->isPassedObject()) {
      arg->AsFortran(o) << '%';
      break;
    }
  }
  proc_.AsFortran(o);
  if (!chevrons_.empty()) {
    bool first{true};
    for (const auto &expr : chevrons_) {
      if (first) {
        expr.AsFortran(o << "<<<");
        first = false;
      } else {
        expr.AsFortran(o << ",");
      }
    }
    o << ">>>";
  }
  char separator{'('};
  for (const auto &arg : arguments_) {
    if (arg && !arg->isPassedObject()) {
      arg->AsFortran(o << separator);
      separator = ',';
    }
  }
  if (separator == '(') {
    o << '(';
  }
  return o << ')';
}

// Operator precedence formatting; insert parentheses around operands
// only when necessary.

enum class Precedence { // in increasing order for sane comparisons
  DefinedBinary,
  Or,
  And,
  Equivalence, // .EQV., .NEQV.
  Not, // which binds *less* tightly in Fortran than relations
  Relational,
  Additive, // +, -, and (arbitrarily) //
  Negate, // which binds *less* tightly than *, /, **
  Multiplicative, // *, /
  Power, // **, which is right-associative unlike the other dyadic operators
  DefinedUnary,
  Top,
};

template <typename A> constexpr Precedence ToPrecedence(const A &) {
  return Precedence::Top;
}
template <int KIND>
static Precedence ToPrecedence(const LogicalOperation<KIND> &x) {
  switch (x.logicalOperator) {
    SWITCH_COVERS_ALL_CASES
  case LogicalOperator::And:
    return Precedence::And;
  case LogicalOperator::Or:
    return Precedence::Or;
  case LogicalOperator::Not:
    return Precedence::Not;
  case LogicalOperator::Eqv:
  case LogicalOperator::Neqv:
    return Precedence::Equivalence;
  }
}
template <int KIND> constexpr Precedence ToPrecedence(const Not<KIND> &) {
  return Precedence::Not;
}
template <typename T> constexpr Precedence ToPrecedence(const Relational<T> &) {
  return Precedence::Relational;
}
template <typename T> constexpr Precedence ToPrecedence(const Add<T> &) {
  return Precedence::Additive;
}
template <typename T> constexpr Precedence ToPrecedence(const Subtract<T> &) {
  return Precedence::Additive;
}
template <int KIND> constexpr Precedence ToPrecedence(const Concat<KIND> &) {
  return Precedence::Additive;
}
template <typename T> constexpr Precedence ToPrecedence(const Negate<T> &) {
  return Precedence::Negate;
}
template <typename T> constexpr Precedence ToPrecedence(const Multiply<T> &) {
  return Precedence::Multiplicative;
}
template <typename T> constexpr Precedence ToPrecedence(const Divide<T> &) {
  return Precedence::Multiplicative;
}
template <typename T> constexpr Precedence ToPrecedence(const Power<T> &) {
  return Precedence::Power;
}
template <typename T>
constexpr Precedence ToPrecedence(const RealToIntPower<T> &) {
  return Precedence::Power;
}
template <typename T> static Precedence ToPrecedence(const Constant<T> &x) {
  static constexpr TypeCategory cat{T::category};
  if constexpr (cat == TypeCategory::Integer || cat == TypeCategory::Real) {
    if (auto n{GetScalarConstantValue<T>(x)}) {
      if (n->IsNegative()) {
        return Precedence::Negate;
      }
    }
  }
  return Precedence::Top;
}
template <typename T> static Precedence ToPrecedence(const Expr<T> &expr) {
  return common::visit([](const auto &x) { return ToPrecedence(x); }, expr.u);
}

template <typename T> static bool IsNegatedScalarConstant(const Expr<T> &expr) {
  static constexpr TypeCategory cat{T::category};
  if constexpr (cat == TypeCategory::Integer || cat == TypeCategory::Real) {
    if (auto n{GetScalarConstantValue<T>(expr)}) {
      return n->IsNegative();
    }
  }
  return false;
}

template <TypeCategory CAT>
static bool IsNegatedScalarConstant(const Expr<SomeKind<CAT>> &expr) {
  return common::visit(
      [](const auto &x) { return IsNegatedScalarConstant(x); }, expr.u);
}

struct OperatorSpelling {
  const char *prefix{""}, *infix{","}, *suffix{""};
};

template <typename A> constexpr OperatorSpelling SpellOperator(const A &) {
  return OperatorSpelling{};
}
template <typename A>
constexpr OperatorSpelling SpellOperator(const Negate<A> &) {
  return OperatorSpelling{"-", "", ""};
}
template <typename A>
constexpr OperatorSpelling SpellOperator(const Parentheses<A> &) {
  return OperatorSpelling{"(", "", ")"};
}
template <int KIND>
static OperatorSpelling SpellOperator(const ComplexComponent<KIND> &x) {
  return {x.isImaginaryPart ? "aimag(" : "real(", "", ")"};
}
template <int KIND>
constexpr OperatorSpelling SpellOperator(const Not<KIND> &) {
  return OperatorSpelling{".NOT.", "", ""};
}
template <int KIND>
constexpr OperatorSpelling SpellOperator(const SetLength<KIND> &) {
  return OperatorSpelling{"%SET_LENGTH(", ",", ")"};
}
template <int KIND>
constexpr OperatorSpelling SpellOperator(const ComplexConstructor<KIND> &) {
  return OperatorSpelling{"(", ",", ")"};
}
template <typename A> constexpr OperatorSpelling SpellOperator(const Add<A> &) {
  return OperatorSpelling{"", "+", ""};
}
template <typename A>
constexpr OperatorSpelling SpellOperator(const Subtract<A> &) {
  return OperatorSpelling{"", "-", ""};
}
template <typename A>
constexpr OperatorSpelling SpellOperator(const Multiply<A> &) {
  return OperatorSpelling{"", "*", ""};
}
template <typename A>
constexpr OperatorSpelling SpellOperator(const Divide<A> &) {
  return OperatorSpelling{"", "/", ""};
}
template <typename A>
constexpr OperatorSpelling SpellOperator(const Power<A> &) {
  return OperatorSpelling{"", "**", ""};
}
template <typename A>
constexpr OperatorSpelling SpellOperator(const RealToIntPower<A> &) {
  return OperatorSpelling{"", "**", ""};
}
template <typename A>
static OperatorSpelling SpellOperator(const Extremum<A> &x) {
  return OperatorSpelling{
      x.ordering == Ordering::Less ? "min(" : "max(", ",", ")"};
}
template <int KIND>
constexpr OperatorSpelling SpellOperator(const Concat<KIND> &) {
  return OperatorSpelling{"", "//", ""};
}
template <int KIND>
static OperatorSpelling SpellOperator(const LogicalOperation<KIND> &x) {
  return OperatorSpelling{"", AsFortran(x.logicalOperator), ""};
}
template <typename T>
static OperatorSpelling SpellOperator(const Relational<T> &x) {
  return OperatorSpelling{"", AsFortran(x.opr), ""};
}

template <typename D, typename R, typename... O>
llvm::raw_ostream &Operation<D, R, O...>::AsFortran(
    llvm::raw_ostream &o) const {
  Precedence lhsPrec{ToPrecedence(left())};
  OperatorSpelling spelling{SpellOperator(derived())};
  o << spelling.prefix;
  Precedence thisPrec{ToPrecedence(derived())};
  if constexpr (operands == 1) {
    if (thisPrec != Precedence::Top && lhsPrec < thisPrec) {
      left().AsFortran(o << '(') << ')';
    } else {
      left().AsFortran(o);
    }
  } else {
    if (thisPrec != Precedence::Top &&
        (lhsPrec < thisPrec ||
            (lhsPrec == Precedence::Power && thisPrec == Precedence::Power))) {
      left().AsFortran(o << '(') << ')';
    } else {
      left().AsFortran(o);
    }
    o << spelling.infix;
    Precedence rhsPrec{ToPrecedence(right())};
    if (thisPrec != Precedence::Top && rhsPrec < thisPrec) {
      right().AsFortran(o << '(') << ')';
    } else {
      right().AsFortran(o);
    }
  }
  return o << spelling.suffix;
}

template <typename TO, TypeCategory FROMCAT>
llvm::raw_ostream &Convert<TO, FROMCAT>::AsFortran(llvm::raw_ostream &o) const {
  static_assert(TO::category == TypeCategory::Integer ||
          TO::category == TypeCategory::Real ||
          TO::category == TypeCategory::Complex ||
          TO::category == TypeCategory::Character ||
          TO::category == TypeCategory::Logical ||
          TO::category == TypeCategory::Unsigned,
      "Convert<> to bad category!");
  if constexpr (TO::category == TypeCategory::Character) {
    this->left().AsFortran(o << "achar(iachar(") << ')';
  } else if constexpr (TO::category == TypeCategory::Integer) {
    this->left().AsFortran(o << "int(");
  } else if constexpr (TO::category == TypeCategory::Real) {
    this->left().AsFortran(o << "real(");
  } else if constexpr (TO::category == TypeCategory::Complex) {
    this->left().AsFortran(o << "cmplx(");
  } else if constexpr (TO::category == TypeCategory::Logical) {
    this->left().AsFortran(o << "logical(");
  } else {
    this->left().AsFortran(o << "uint(");
  }
  return o << ",kind=" << TO::kind << ')';
}

llvm::raw_ostream &Relational<SomeType>::AsFortran(llvm::raw_ostream &o) const {
  common::visit([&](const auto &rel) { rel.AsFortran(o); }, u);
  return o;
}

template <typename T>
llvm::raw_ostream &EmitArray(llvm::raw_ostream &o, const Expr<T> &expr) {
  return expr.AsFortran(o);
}

template <typename T>
llvm::raw_ostream &EmitArray(
    llvm::raw_ostream &, const ArrayConstructorValues<T> &);

template <typename T>
llvm::raw_ostream &EmitArray(llvm::raw_ostream &o, const ImpliedDo<T> &implDo) {
  o << '(';
  EmitArray(o, implDo.values());
  o << ',' << ImpliedDoIndex::Result::AsFortran()
    << "::" << implDo.name().ToString() << '=';
  implDo.lower().AsFortran(o) << ',';
  implDo.upper().AsFortran(o) << ',';
  implDo.stride().AsFortran(o) << ')';
  return o;
}

template <typename T>
llvm::raw_ostream &EmitArray(
    llvm::raw_ostream &o, const ArrayConstructorValues<T> &values) {
  const char *sep{""};
  for (const auto &value : values) {
    o << sep;
    common::visit([&](const auto &x) { EmitArray(o, x); }, value.u);
    sep = ",";
  }
  return o;
}

template <typename T>
llvm::raw_ostream &ArrayConstructor<T>::AsFortran(llvm::raw_ostream &o) const {
  o << '[' << GetType().AsFortran() << "::";
  EmitArray(o, *this);
  return o << ']';
}

template <int KIND>
llvm::raw_ostream &
ArrayConstructor<Type<TypeCategory::Character, KIND>>::AsFortran(
    llvm::raw_ostream &o) const {
  o << '[';
  if (const auto *len{LEN()}) {
    o << GetType().AsFortran(len->AsFortran()) << "::";
  }
  EmitArray(o, *this);
  return o << ']';
}

llvm::raw_ostream &ArrayConstructor<SomeDerived>::AsFortran(
    llvm::raw_ostream &o) const {
  o << '[' << GetType().AsFortran() << "::";
  EmitArray(o, *this);
  return o << ']';
}

template <typename RESULT>
std::string ExpressionBase<RESULT>::AsFortran() const {
  std::string buf;
  llvm::raw_string_ostream ss{buf};
  AsFortran(ss);
  return buf;
}

template <typename RESULT>
llvm::raw_ostream &ExpressionBase<RESULT>::AsFortran(
    llvm::raw_ostream &o) const {
  common::visit(common::visitors{
                    [&](const BOZLiteralConstant &x) {
                      o << "z'" << x.Hexadecimal() << "'";
                    },
                    [&](const NullPointer &) { o << "NULL()"; },
                    [&](const common::CopyableIndirection<Substring> &s) {
                      s.value().AsFortran(o);
                    },
                    [&](const ImpliedDoIndex &i) { o << i.name.ToString(); },
                    [&](const auto &x) { x.AsFortran(o); },
                },
      derived().u);
  return o;
}

static std::string DerivedTypeSpecAsFortran(
    const semantics::DerivedTypeSpec &spec) {
  std::string buf;
  llvm::raw_string_ostream ss{buf};
  EmitVar(ss, spec.typeSymbol(), spec.name());
  char ch{'('};
  for (const auto &[name, value] : spec.parameters()) {
    ss << ch << name.ToString() << '=';
    ch = ',';
    if (value.isAssumed()) {
      ss << '*';
    } else if (value.isDeferred()) {
      ss << ':';
    } else {
      value.GetExplicit()->AsFortran(ss);
    }
  }
  if (ch != '(') {
    ss << ')';
  }
  return buf;
}

llvm::raw_ostream &StructureConstructor::AsFortran(llvm::raw_ostream &o) const {
  o << DerivedTypeSpecAsFortran(result_.derivedTypeSpec());
  if (values_.empty()) {
    o << '(';
  } else {
    char ch{'('};
    for (const auto &[symbol, value] : values_) {
      value.value().AsFortran(EmitVar(o << ch, *symbol) << '=');
      ch = ',';
    }
  }
  return o << ')';
}

std::string DynamicType::AsFortran() const {
  if (derived_) {
    CHECK(category_ == TypeCategory::Derived);
    std::string result{DerivedTypeSpecAsFortran(*derived_)};
    if (IsPolymorphic()) {
      result = "CLASS("s + result + ')';
    }
    return result;
  } else if (charLengthParamValue_ || knownLength()) {
    std::string result{"CHARACTER(KIND="s + std::to_string(kind_) + ",LEN="};
    if (knownLength()) {
      result += std::to_string(*knownLength()) + "_8";
    } else if (charLengthParamValue_->isAssumed()) {
      result += '*';
    } else if (charLengthParamValue_->isDeferred()) {
      result += ':';
    } else if (const auto &length{charLengthParamValue_->GetExplicit()}) {
      result += length->AsFortran();
    }
    return result + ')';
  } else if (IsAssumedType()) {
    return "TYPE(*)";
  } else if (IsUnlimitedPolymorphic()) {
    return "CLASS(*)";
  } else if (IsTypelessIntrinsicArgument()) {
    return "(typeless intrinsic function argument)";
  } else {
    return parser::ToUpperCaseLetters(EnumToString(category_)) + '(' +
        std::to_string(kind_) + ')';
  }
}

std::string DynamicType::AsFortran(std::string &&charLenExpr) const {
  if (!charLenExpr.empty() && category_ == TypeCategory::Character) {
    return "CHARACTER(KIND=" + std::to_string(kind_) +
        ",LEN=" + std::move(charLenExpr) + ')';
  } else {
    return AsFortran();
  }
}

std::string SomeDerived::AsFortran() const {
  if (IsUnlimitedPolymorphic()) {
    return "CLASS(*)";
  } else {
    return "TYPE("s + DerivedTypeSpecAsFortran(derivedTypeSpec()) + ')';
  }
}

llvm::raw_ostream &BaseObject::AsFortran(llvm::raw_ostream &o) const {
  return EmitVar(o, u);
}

llvm::raw_ostream &TypeParamInquiry::AsFortran(llvm::raw_ostream &o) const {
  if (base_) {
    base_.value().AsFortran(o) << '%';
  }
  return EmitVar(o, parameter_);
}

llvm::raw_ostream &Component::AsFortran(llvm::raw_ostream &o) const {
  base_.value().AsFortran(o);
  return EmitVar(o << '%', symbol_);
}

llvm::raw_ostream &NamedEntity::AsFortran(llvm::raw_ostream &o) const {
  common::visit(common::visitors{
                    [&](SymbolRef s) { EmitVar(o, s); },
                    [&](const Component &c) { c.AsFortran(o); },
                },
      u_);
  return o;
}

llvm::raw_ostream &Triplet::AsFortran(llvm::raw_ostream &o) const {
  EmitVar(o, lower_) << ':';
  EmitVar(o, upper_);
  EmitVar(o << ':', stride_.value());
  return o;
}

llvm::raw_ostream &Subscript::AsFortran(llvm::raw_ostream &o) const {
  return EmitVar(o, u);
}

llvm::raw_ostream &ArrayRef::AsFortran(llvm::raw_ostream &o) const {
  base_.AsFortran(o);
  char separator{'('};
  for (const Subscript &ss : subscript_) {
    ss.AsFortran(o << separator);
    separator = ',';
  }
  return o << ')';
}

llvm::raw_ostream &CoarrayRef::AsFortran(llvm::raw_ostream &o) const {
  base().AsFortran(o);
  char separator{'['};
  for (const auto &css : cosubscript_) {
    EmitVar(o << separator, css);
    separator = ',';
  }
  if (stat_) {
    EmitVar(o << separator, stat_, "STAT=");
    separator = ',';
  }
  if (team_) {
    EmitVar(o << separator, team_,
        std::holds_alternative<Expr<SomeInteger>>(team_->value().u)
            ? "TEAM_NUMBER="
            : "TEAM=");
  }
  return o << ']';
}

llvm::raw_ostream &DataRef::AsFortran(llvm::raw_ostream &o) const {
  return EmitVar(o, u);
}

llvm::raw_ostream &Substring::AsFortran(llvm::raw_ostream &o) const {
  EmitVar(o, parent_) << '(';
  EmitVar(o, lower_) << ':';
  return EmitVar(o, upper_) << ')';
}

llvm::raw_ostream &ComplexPart::AsFortran(llvm::raw_ostream &o) const {
  return complex_.AsFortran(o) << '%' << EnumToString(part_);
}

llvm::raw_ostream &ProcedureDesignator::AsFortran(llvm::raw_ostream &o) const {
  return EmitVar(o, u);
}

template <typename T>
llvm::raw_ostream &Designator<T>::AsFortran(llvm::raw_ostream &o) const {
  common::visit(common::visitors{
                    [&](SymbolRef symbol) { EmitVar(o, symbol); },
                    [&](const auto &x) { x.AsFortran(o); },
                },
      u);
  return o;
}

llvm::raw_ostream &DescriptorInquiry::AsFortran(llvm::raw_ostream &o) const {
  switch (field_) {
  case Field::LowerBound:
    o << "lbound(";
    break;
  case Field::Extent:
    o << "size(";
    break;
  case Field::Stride:
    o << "%STRIDE(";
    break;
  case Field::Rank:
    o << "int(rank(";
    break;
  case Field::Len:
    o << "int(";
    break;
  }
  base_.AsFortran(o);
  if (field_ == Field::Len) {
    o << "%len";
  } else if (field_ == Field::Rank) {
    o << ")";
  } else {
    if (dimension_ >= 0) {
      o << ",dim=" << (dimension_ + 1);
    }
  }
  return o << ",kind=" << DescriptorInquiry::Result::kind << ")";
}

llvm::raw_ostream &Assignment::AsFortran(llvm::raw_ostream &o) const {
  common::visit(
      common::visitors{
          [&](const Assignment::Intrinsic &) {
            rhs.AsFortran(lhs.AsFortran(o) << '=');
          },
          [&](const ProcedureRef &proc) { proc.AsFortran(o << "CALL "); },
          [&](const BoundsSpec &bounds) {
            lhs.AsFortran(o);
            if (!bounds.empty()) {
              char sep{'('};
              for (const auto &bound : bounds) {
                bound.AsFortran(o << sep) << ':';
                sep = ',';
              }
              o << ')';
            }
            rhs.AsFortran(o << " => ");
          },
          [&](const BoundsRemapping &bounds) {
            lhs.AsFortran(o);
            if (!bounds.empty()) {
              char sep{'('};
              for (const auto &bound : bounds) {
                bound.first.AsFortran(o << sep) << ':';
                bound.second.AsFortran(o);
                sep = ',';
              }
              o << ')';
            }
            rhs.AsFortran(o << " => ");
          },
      },
      u);
  return o;
}

#ifdef _MSC_VER // disable bogus warning about missing definitions
#pragma warning(disable : 4661)
#endif
INSTANTIATE_CONSTANT_TEMPLATES
INSTANTIATE_EXPRESSION_TEMPLATES
INSTANTIATE_VARIABLE_TEMPLATES
} // namespace Fortran::evaluate
