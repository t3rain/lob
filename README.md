# C++ Limit Order Book: matching engine, reference model and differential fuzzer

A single-instrument limit order book with price-time priority, written in C++17.
The emphasis is on correctness evidence (invariants, a naive reference engine, a differential fuzzer,
mutation testing) and on honest, reproducible performance measurement.

**Supported operations:** limit order, market order, cancel, modify (reduce keeps queue priority,
increase goes to the back of the queue). Every call that matches returns the list of trades.

## Contents
1. [Matching rules](#basic-matching-rules-and-assumptions)
2. [Design and complexity](#design-and-complexity)
3. [Build and run](#build-and-run)
4. [How it was tested](#how-it-was-tested)
5. [Benchmark](#benchmark)
6. [Limitations and future work](#limitations-and-future-work)

## Basic matching rules and assumptions

- Prices in integer ticks (`int64_t`).
- Price priority first, then time priority (FIFO) within a price level.
- A trade executes at the **resting** order's price. The incoming order receives any price improvement.
- A limit order matches while it crosses the opposite side. Any remainder rests at its own limit price.
- A market order matches at any price. An unfilled remainder is **discarded**. The order never rests.
- `modify(id, new_qty)`: `new_qty` is the order's new **remaining** quantity, which replaces the resting quantity
  of the order. It is independent of the quantity the order was first added with.
  `new_qty <= 0` is rejected. Equal or smaller keeps priority. Larger moves the order to the back of its queue.
- An order ID must not belong to a currently resting order. A cancelled or fully filled ID can be reused.
- `qty <= 0` is rejected.
- A market order against an empty opposite side is a no-op. The operation still counts as a success.
- The book is never crossed: after every operation, best bid < best ask.

## Design and complexity

| Component | Structure |
|---|---|
| Bids | `std::map<Price, Level, std::greater<Price>>`|
| Asks | `std::map<Price, Level>`|
| `Level` | `OrderList` (a `std::list<Order>`) as a FIFO queue, plus a cached `total_qty` |
| Order index | `std::unordered_map<OrderId, Locator>`; a `Locator` holds side, price, the list iterator and a pointer to the level (step 2) |
| Allocation | `OrderList` nodes and the index nodes come from a free-list pool allocator, `PoolAlloc` (step 3). The `bids_` / `asks_` map nodes use the default allocator. |

`std::list` is used because erasing one element never invalidates the other iterators,
so the iterators stored in the index stay valid for the life of each order.
The level pointer in the locator stays valid because `std::map` nodes never move.

| Operation | Cost |
|---|---|
| Rest a new limit order | O(log L) to find or create the level (L = number of price levels), O(1) average for the index insert |
| Cancel | O(1) average to locate and erase, plus O(log L) if the level becomes empty |
| Modify (reduce or equal) | O(1) |
| Modify (increase) | cancel plus re-insert at the back of the queue |
| Match | O(k) for k fills, plus O(log L) for each level exhausted |
| Best bid / best ask | O(1) |

## Build and run

```
.\build.ps1                      # debug build of everything
.\build.ps1 -Mode check          # with libstdc++ checked containers (_GLIBCXX_DEBUG)
.\build.ps1 bench -Mode release  # benchmark build (-O2 -DNDEBUG)
.\bench_rel.exe 1 5000000 20     # seed, operations, timed runs
.\fuzz.exe 14 100000 3           # seed, steps, price half-width
.\run_fuzz.ps1 -Seeds 1000 -Steps 5000 -Width 3
```

## How it was tested

| Layer | What it checks |
|---|---|
| Unit tests | [N] hand-derived cases: FIFO, multi-level sweeps, price improvement, partial fills, cancel at front, middle and back of a queue, modify priority, ID reuse, rejected inputs |
| `check_invariants()` | Not crossed; no empty levels; each level's `total_qty` equals the sum of its orders; no non-positive quantities; every resting order is in the index with matching side, price, iterator and level pointer; index size equals the number of resting orders. Called after every operation in the test suites. |
| Reference engine | A deliberately naive book (two unsorted vectors, a sequence number per order, linear scans) that shares no matching logic with the real engine. |
| Differential fuzzer | The same random operation stream goes to both engines. After every step the return values, trades, best bid/ask and depth at every price are compared, and `check_invariants()` runs on the real engine. [1,000 seeds x 5,000 steps at two price widths, plus longer single runs of up to 700,000 steps] |
| Coverage counters | The fuzzer reports: longest queue at one price, max live orders, trades and partial fills, modifies split into reduce / equal / increase / invalid / dead ID, cancels and modifies that hit a partially filled order, empty-book events |
| Mutation tests | The checker was tested by injecting [10] deliberate bugs into the engine, and the fuzzer by injecting [5], each of which must be detected. Detected: [10/10, 5/5]. This includes a priority bug that no invariant can see and only the differential comparison catches. |
| Checked containers | Unit tests and the fuzzer were run with `-D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS`. |

The unit tests and the fuzzer were re-run on the final build (with the level pointer and the pooled allocator) before the
benchmark numbers below were recorded.

**Notes on the process**

- A stale list iterator can stay "valid" by accident when the allocator hands back the same memory,
  so an invariant that only compares iterators can miss it. It was found while testing the checker.
  The pooled allocator reuses freed slots last-in, first-out, which makes this case more likely, so the invariants
  also check the level pointer and the quantities, not only the iterators.
- The priority bug mentioned is the modify increase case where the order quantity grows in place. Since no other invariant changes here, only a comparison to a reference engine can catch this.
- The fuzzer's own generator had a bug (arguments to `uniform_int_distribution` swapped by mistake, which is undefined behaviour),
  which produced prices across the whole 64-bit range and quietly removed all queueing. The coverage counters exposed it.

**Limits of this evidence.** Both engines were written by the same author, so a shared misreading of an exchange rule
would not be detected by the comparison; the hand-derived unit tests are the guard against that.
Full AddressSanitizer / UBSan runs were not available in this environment (WSL could not be enabled).

## Benchmark

### Environment

| Item | Value |
|---|---|
| CPU | Intel Core Ultra 7 255U, 12 cores / 14 threads (hybrid design with mixed core types), base 2.0 GHz (boosts under high load) |
| Caches | L1 1.2 MB, L2 10 MB, L3 12 MB |
| RAM | 16 GB, 5600 MT/s |
| OS | Windows 11 25H2, build 26200.9550 |
| Compiler | g++ (MinGW-W64 x86_64-msvcrt-posix-seh, built by Brecht Sanders, r1) 14.0.1 20240324 (experimental) |
| Flags | `-std=c++17 -O2 -DNDEBUG -Wall -Wextra` |
| Power / load | plugged in, Balanced mode, not pinned to a core |
| Date and commit | [date, `git rev-parse --short HEAD`] |
| Clock | `std::chrono::steady_clock`, smallest tick 100 ns, `now()` costs about 25 ns |

### Workload

Pre-generated before timing, seeded, identical for every build. 5,000,000 operations:
45% limit, 40% cancel, 10% modify, 5% market. The mid price takes a random +/-1 tick step every 50 operations.
Limit prices are 1 to 20 ticks away from the mid (a few cross the spread). Quantities are 1 to 50.
Cancels and modifies pick one of the 100 most recent orders 90% of the time, and any live order otherwise.
About 75% of cancels and modifies hit a still-resting order; the rest target orders that already traded.
The book grows large in this workload (the generator still holds about 250,000 live IDs at the end, before fills),
so it is a cache-pressure test and not a typical book.

### Throughput

One untimed warm-up run, then 20 timed runs of the full workload on a fresh book. No per-operation timing in these runs.

| Step | Seed | Median (M ops/s) | Median (ns/op) | Min | Max | Spread | Checksum |
|---|---|---|---|---|---|---|---|
| Baseline | 4 | 13.22 | 75.63 | 11.92 | 13.70 | 13.5% | `0x5350e91cf56a6367` |
| Final (steps 1 to 3) | 4 | 28.73 | 34.80 | 27.10 | 29.66 | 8.9% | `0x5350e91cf56a6367` |

The checksum is an order-dependent hash of every trade plus the final book state.
It must be identical after every optimisation; a change means behaviour changed.
Run-to-run spread is about 10% for a single configuration, and occasionally much more (one run in a set dropped by 30%),
so single runs are not compared. The optimisation steps below were measured with interleaved or back-to-back pairs.

The baseline and final rows were measured in different sessions, so the overall factor is approximate:
13.22 to 28.73 M ops/s is about 2.2x, which agrees with the product of the per-step ratios measured below
(1.12 x 1.16 x 1.7).

### Optimisations

Each row is one change on top of the previous one. The unit tests, invariants and fuzzer pass before benchmarking.

| Step | What changed | Median (M ops/s) | vs. previous step | Checksums match |
|---|---|---|---|---|
| 1 | `finder_.reserve(1<<20)` | 12.86 → 14.45 | 1.12x (range 1.10x to 1.20x over 6 interleaved pairs) | yes |
| 2 | Pointer to `level` in the locator map | 14.60 → 16.91 (seed 4) | 1.16x | yes |
| 3 | Pooled node allocation (`OrderList`, `finder_`) | 17.10 → 29.08 (seed 3) | 1.7x (3 interleaved pairs: 1.69x to 1.72x) | yes |

#### __Reserve__: reserve space upfront for the finder map (8 MB).
Measured by alternating the two configurations (no reserve vs. 1M buckets) in a single binary, six interleaved
pairs; every pair favoured the reserve (ratio 1.10x to 1.20x, median-of-medians 12.86 to 14.45 M ops/s, about 1.12x).
The result is the same when book construction, including the 8 MB bucket-array allocation, is moved inside the
timed region, so the gain is not just startup work moved out of the timer. It is consistent with removing
rehash cost during table growth; the mechanism was not isolated further. Both seeds' checksums are unchanged.
A first sweep that ran the sizes sequentially did not show the effect, which is why paired comparisons are used
for all later steps.

#### __Stashing level pointer__: store the pointer to the level in the locator map for O(1) instead of O(logL) lookup.
Cancel and modify previously located an order's price level by searching the `bids_` / `asks_` map. Each
`Locator` now also stores a pointer to its `Level`, which stays valid because `std::map` nodes do not move;
an emptied level is erased by price as the last step of the removal. `check_invariants()` was extended to
verify that every locator's level pointer equals the address of the level that actually contains the order.
Measured on two seeds (20 runs each, base and new binaries run back to back, not interleaved): median
throughput rose from 13.50 to 16.02 M ops/s (1.19x) on seed 1 and from 14.60 to 16.91 M ops/s (1.16x) on
seed 4. The slowest new run was at least as fast as the fastest base run on both seeds (by a narrow margin
on seed 1), and run-to-run spread was 7-16%, so the ratios are approximate. Checksums are unchanged on both seeds.
Sampled mean latency fell by about 24 ns for both cancel and modify (seed 1: 91 to 68 and 80 to 56 ns; seed 4:
90 to 66 and 78 to 55 ns), consistent with removing the map search from exactly those two operations. Limit and
market, whose code did not change, moved by 6-9 ns, which is within the noise of a single sampled pass.
Latency means include about 25 ns of clock overhead.

#### __Pooled allocation__: replace per-node heap allocation with a free-list pool for the order queues and the finder map.
Every resting order costs two heap allocations (a `std::list` node and an `unordered_map` node) and every cancel frees
two. All nodes of one type are the same size, so `PoolAlloc<T>` (`inc/pool_alloc.h`) keeps a free list of equal-size
slots carved from 4096-slot chunks: allocate pops a slot, deallocate pushes it back, with no lock and no header.
It is stateless (one static pool per node type) and is used for `OrderList = std::list<Order, PoolAlloc<Order>>` and
the `finder_` index. Requests for more than one object, such as the hash bucket array, still go to `::operator new`.
The `bids_` / `asks_` map nodes are not pooled, because a price level is created far less often than an order.
Measured by alternating the base and pooled binaries three times each on seed 3 (20 runs per binary): median
throughput 16.56 to 28.06, 17.23 to 29.60 and 17.10 to 29.08 M ops/s, ratios 1.69x, 1.72x and 1.70x.
Non-interleaved runs agree: 15.05 to 28.41 M ops/s (1.89x) on seed 1 and 17.08 to 28.73 M ops/s (1.68x) on seed 4.
Checksums are identical on every run of every seed. Sampled mean latency on seed 3 (raw means, including about
25 ns of clock overhead) fell for limit from about 97 to 59 ns, for cancel from 66 to 52 ns, for modify from 54 to 44 ns
and for market from 77 to 61 ns. The limit add gains most because it makes two allocations, and cancels gain because
they free two nodes. The size of the gain is specific to this platform: MinGW's `new` goes to the Windows process
heap, which is a likely reason for the high per-call cost (not profiled), so the gain with glibc `malloc` on Linux
is expected to be smaller (not measured). The pool never returns memory to the OS and is not thread-safe, and later
runs in the same process reuse chunks warmed by earlier ones (the untimed warm-up run absorbs the first-touch cost).

### What did not work

- Compiler flags: adding `-O3` and `-flto` had no measurable effect on median throughput.
- Pinning the benchmark to a single core gave lower throughput (11.77 M ops/s) than unpinned runs, so all benchmarks
  run unpinned in Balanced power mode.

### Latency

A separate pass on a fresh book, timing a sample of about 50,000 operations of each kind.
Because the clock ticks every 100 ns and one `now()` call costs about 25 ns, **p50 and p90 are not resolvable**
(read p50 = 100 as "about one tick or less", and p50 = 0 as "below one tick"). The p99 and p99.9 columns are the
informative ones. Final build, seed 4:

| Operation | p50 | p90 | p99 | p99.9 | Mean (raw) |
|---|---|---|---|---|---|
| Limit | 100 | 100 | 200 | 400 | 59 |
| Market | 100 | 100 | 200 | 400 | 60 |
| Cancel | 0 | 100 | 200 | 300 | 52 |
| Modify | 0 | 100 | 200 | 300 | 44 |

All values in nanoseconds. The raw mean includes about 25 ns of clock overhead. For comparison, the original baseline
raw means on the same seed were 118 (limit), 95 (market), 85 (cancel) and 77 (modify) ns.

### Where the time goes

Sampled mean per operation (clock overhead subtracted) times the number of operations, final build, approximate:
limit adds about 50% of runtime, cancels about 35%, modifies about 7%, market orders about 6%.
Before pooling, limit adds were about 60%. They are still the largest share, but the gap to cancels has narrowed,
which fits the allocation cost being removed from both. The remaining cost is likely cache misses on the hash index
and the level maps (the book is large in this workload); this was not profiled.

## Limitations and future work

- Single thread, single instrument; no IOC / FOK / stop orders and no auction phases.
- Benchmarked on Windows with MinGW GCC only; numbers are not directly comparable to Linux results, and the pooling gain
  in particular depends on how slow the platform's `malloc` is.
- The pool allocator is not thread-safe and never returns memory to the OS.
- Latency percentiles are limited by the 100 ns clock.
- Not done: pooling the `bids_` / `asks_` map nodes, and a flat array of levels indexed by price tick, which would
  replace the O(log L) level lookup on limit adds. The flat array would need the cached level pointers redesigned,
  because they rely on `std::map` nodes not moving.
- Not implemented: market simulator, market-making strategy and markout analysis.