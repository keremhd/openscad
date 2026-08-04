# `Chain::verts` → `Chain::stations`: the audit's patches, then the type change

Two commits on `kerem-fillet`, taken in that order so that the geometry-neutral
patch application can be verified without the rename in the way.

* Step 1 — `91939a920` "Apply the Chain::verts audit: a count where a poisoned
  index was, and a ring's closing edge"
* Step 2 — the rename plus R1/R2/R3 (SHA at the foot of this file).

Baseline: `kerem-fillet` at `7f5f35cc4`, working tree clean.

---

## 0. The corpus and the instruments

**Models — 19.** The nine ring-carrying model/environment pairs the audit names
(`m_boss`, `m_twoboss`, `m_hp`, `m_bosscut`, `m_reaches_junction`, `grid100`,
`boss`, `ctrl_boss`, `rib`) plus the thirteen controls
(`ctrl1..5 boss grid rib pocket rhomb slab corner grid100`); `boss`, `grid100`
and `rib` are in both lists, so 19 files. `boss` and `ctrl1` are the same shape
written twice, so 18 distinct meshes. Sources recovered with `git show` from
`audit-chain-verts:work/audit/`, `audit-chain-verts:work/ab/` and
`integ-three:work/ab/`; no worktree was checked out.

The audit's environment variation (`OPENSCAD_FILLET_LOCALGROUP` unset / `=1`)
could not be reproduced: **all fillet env vars were removed this week.** So the
nine pairs are nine models, not eighteen runs.

**Instrument check on a known answer.** `rib` exports to md5
`4af898e719553dd42db5080e9b987be1` under the baseline binary — the exact `cur`
md5 recorded in `controls/2026-08-04-sizegate-13-13-control.md`. The exporter
reproduces a number taken independently three days ago.

**Nondeterminism ruled out.** The whole 19-model set was exported twice end to
end with the baseline binary. All 19 md5s identical between passes. None of
these models is in the 15-of-225 nondeterministic class, so "differs" here would
have meant something.

**Baseline suite:** 1709 assertions / 85 test cases, green.

---

## 1. Step 1 — applying the audit's patches

The patch files survived on `audit-chain-verts` (the worktree is gone):

* `work/patches/0001-Ask-the-ends-map-for-a-count-and-say-what-a-ring-s-l.patch`
* `work/patches/0002-Close-the-ring-when-collecting-filleted-edges.patch`

### What applied, and what needed resolving

| patch | file | result |
|---|---|---|
| 0001 | `FilletBuilder.cc` hunk 1 (remove AUDIT PROBE) | **conflict — resolved, see below** |
| 0001 | `FilletBuilder.cc` hunks 2, 3, 4 | clean (3-way) |
| 0001 | `FilletBuilder_internal.h` | clean |
| 0001 | `work/audit/vol.py` | skipped — `work/` does not exist in this checkout |
| 0002 | `FilletBuilder.cc` | clean, offset -59 lines |
| 0002 | `work/CHAIN-VERTS-AUDIT.md`, `work/audit/m_boss.scad`, `m_twoboss.scad` | skipped — `work/` does not exist in this checkout |

**The one conflict, and why the resolution is mechanical.** Hunk 1 of 0001 does
nothing but delete the audit's own temporary `OPENSCAD_FILLET_AUDIT` probe from
`resampleChains`. That probe was never in the mainline, so there was nothing to
delete. The hunk's *context* also carried `++redivided;` and the
`OPENSCAD_FILLET_RESAMPLE_DEBUG` block, and those have since been removed from
HEAD along with every other fillet env var — confirmed by
`git show HEAD:...FilletBuilder.cc | grep -n 'redivided\|RESAMPLE_DEBUG'`
returning nothing. So the conflict is "patch wants to delete lines that are
already gone", and the resolution is to take our side and delete nothing. No
judgement call: the net change of that hunk to this tree is empty.

**Consequence worth recording.** `OPENSCAD_FILLET_RESAMPLE_DEBUG` is gone, so
the `redivided` count the audit reported beside its measurements cannot be read
back out of a stock binary any more. The "did the mechanism fire" question had
to be answered another way — see §1.2.

### 1.1 What the four changes are

1. **F1** — `chainJunctions`' `ends` becomes `std::map<int,int>` counting arrivals
   instead of `std::map<int,std::vector<int>>` recording `verts[1]` / `verts[n-2]`.
   Those two stations are `-1` on a resampled chain; the values were written and
   never dereferenced. The change removes the poisoned value rather than fixing it.
2. **F4** — `filletedEdges` loses `if (a < 0 || b < 0) continue;`, dead by I3
   (`raw` never holds `-1`), and the twelve-line comment whose first paragraph
   contradicts its second.
3. **F6** — `filletedEdges` now walks a ring's closing edge `run[n-1] → run[0]`.
4. **F2** — the `Chain` comment stops claiming both ends are always mesh
   vertices, says what a ring's last station is, and warns that
   `verts.size() == rawRun().size()` is unenforced.

### 1.2 Did anything actually fire?

F1 and F4 are inert by construction — F1's values were never read, F4's guard
provably cannot fire. F6 is the only one that changes a value anything computes,
so it is the one that needs evidence that the byte-identity below means
"neutral" and not "never ran".

A temporary `F6PROBE` block was added to `filletedEdges`, built (`strings -a`
on the binary found the symbol, twice — presence confirmed in the binary, not
by trusting `make`), measured, and reverted. It counts the ring closing edges
the new arm of the loop inserts:

| model | `filletedEdges` calls | ring closing edges added |
|---|---|---|
| m_boss | 1 | 1 |
| m_twoboss | 1 | 2 |
| m_hp | 1 | 4 |
| m_bosscut | 1 | 0 |
| m_reaches_junction | 1 | 0 |
| grid100 | 1 | **100** |
| boss | 1 | 1 |
| ctrl_boss | 1 | 1 |
| rib | 1 | 0 |

**109 edges that were not in the set before are in it now**, and the geometry
does not move. That is exactly what the audit's structural argument predicts:
`seamVertex` is only ever asked at a junction or an open chain's end, and a ring
shares no vertex with either, so the added edges are correct and unread.

*A broken instrument, caught because the answer was known.* The first run of
this probe used `-o /dev/null` and reported `calls=0` on every model —
`filletedEdges` is unconditionally reached, so zero was impossible. `/dev/null`
makes OpenSCAD skip the render. Re-run against real output files. Seventh broken
instrument this effort; the only reason it was caught is that the expected
answer was known in advance.

### 1.3 Step 1 verification

Predicted before measuring: suite 1709/85 unchanged; all 19 md5s unchanged.

* **Suite: 1709 assertions / 85 test cases, green.** Unmoved.
* **All 19 models byte-identical** to the baseline binary.

Mtime evidence: last source edit `FilletBuilder.cc` 2026-08-04 10:57:00,
`FilletBuilder_internal.h` 10:56:16; `FilletBuilder.cc.o` 10:57:29;
`OpenSCADExe` → `OpenSCAD.app/Contents/MacOS/OpenSCAD` 10:57:30;
`OpenSCADUnitTests` 10:57:35. Binary md5 changed from the baseline
(`7eb3e263…` → `7e972302…`), so the rebuild is real and not a "Built target"
line over a stale file.

---

## 2. Step 2 — the rename, R1, R2, R3

### R1 — make the station list unnameable from outside

`Chain::verts` is now `Chain::stations` and is **private**. The only two
questions it can answer from outside are given names:

```cpp
int stationCount() const;      // how many stations; station space, not the mesh
int endVert(bool front) const; // asserts !closed; the mesh vertex an open chain ends on
void setStations(std::vector<int> s);  // the three writers only
```

Everything else about the crease already had a name: `rawRun()`, `rawCount()`,
`param()`, `point()`, `inEdge()`, `outEdge()`, `rawMid()`.

**`ChainContact::vert` deleted (audit F3).** R1 cannot be done without it: it is
the only consumer that indexed the station list at an interior `i`, so leaving it
would have forced an interior accessor and given the whole guarantee back. The
field is provably write-only — written at three places, and a grep for `.vert`
across `src/geometry/fillet/` finds only `Junction::vert` on the read side. This
goes beyond the letter of R1/R2/R3 and is called out here rather than buried:
**it is a fourth audit finding taken on the grounds that R1 requires it.**

**Limit of the assert.** This build is `CMAKE_BUILD_TYPE=Release` with
`-DNDEBUG` in `CXX_FLAGS`, so `assert(!closed)` inside `endVert()` compiles to
nothing in the shipping binary. The guarantee that actually holds in every build
is the compile-time one: `stations` is private, so `chain.stations.back()` on a
ring does not compile. The assert only bites in a debug build. Claiming runtime
protection here would be false.

### R2 — the rename, and the pin

`verts` → `stations`, because it is not the vertices of this crease; it is the
mesh vertex under each station *where there is one*. The struct comment now
says so, names the four consumers that read it the other way, and says where to
go instead. I2 (`stationCount() == rawCount()`) is pinned by the R3 test rather
than by prose — the previous prose about the ends was wrong, which is the whole
reason the audit exists.

### R3 — pin the invariants in the suite

New test: **"resampling: a station is not a crease vertex, and the ends still
are"**, 513 assertions. It is the first test in the suite that calls
`resampleChains` at all.

Model: a $fn 96 bore through a round bar (the audit's `m_hp` shape), giving two
convex bore-mouth rings whose creases carry slivers. The bar's own two end rims
are rings too but are regular and never resampled, so they are filtered out by a
constant-z test. Two open chains are made by reopening the same two creases,
because a bore mouth is always closed and I1's open-chain half needs one.

What it pins, in the vocabulary the chain still exposes:

* **the mechanism fired** — `at` is empty on a chain the resampler skipped, so
  all four chains having non-empty `at` is the served/re-divided count: **4 of 4
  re-divided, 0 refused, 0 blind.** A pass that measured nothing fails here.
* **I3** — the crease is unchanged by resampling (`rawRun()` before == after).
* **I2** — `stationCount() == rawCount()`, both parities. Measured: 120 stations
  / 120 raw on each ring at $fn 96 — the same numbers the audit's own probe
  recorded on `m_hp`.
* **I1, open** — `endVert(front) >= 0`, its parameter is exactly 0 or
  `rawCount()-1`, and its position is the mesh position of that vertex.
* **I1, closed** — a ring has exactly one pinned station and it is station 0;
  its *last* station's parameter is strictly past `rawCount()-1`, i.e. it lies
  on the closing segment between two mesh vertices and is not a vertex at all.
  That is `verts.back() == -1` said without naming the private list.
* **interpolated stations exist** (`interior > 0`), and every station's position
  is the point its own parameter names on the crease, so `at`, `pts` and the
  station list are one list.

**Negative control.** The pin was checked against a case whose answer is known
to be wrong: `resampleChains` was temporarily made to drop one station from
every closed chain. The test failed loudly and specifically —
`CHECK( c.stationCount() == c.rawCount() )` with expansion `119 == 120` on both
rings, plus the ring's last-station-parameter check. Reverted. The instrument
can fail.

### 2.1 Every site the compiler surfaced

**58 references to `.verts` in 3 files**, all surfaced by the rename:
44 in `FilletBuilder.cc`, 12 in `FilletBuilder_test.cc`, 2 in
`FilletBuilder_internal.h`. Classified:

| class | count | what was done |
|---|---|---|
| writes (`buildChains`, `resampleChains`, `rawStationChain`) | 12 | `setStations()`, or moved onto `raw` (see below) |
| `.size()` in station space (I2) | 17 | `stationCount()` |
| `.front()`/`.back()` behind a `closed` guard (I1) | 24 | `endVert(front)` |
| interior index — **needed more than substitution** | 3 | below |
| accessor bodies in the header | 2 | renamed member |

`buildChains` now fills `chain.raw` during the walk and calls
`setStations(chain.raw)` once at the end, instead of filling `verts` and copying
it to `raw`. Same list, same order, one fewer name for it; the canonical
re-ordering and the chain sort now read `raw` because that is what they were
always ordering.

### 2.2 The three sites that were not mechanical

**(a) `FilletBuilder_test.cc:735` — a closed chain indexed station by station
into the mesh.** This is F5's crash-in-waiting, named in the audit:

```cpp
CHECK(ch.closed);
...
for (const int v : ch.verts) nearest = std::min(nearest, std::abs(apart.pos[v].y()));
```

Every station of a **ring** used as a mesh index. It has never crashed only
because no test calls `resampleChains` — which is no longer true as of R3, though
this particular chain still comes straight from `buildChains`. Now reads
`ch.rawRun()`, the crease as the mesh has it. A no-op today; not a no-op the
first time anyone resamples before this line.

**(b) `FilletBuilder.cc:2814` — section space read as station space.**

```cpp
const size_t n = sec.size();        // sections[ci]
const size_t endIdx = front ? 0 : n - 1;
const auto it = junctionAt.find(chain.verts[endIdx]);
```

`endIdx` is an index into the **section** list, used to index the **station**
list. Correct today, because at that point in the pass `sections[ci]` came from
`roundSections` → `spineFrames` → `chainNormals`, which is sized
`chain.stationCount()`. But `sections[ci]` is *rebuilt to a different length*
about 190 lines further down (the run-out ramp at `sections[ci] = std::move(rebuilt)`),
so the two lists are only equal in this window. This is the same conflation as
I2, one level down, and it survived because nothing named it. Now
`chain.endVert(front)` — which cannot be wrong, and asserts.

**(c) `FilletBuilder.cc:950, 953` — `chain.verts[i]` into `ChainContact::vert`.**
Audit F3: dead field, write-only. Field and the argument that fed it deleted.
See R1 above.

Nothing else surfaced that was wrong. The other 55 references were ends behind a
`closed` guard, or a size — the two things the audit says the list is legitimately
for.

### 2.3 Step 2 verification

Predicted before measuring: the pre-existing suite unchanged at 1709/85; the new
test adds one test case; all 19 md5s unchanged.

| measurement | predicted | measured |
|---|---|---|
| suite excluding the new test | 1709 assertions / 85 cases | **1709 / 85, green** |
| the new test alone | 1 case | **513 assertions / 1 case, green** |
| full suite | 1709 + N / 86 | **2222 / 86, green** |

**The 1709/85 number is held.** The total moves to 2222/86 and every one of the
513 added assertions is in the new R3 test; nothing pre-existing moved, and no
pin was edited.

**Byte-identity — 19 of 19 identical, baseline vs step 2:**

| model | baseline md5 | after step 2 |
|---|---|---|
| boss | 32022214c4c7f81e2de8c2e9ac25dcb5 | identical |
| corner | c3d3a98c5c08012e02aeab97c2eaec15 | identical |
| ctrl_boss | 630a462ace733cba4ea10e2b15d34775 | identical |
| ctrl1 | 32022214c4c7f81e2de8c2e9ac25dcb5 | identical |
| ctrl2 | f51c531d99e20d7fb0afff419b3ce83e | identical |
| ctrl3 | c2abb1cb75c22f4ef42d868173c6d486 | identical |
| ctrl4 | 2739fd21af4f63aea33527f7e6187232 | identical |
| ctrl5 | ae43dfd5216de40145cf8d292efaba60 | identical |
| grid | c5f36622d4ae1623dc08936dd5ce4adb | identical |
| grid100 | 7244484e6968a12e7e48150658c5fbf5 | identical |
| m_boss | f089c75d17fc086f519c083c484c213b | identical |
| m_bosscut | b4611b9016083776cec89a74aafac7ea | identical |
| m_hp | fc1ffa734b2dfeec19d7d067ac77cf7e | identical |
| m_reaches_junction | db82b890de69ec78ca7f5535d84778df | identical |
| m_twoboss | 614d2724cda88e50d9a9a0412d2294f4 | identical |
| pocket | fc13e94c2ec693d251ac786270880953 | identical |
| rhomb | b723db2f13bf48f07dfe965331705315 | identical |
| rib | 4af898e719553dd42db5080e9b987be1 | identical |
| slab | b0d8059ce90042c00e7dc223a49351ed | identical |

Not one byte moved in either step. **No blocker in this diff by the byte
standard.**

Mtime evidence for step 2: last source edits `FilletBuilder_internal.h`
2026-08-04 11:06:00, `FilletBuilder.cc` 11:12:03, `FilletBuilder_test.cc`
11:09:30; `OpenSCADUnitTests` 11:12:10, `OpenSCAD.app/Contents/MacOS/OpenSCAD`
11:12:17. Both binaries newer than every source file they were built from.
Target built is `OpenSCADExe`, not `OpenSCAD`; existence confirmed by `stat`,
not by the "Built target" line.

**`ctest -R fillet`: 21 tests, 100% passed, 11.81 s.** Necessary, not
sufficient — the previous reviewer established that this shell suite is largely
blind to fillet geometry (CSG scene-graph dumps plus fuzzy image compares on
models the change does not touch). Its pass is recorded and not leaned on.

---

## 3. What was NOT measured

* **No environment variation.** The audit's nine *pairs* are nine *models*: all
  fillet env vars were removed this week, so `LOCALGROUP` unset/`=1` and
  `SIZEGATE=legacy` columns cannot be taken at all any more.
* **No re-divided count from a stock binary.** `OPENSCAD_FILLET_RESAMPLE_DEBUG`
  was removed with the rest. The re-division evidence in this file comes from the
  R3 test (4 of 4, in-process) and from a temporary probe that was reverted, not
  from a shipping binary.
* **No size-gate counts.** No `FILLETGATE` diagnostic exists any more, so the
  served / refused / blind / blind-refusal columns the 13-control document
  carries have no counterpart here. What replaces them for this change is the
  F6 probe (109 ring closing edges, §1.2) and the R3 test's 4-of-4 re-division.
* **Only 19 models.** Not the 225-model corpus, not the 144-run box census, not
  the 81 shapes, no `$fn` or scale sweep, no rotation sweep.
* **No `endVert()` assert was ever observed firing**, and it cannot fire in this
  build: `-DNDEBUG`. No debug build was made.
* **No genus / manifoldness / volume re-derivation.** Byte-identity is strictly
  stronger for these 19 and was achieved, so no mesh property was independently
  recomputed. If any model had differed, that work would have been owed.
* **No measurement of `arrivesStraight`** — another agent holds that, in a
  separate worktree, off the same HEAD.
* **The `work/` half of both patches was skipped**, not applied elsewhere: the
  audit's own notes, `vol.py`, `m_boss.scad` and `m_twoboss.scad` live only on
  `audit-chain-verts`. The two `.scad` files were recovered from that branch and
  used; the rest was not needed.
* **No performance measurement.** `std::map<int,std::vector<int>>` becoming
  `std::map<int,int>` should be cheaper and the field deletion shrinks
  `ChainContact`; neither was timed.
