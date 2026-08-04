The table below shows all possible combinations of prefix and path matches and explains why each is a conflict. We always have that `p` is a prefix of `q`, for the entries marked with **s** its a strict prefix. The entry shows the always true set statement that conflicts the assignments. `⟨p⟩` is the spelling of the path, which is the members of the location and nothing else.

Note that the inclusive, the exclusive and the path word of a location all spell `⟨p⟩`, so every one of the entries below with `p=q` is a real hit of the index. What separates them is the kind of the two words plus the strictness of the hit, which is what `InvariantPrefixes::raisesConflict()` decides on. The strictness is the one of `PrefixIndex::isStrictPrefixOf()`: the path stays longer than the prefix under *every* rewrite, not just under the current one.

|  | `e ∈ INCL(q)` / `⟨q⟩` | `e ∈ EXCL(q)` / `⟨q⟩` | `e ∈ SING(q,J)` / `⟨q⟩J` | `e ∉ PATH(q)` / `⟨q⟩` |
| --- | --- | --- | --- | --- |
| `e ∉ INCL(p)` / `⟨p⟩` | `INCL(q)⊆INCL(p)` | `EXCL(q)⊆INCL(p)` | `SING(q,J)⊆INCL(p)` | ✗ both negative — skip |
| `e ∉ EXCL(p)` / `⟨p⟩` | **s**, `INCL(q)⊆EXCL(p)` | `EXCL(q)⊆EXCL(p)` | **s**, `SING(q,J)⊆EXCL(p)` | ✗ both negative — skip |
| `e ∉ SING(p,I)` / `⟨p⟩I` | impossible | impossible | `SING(p,I)=SING(q,J)`* | impossible |
| `e ∈ PATH(p)` / `⟨p⟩` | `PATH(p)∩INCL(q)=∅` | `PATH(p)∩EXCL(q)=∅` | `PATH(p)∩SING(q,J)=∅` | `PATH(p)⊆PATH(q)` |
| `e ∈ SING(p,I)` / `⟨p⟩` | **s**, `SING(p,I)∩INCL(q)=∅` | `SING(p,I)∩EXCL(q)=∅` | **s**, `SING(p,I)∩SING(q,J)=∅` | **s**, `SING(p,I)⊆PATH(q)` |

\* This only matches when `p=q` and `I=J` so `SING(p,I)=SING(q,J)` must be true, contradicting `e ∉ SING(p,I)` + `e ∈ SING(q,J)`.

The last row is the exclusive word a positive singleton is added as on top of its own word, so its column `e ∈ SING(q,J)` also covers the hit of that containment with itself, where `p=q` and `I=J`. That one is not strict, so it raises nothing.

Every entry marked **s** has an exclusive word on the prefix side, and every entry with an exclusive word on the prefix side is marked **s** unless the path side is exclusive as well.
