

|  | `e ∈ INCL(q)` / `⟨q⟩` | `e ∈ EXCL(q)` / `⟨q⟩N` | `e ∈ SING(q,J)` / `⟨q⟩J` | `e ∉ PATH(q)` / `⟨q⟩` |
| --- | --- | --- | --- | --- |
| `e ∉ INCL(p)` / `⟨p⟩` | `p⊑q`: `INCL(q)⊆INCL(p)` | `p⊑q`: `EXCL(q)⊆INCL(p)` | `p⊑q`: `SING⊆INCL(p)` | `p⊑q`: ✗ both negative — skip |
| `e ∉ EXCL(p)` / `⟨p⟩N` | `p⊏q`: `INCL(q)⊆EXCL(p)` | `p⊑q`: `EXCL(q)⊆EXCL(p)` | `p⊏q`: `SING⊆EXCL(p)` | `p⊏q`: ✗ both negative — skip |
| `e ∉ SING(p,I)` / `⟨p⟩I` | impossible | impossible | `p=q`: `SING(q,J)=SING(p,I)` | impossible |
| `e ∈ PATH(p)` / `⟨p⟩` | `p⊑q`: `PATH(p)∩INCL(q)=∅` | `p⊑q`: `PATH(p)∩EXCL(q)=∅` | `p⊑q`: `PATH(p)∩SING(q,J)=∅` | `p⊑q`: `PATH(p)⊆PATH(q)` |
| `e ∈ SING(p,I)` / `⟨p⟩N` | `p⊏q`: `SING(p,I)∩INCL(q)=∅` | `p⊑q`: `SING(p,I)∩EXCL(q)=∅` | `p⊏q`: `SING(p,I)∩SING(q,K)=∅` | `p⊏q`: `SING(p,I)⊆PATH(q)` |