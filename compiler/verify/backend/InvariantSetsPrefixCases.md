For each invariant set assignment different words are added to the prefix index as described by the following table:

| Assignment      | Role   | Inclusion |
| --------------- | ------ | --------- |
| `e ∈ INCL(p)`   | Path   | Inclusive |
| `e ∉ INCL(p)`   | Prefix | Inclusive |
| `e ∈ EXCL(p)`   | Path   | Exclusive |
| `e ∉ EXCL(p)`   | Prefix | Exclusive |
| `e ∈ PATH(p)`   | Prefix | Inclusive |
| `e ∉ PATH(p)`   | Path   | Inclusive |
| `e ∈ SING(p,I)` | Prefix | Exclusive |

For `e ∈ SING(p,I)` we additionally propagate that `e ∈ INCL(p)`. `e ∉ SING(p,I)` conflicts only with an identical after rewriting `e ∈ SING(p,I)`, which is detected separately.

The table below shows all possible combinations of prefix and path matches and explains why each is a conflict. We always have that `p` is a prefix of `q`. When the prefix word is exclusive and the path word inclusive a strict prefix is required. The entry shows the always true set statement that conflicts the assignments.

| Prefix / Path         | `e ∈ INCL(q)`, incl           | `e ∈ EXCL(q)`, excl   | `e ∉ PATH(q)`, incl         |
| --------------------- | ----------------------------- | --------------------- | --------------------------- |
| `e ∉ INCL(p)`, incl   | `INCL(q)⊆INCL(p)`             | `EXCL(q)⊆INCL(p)`     | ✗ both negative — skip      |
| `e ∉ EXCL(p)`, excl   | strict, `INCL(q)⊆EXCL(p)`     | `EXCL(q)⊆EXCL(p)`     | ✗ both negative — skip      |
| `e ∈ PATH(p)`, incl   | `PATH(p)∩INCL(q)=∅`           | `PATH(p)∩EXCL(q)=∅`   | `PATH(p)⊆PATH(q)`           |
| `e ∈ SING(p,I)`, excl | strict, `SING(p,I)∩INCL(q)=∅` | `SING(p,I)∩EXCL(q)=∅` | strict, `SING(p,I)⊆PATH(q)` |
