# Adversarial review — `Chain::verts` → `Chain::stations` (91939a920, 937aa4707)

Reviewer working in the main checkout, `kerem-fillet` at `937aa4707`, tree clean before
and after. A second agent holds `arrivesStraight` in a separate worktree; it was not
touched and is not measured here.

**Verdict: BLOCKER (one), plus three concerns. No geometry defect. No fourth instance of
the station-as-vertex fault.**

Every numeric claim in `fillet-feature-design/chain-stations-rename.md` was re-derived
from my own builds and my own exports. All of them hold, to the digit.

---

## 0. What I built and measured on

`CMAKE_BUILD_TYPE=Release`, `CMAKE_CXX_FLAGS_RELEASE = -O3 -DNDEBUG` (from
`build/CMakeCache.txt`). Target `OpenSCADExe`, binary at
`build/OpenSCAD.app/Contents/MacOS/OpenSCAD`; tests `build/OpenSCADUnitTests`.

Binary-vs-source mtimes checked to the second before every measurement, never by the
"Built target" line:

| point | last source edit | `OpenSCADUnitTests` | `OpenSCAD` |
|---|---|---|---|
| HEAD (as received) | 11:12:03 | 11:12:10 | 11:12:17 |
| baseline `7f5f35cc4` (my build) | 12:44:13 | 12:44:16 | 12:44:18 |
| HEAD restored (my build) | 12:48:07 | 12:48:10 | 12:48:12 |

Models: the same 19, recovered by `git show` from `audit-chain-verts:work/audit/` and
`integ-three:work/ab/`. No worktree was checked out; the other agent's worktree was not
touched.

**Instrument check on a known answer, before trusting the exporter.** My first export
loop reported `FAILED` on all 19 — `timeout(1)` does not exist on this machine, so
nothing ran and `md5` found no file. Caught because 19-of-19 failure was impossible.
Re-run without it, `rib` exports to `4af898e719553dd42db5080e9b987be1`, which is the
`cur` md5 recorded in `controls/2026-08-04-sizegate-13-13-control.md` three days ago and
independently of this change. Eighth broken instrument this effort.

---

## 1. Claim 1 — the suite arithmetic. **CONFIRMED, independently.**

Measured by me, not read off the commit message:

| | assertions | cases |
|---|---|---|
| baseline `7f5f35cc4`, my own build | **1709** | **85** |
| HEAD, `exclude:resampling: a station is not*` | **1709** | **85** |
| HEAD, the new test alone | **513** | **1** |
| HEAD, full | **2222** | **86** |

1709 + 513 = 2222; 85 + 1 = 86. The +513/+1 is entirely the new test. The baseline 1709
is my own measurement from a baseline binary I built, not the implementer's number.

**No pin was edited.** Every pre-existing edit in `FilletBuilder_test.cc` is a mechanical
translation — `ch.verts.size() == static_cast<size_t>(fn)` → `ch.stationCount() == fn`,
`mm.pos[ch.verts.front()]` → `mm.pos[ch.endVert(true)]`, `for (const int v : ch.verts)` →
`for (const int v : ch.rawRun())`. No constant, tolerance or expected value moved.

`ctest -R fillet`: **21/21, 100%, 11.97 s.** Recorded, not leaned on — the previous
reviewer established this shell suite is largely blind to fillet geometry.

---

## 2. Claim 2 — byte-identity. **CONFIRMED, re-derived end to end.**

I built the baseline myself (`git checkout 7f5f35cc4 -- src/geometry/fillet/`, rebuild,
then `git checkout HEAD --` and rebuild; tree verified clean at both ends) and exported
all 19 models from both binaries into my own directories.

**19 of 19 byte-identical, baseline vs HEAD.** Every one of the 19 md5s also equals the
value recorded in the implementer's table — same hashes, different machine paths,
different working directory, so the exporter is path-independent and the recorded table
is real.

Determinism: seven models (`grid100`, `m_hp`, `m_boss`, `rib`, `boss`, `ctrl_boss`,
`corner`) re-exported in a second pass at HEAD, all seven identical to pass 1. None of
the 19 is in the 15-of-225 nondeterministic class.

**On the coverage question — did it silently halve?** The implementer states plainly that
the audit's nine *model/environment pairs* are now nine *models*, because every fillet
env var was removed this week. I checked what was lost: the variation was
`OPENSCAD_FILLET_LOCALGROUP` unset vs `=1`, and `grep -rn getenv src/geometry/` now
returns nothing — the seam rule ships unconditionally on. So the half that survives is
the half that is now the only reachable one, and the half that was lost is dead code that
no longer exists. Coverage did halve in count; it did not halve in *relevance*. This is
the right answer and the implementer gave it honestly rather than hiding it.

---

## 3. Claim 3 — F6 is neutral, not inert. **CONFIRMED, with my own probe.**

I did not take the implementer's probe on trust. I added my own counter to
`filletedEdges` (`REVPROBE_F6`, counting only the new `i + 1 == n` arm), rebuilt
`OpenSCADExe`, and confirmed the symbol was in the binary with `strings` before believing
any number. Output to real files, never `-o /dev/null`.

| model | ring closing edges inserted |
|---|---|
| m_boss | 1 |
| m_twoboss | 2 |
| m_hp | 4 |
| m_bosscut | 0 |
| m_reaches_junction | 0 |
| grid100 | **100** |
| boss | 1 |
| ctrl_boss | 1 |
| rib | 0 |
| **total** | **109** |

Reproduces the implementer's table exactly. **Known-answer validation:** `grid100` is a
10 × 10 grid of square bosses, so exactly 100 closed base rings and exactly 100 closing
edges was predictable before running; `rib`'s creases are all open, so 0 was predictable.
Both came out right, which is what licenses the other seven rows.

So the geometry is byte-identical *while* the set `filletedEdges` returns has 109 members
it did not have before. Neutral, not inert — the claim is exactly right.

**Probe reverted.** `git status` clean; `grep -rn "FILLETJUNCPROBE\|OPENSCAD_FILLET_AUDIT\|
RESAMPLE_DEBUG\|redivided" src/` returns nothing outside the new test's local variable;
`strings` on both shipped binaries finds zero probe symbols.

**And the reachability argument holds.** I checked it rather than accepting it.
`filletedEdges`' output reaches the outside world only through
`creaseLeavesUnfilleted(…, v)` inside the `seamVertex` lambda
(`FilletBuilder.cc:2731`), and every call passes either a `Junction::vert` (2765, 3211,
3286) or an open chain's end vertex (3069, 3197). `buildChains` cuts at every vertex of
crease-degree ≠ 2, so a ring's vertices are all degree 2 and can be neither. The added
edges are correct and unread.

I also checked the shape of the new arm for the failure it invites: a closed chain's
`raw` does **not** repeat its first vertex — `buildChains` breaks on
`nx == chain.raw.front()` with the closing edge consumed — so `run[n-1] → run[0]` is a
genuine distinct edge and not a degenerate `EdgeKey{v, v}`. `n > 2`, `n == 1` and `n == 0`
are all handled. The arm is correct.

---

## 4. Claim 4 — the three genuinely-wrong sites, and the hunt for a fourth

### (a) The test that walked a closed chain into `m.pos` — correct and complete.

`FilletBuilder_test.cc:735` now reads `ch.rawRun()`. `CHECK(ch.closed)` sits two lines
above it, so this was the crash-in-waiting F5 named. `rawRun()` is the right question:
the test wants how near the crease comes to `y = 0`, which is a property of the mesh
curve, not of where stations happen to sit.

### (b) The seam-truncation index — correct, and I verified the coupling it removes.

`FilletBuilder.cc:2814`. `endIdx` derives from `sec.size()`, and `sections[ci]` is built
one-per-station by `roundSections` → `spineFrames` → `chainNormals`, whose `n` is
`chain.stationCount()` (`FilletBuilder.cc:604`). So the two agree at line 2814 and the
change is a no-op today — which is why the bytes did not move. They stop agreeing at
`FilletBuilder.cc:3001`, `sections[ci] = std::move(rebuilt)`, where a runout replaces one
section with a fan. The truncation loop runs *before* that; the next loop to ask the same
question (3037) runs *after* it and already asked the chain. The fix is right and the
reasoning in the commit message is right.

### (c) `ChainContact::vert` — correct to delete. See §5.

### The fourth instance: **there is not one, and I can say why structurally.**

After the rename there is no way for any code outside `Chain` to obtain an *interior*
station index at all — `stations` is private and the only accessors are `stationCount()`
and `endVert(bool)`. A fourth instance of "interior station index read as a mesh vertex"
is not merely absent, it no longer compiles. That half of R1 is genuinely airtight, at
compile time, in every build. It is the real win in this change.

That leaves two ways a fourth instance could still exist, and I checked both by hand.

**(i) `endVert(false)` on a closed chain.** I enumerated all 23 call sites (13 in
`FilletBuilder.cc`, 6 in `FilletBuilder_test.cc`, plus 4 in the new test) and their
guards:

| site | guard | verdict |
|---|---|---|
| `FilletBuilder.cc:1130-1131` | 1129 `closed \|\| stationCount() < 2` | safe |
| `2150-2151` | 2149 same | safe |
| `2159` | 2156 same | safe |
| `2215` | 2209 `!keep.empty() && !closed && n >= 2` | safe |
| `2463-2464` | 2462 same as 1129 | safe |
| `2726` | 2723 same | safe |
| `2814` | 2804 `chain.closed \|\| n < 2`, `n = sec.size()` | safe — see concern C3 |
| `3028-3029` | 3027 same as 1129 | safe |
| `3037` | 3032 `!chainUsable \|\| closed`, then `sec.size() < 2` break | safe — see C3 |
| `3180-3181` | 3179 same as 1129 | safe |
| `3195` | 3194 same | safe |
| test `777-778` | `CHECK_FALSE(ch.closed)` above — a *soft* check | safe in practice |
| test `965` | none, but `front=true` only | safe: station 0 is pinned on a ring too |
| test `2551-2552` | 2550 `closed \|\| stationCount() < 2` | safe |
| new test `2694` | `if (!c.closed)` | safe |

No site is unguarded. No fourth instance here.

**(ii) `stationCount()` asked where the question is about the crease.** The header's own
rule is "read `rawCount()` when the question is about the crease". I walked all 16
`stationCount()` sites. Fifteen are genuinely in station space — `keep`, `at`, `pts` and
the section lists all are, and the bodies reach the mesh only through `point()`,
`param()`, `inEdge()`, `outEdge()` and `rawMid()`. One is not clean: see concern C1.

**Conclusion on the 55 dismissals: the dismissals are correct.** I did not find a fourth
instance, and I believe the reason is structural rather than lucky — R1 removed the
category, it did not merely patch the members of it.

---

## 5. The two flagged items

### 5.1 Deleting `ChainContact::vert` — **in scope, correct, no caller's meaning changed.**

The field held an *interior* station index (`chain.verts[i]` for arbitrary `i`). Keeping
it under R1 would have required a public interior accessor, which hands back exactly the
guarantee R1 exists to establish — so the deletion is not smuggled in beside R1, it is a
precondition of R1. The audit already recommended it as F3 and deferred it only because
an audit patch is the wrong vehicle; the rename is the right one.

Verified there is nothing to break: `grep -rn ChainContact src/` finds the struct
(`FilletBuilder_internal.h:413`) and the declaration (441) and nothing else outside
`FilletBuilder.cc`; the field is read nowhere in `src/`, including all three test callers
of `chainContacts` (`FilletBuilder_test.cc:1580, 1704, 1827`), which use `.v`, `.C`,
`.radius`, `.offFace`, `.valid` and never `.vert`. `Junction::vert`, which *is* a real
mesh vertex and *is* read in fifteen places, is untouched and correctly keeps the name.

Write-only field carrying a sentinel, deleted. Right call.

### 5.2 `endVert()`'s `assert(!closed)` — **BLOCKER.**

The finding, stated once: **the compile-time half of R1 is airtight and the ring half is
not, and the header states the ring half as fact.**

`FilletBuilder_internal.h:215`:

```
assert(!closed && "a ring has no end vertex; its last station may be -1");
```

- The configured build type is `Release`; `CMAKE_CXX_FLAGS_RELEASE` is `-O3 -DNDEBUG`.
- Measured, not assumed: `strings` on **both** shipped binaries returns **zero** matches
  for `a ring has no end vertex`. The assert is not merely inert at runtime — it is not
  in the binary at all.
- No debug build of this project exists or is produced by any target here.

So in every build this repository makes, `chain.endVert(/*front=*/false)` on a resampled
ring returns `stations.back()`, which is `-1`, silently, and the next line indexes
`m.pos` at `-1`.

That would be a tolerable, ordinary "the caller must guard" situation — every current
caller does guard, and I checked all 23 — except that the change documents it as
something stronger. `FilletBuilder_internal.h:188-190`:

> *"That is why `endVert()` asserts the chain is open: on a ring there is no end to ask
> about"*

and the commit message:

> *"endVert(front) on an open chain, which asserts the chain is open **so a ring's last
> station cannot be reached at all**."*

and the audit's R1, on which the whole task rests:

> *"Note that `endVert()` asserting `!closed` is what makes F2 impossible to write, which
> is worth more than the comment I have added."*

All three are false as shipped. A ring's last station *can* be reached, by writing four
words, with no diagnostic in any build.

Why this is blocker-class and not a nit: the audit's own verdict was that F2 — a comment
that told the reader a `verts.back()` was safe when it was not — was *"the single most
dangerous artefact this audit found: it is the exact reassurance that would stop the next
reviewer from checking a `verts.back()`."* This change removes that reassurance and
installs a new one in the same place, about the same value, with the same effect on the
same reader. The fault class has been found four times, every time by a reviewer, never
by a test — and the mechanism each time was a reader who had been told not to look.

The geometry is untouched and nothing is wrong today. What is wrong is that the document
that is supposed to stop the fifth instance now argues that the fifth instance is
impossible.

**Two ways out, either acceptable; both are small.**

1. *Make the guarantee real.* Give the pair as a single query that cannot be asked of a
   ring — e.g. `bool openEnds(int& front, int& back) const` returning false when `closed`
   or `stations.size() < 2`. Every call site already computes exactly that guard, so this
   removes eleven duplicated guards rather than adding anything, and it holds in Release.
2. *Make the documentation true.* Keep `endVert(bool)`, delete the claim that the assert
   is what protects the ring, and say instead that the caller's `closed` guard is the only
   protection in a shipped build and must stay. Cheaper, weaker, honest.

What is not acceptable is shipping the current text, because it retires the one habit —
"check every read of a chain's far end" — that has caught this bug all four times.

---

## 6. Claim 6 — the conflict resolution in step 1. **CONFIRMED.**

Read `audit-chain-verts:work/patches/0001-…patch` directly. Its first hunk to
`FilletBuilder.cc` removes exactly one thing: the seventeen-line
`if (getenv("OPENSCAD_FILLET_AUDIT") != nullptr) { … FILLETAUDIT … }` block, which never
existed in mainline. Its context carries `++redivided;`, and I confirmed against
`git show 7f5f35cc4:src/geometry/fillet/FilletBuilder.cc` that mainline has no
`redivided` counter and no such comment — both went with the env-var removal. Taking
"our side" is therefore a genuinely empty net change, and nothing else lived in that hunk.

The `work/*` hunks: `work/audit/vol.py` (a volume-measuring script), `work/CHAIN-VERTS-AUDIT.md`
and two `.scad` models. None is load-bearing for the shipped tree; the two `.scad` files
were recovered independently and I used them myself. Nothing was lost.

Patch 0002 shipped byte-for-byte as written, at offset −59.

---

## 7. Unprompted checks

### 7.1 The new R3 test — real, but the 513 is 94% one assertion repeated.

**It is not trivially passing.** I validated it on two known-answer breakages of my own,
built and run, then reverted:

| breakage | what the test did |
|---|---|
| resampler emits `count` stations for an open chain instead of `count + 1` (the "fixed-count resampling" shape the audit says D19 nearly landed) | **FAILED**, `FilletBuilder_test.cc:2688`, `119 == 120` — the exact signature the implementer reported; plus `:2695`, the open chain's end-parameter pin |
| resampler pins a ring's last station onto a mesh vertex (`p = segments - 1`) | **FAILED** twice, `FilletBuilder_test.cc:2710`, `119.0 > 119.0` |

So both halves of the invariant — I2 (count) and I1 (which ends are pinned) — genuinely
fail when broken, at the right lines. The test also guards against its own inertness:
`CHECK(redivided == all.size())` fails if the resampler never fires.

**But the assertion count overstates it.** The arithmetic: 4 chains (2 rings, 2 synthetic
open) × 120 crease vertices each. 3 fixed + 2 × 10 (open) + 2 × 5 (closed) = 33
substantive assertions, and 4 × 120 = **480 iterations of the single position check** at
`FilletBuilder_test.cc:2723-2726`. 480 of 513 — 94% — are the same assertion on
consecutive stations of the same two curves. Not worthless (it does check every station,
and it is what catches the ring's last station drifting) but "513 assertions" should not
be read as 513 units of coverage. Worth saying in the record.

**Against what R3 asked for.** R3 wanted `pts[i] == m.pos[verts[i]]` wherever
`verts[i] >= 0`, and "a ring's `verts.back()` may be `-1`". Neither is expressible now —
R1 made `stations` private, which is the point. The substitutes are faithful:
`param(k) > floor(param(k))` is exactly the condition under which `resampleChains` writes
`-1` (`FilletBuilder.cc:405`), and checking `point()` against the position the parameter
names is strictly stronger than checking it against `m.pos[stations[i]]`. R1, R2 and R3
as implemented all match what the audit specified; R3 is met by proxy and the proxy is
sound.

One note on construction: the two "open" chains are synthetic — the ring's own run with
`closed = false` — so their first and last vertices are mesh-adjacent, which no real
`buildChains` output would be. The implementer says so in the comment. It is still a
valid exercise of the resampler's open-chain path.

### 7.2 Stale "vert"/"vertex" names — clean.

`grep -n "verts"` over the three files returns only `Junction::vert` (a real mesh vertex,
correctly named), the `"mesh %d verts"` echo string (about the mesh, correct), and the
header's own explanation of why the old name was wrong. Nothing is called `vert` that is
a station. `resampleChains`' local is `stationVerts`, which is accurate — it is the list
of mesh vertices *under* stations.

Two cosmetic shadows the rename introduced, neither harmful: `FilletBuilder.cc:385`
`const int stations = chain.closed ? count : count + 1;` is an `int` named `stations`
inside the function that fills `Chain::stations`, and `FilletBuilder.cc:645`
`const std::vector<StationNormals> stations` is a different `stations` again. Both are
locals in functions that cannot see the private member. Mentioning them so the next
reader is not surprised.

### 7.3 Dead code — none introduced; one piece correctly removed.

`filletedEdges`' `if (a < 0 || b < 0) continue;` was dead by I3 (`raw` never holds `-1`)
and is gone, together with the twelve-line comment whose first paragraph contradicted its
second. Both removals are right, and the new test's `CHECK(all.front().rawRun() == rawBefore0)`
now pins I3 — the fact that made the guard dead — so the guard cannot be reinstated by
someone who doubts it.

`setStations()` is public and has exactly three callers (`buildChains`,
`resampleChains`, `rawStationChain`), as its comment says. It is a write-only door and
does not reopen the read hole.

### 7.4 Comment convention — respected.

`grep` for `D1[0-9]`, `D2[0-9]`, `milestone`, `R1`/`R2`/`R3` over both `.cc` and `.h`
returns nothing. The header cites function names (`checkChainSizes`, `arrivesStraight`,
`filletedEdges`, `chainJunctions`), which is the right currency. No design-doc citations
leaked into the source.

---

## 8. Concerns (not blockers)

**C1 — `FilletBuilder.cc:3602-3603` and `3640-3641`: one variable answering two
questions.** `segments` is computed from `stationCount()` and then used both as the bound
of *station-parameter* space (correct — `chain.keep` lives there, and the interval-overlap
loop at 3644 must) and as the count of *crease edges* reported to the user ("Counted per
edge, since that is the unit the user wrote the model in"; the `%2$d candidate edge(s)`
warning and the taken/offered echo). Those are different questions and only I2 makes them
the same number. The header's own rule — "read `rawCount()` when the question is about the
crease" — is violated here, in the one place the answer is shown to a user. Consequence
today: none; consequence if I2 ever breaks: a miscounted number in a warning. This is the
closest thing to a fourth instance in the file, and it is cosmetic. Splitting the two uses
would honour the rule the same commit wrote down.

**C2 — `endTouched` (1850), `endWindow` (1865) and `endAnchored` (1911) take a `front`
and none refuses a closed chain.** Their whole subject is "an end", `endWindow` starts at
station `n - 1` for `front = false`, and on a ring that is the unpinned station. They are
safe only because all four callers guard `closed` first — the identical situation
`endVert()` is in, without even the (compiled-out) assert. Pre-existing, unchanged by this
diff, and it is the same hazard C5.2 describes; if 5.2 is fixed by making the guard part
of the query, these three should follow.

**C3 — `FilletBuilder.cc:2804` and `3032-3034` still take the chain's station count from
`sections[ci].size()`.** The commit fixed the *index* at 2814 but the *guard* two lines
above still reads `sec.size()` to decide whether the chain has two ends, and at 3034 it
does so after `sections[ci]` has been rebuilt to a different length at 3001. Both are
correct today (a chain with fewer than two stations produces fewer than two sections, and
a runout cannot happen to such a chain), so this is a latent coupling and not a fault —
but it is exactly the coupling the 2814 fix was written to remove, left standing two lines
away from it.

---

## 9. What I did NOT check

* **`arrivesStraight`** — held by another agent in a separate worktree. Not measured, not
  touched, not reasoned about beyond noting the header names it.
* **No debug build.** I did not build with assertions enabled to watch `endVert()` fire.
  I established that it *cannot* fire in the shipped configuration, which is the point of
  §5.2, but I have not confirmed it would fire correctly in a build that had it.
* **Only 19 models.** Not the 225-model corpus, not the 144-run box census, not the 81
  shapes. No `$fn`, scale or rotation sweep. Byte-identity on 19 is strong for those 19
  and says nothing about the other 206.
* **No genus, manifoldness, volume or weld-tolerance work.** Byte-identity is strictly
  stronger and was achieved on every model, so no mesh property was recomputed. Had one
  model differed, that work would have been owed and I would have done it.
* **No performance measurement.** `std::map<int, std::vector<int>>` → `std::map<int, int>`
  and a four-byte-smaller `ChainContact` should both be cheaper; neither was timed.
* **No re-derivation of the audit's own F1 probe** (`neg1=4` on `m_reaches_junction`). I
  took the audit's measurement of the poisoned value on trust, because F1's repair removes
  the container the value sat in and the byte-identity covers the outcome either way.
* **`FilletCompare_test.cc`** was checked only by grep — it contains no `verts`,
  `stationCount` or `endVert` reference at all, so nothing in it could have moved.
* **The `work/` trees** on `audit-chain-verts` and `integ-three` were read with
  `git show` and never checked out; I did not verify anything about their working state.

---

## 10. Bottom line

The engineering is good and the record is honest — the implementer flagged the NDEBUG
problem himself, in his own "what was NOT measured" section, rather than letting a
reviewer find it. Every claim he made is true and I re-derived all of them: 1709/85 →
2222/86 with the +513 isolated to the new test; 19/19 byte-identical from my own baseline
build; 109 ring closing edges from my own probe with `grid100 = 100` predicted in advance;
the conflict resolution genuinely empty. The three wrong sites are correctly and completely
fixed, `ChainContact::vert` is rightly deleted, and there is no fourth instance — R1
retired the category rather than patching its members.

The one thing to fix before this lands is §5.2. Not because anything is broken, but
because the change writes down a guarantee it does not deliver, in the exact place, about
the exact value, and to the exact reader that this fault class has defeated four times.
Deliver the guarantee or withdraw the sentence.
