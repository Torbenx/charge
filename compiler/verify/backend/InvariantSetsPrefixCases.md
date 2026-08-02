The table below shows all possible combinations of prefix and path matches and explains why each is a conflict. We always have that `p` is a prefix of `q`. For the entries marked with **s** its a strict prefix. `N` dones the the narrow word and `⟨p⟩` is the spelling of the path alternating `N` and members.

|  | `e ∈ INCL(q)` / `⟨q⟩` | `e ∈ EXCL(q)` / `⟨q⟩N` | `e ∈ SING(q,J)` / `⟨q⟩J` | `e ∉ PATH(q)` / `⟨q⟩` |
| --- | --- | --- | --- | --- |
| `e ∉ INCL(p)` / `⟨p⟩` | `INCL(q)⊆INCL(p)` | `EXCL(q)⊆INCL(p)` | `SING⊆INCL(p)` | ✗ both negative — skip |
| `e ∉ EXCL(p)` / `⟨p⟩N` | **s**, `INCL(q)⊆EXCL(p)` | `EXCL(q)⊆EXCL(p)` | **s**, `SING⊆EXCL(p)` | ✗ both negative — skip |
| `e ∉ SING(p,I)` / `⟨p⟩I` | impossible | impossible | `SING(q,J)=SING(p,I)` | impossible |
| `e ∈ PATH(p)` / `⟨p⟩` | `PATH(p)∩INCL(q)=∅` | `PATH(p)∩EXCL(q)=∅` | `PATH(p)∩SING(q,J)=∅` | `PATH(p)⊆PATH(q)` |
| `e ∈ SING(p,I)` / `⟨p⟩N` | **s**, `SING(p,I)∩INCL(q)=∅` | `SING(p,I)∩EXCL(q)=∅` | **s**, `SING(p,I)∩SING(q,K)=∅` | **s**, `SING(p,I)⊆PATH(q)` |