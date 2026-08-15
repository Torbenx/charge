#pragma once

#include <verify/ir/Function.h>

namespace verify::ir {

//! Whether the clauses of 'proof' are incompatible with the negation of 'prop'
/*!
The clauses and the negated proposition are read as propositional formulas and handed to a SAT
solver: 'and', 'or' and negation become the connectives they denote, every other proposition is
an opaque boolean variable that only ever equals itself. The proof holds when the resulting
problem has no model at all, because then the clauses already rule out every way for 'prop' to
be false.

The clauses are theorems of the function, so their own proofs are established where they are
stated and only their propositions are read here.
*/
bool provesPropBySat(const Function& function, const SatProof& proof, Bool prop);

}
