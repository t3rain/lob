# C++ Limit Order Book: matching engine, reference model and differential fuzzer

A single-instrument limit order book with price-time priority, written in C++17.
The emphasis is on correctness evidence (invariants, a naive reference engine, a differential fuzzer,
mutation testing) and on honest, reproducible performance measurement.

**Supported operations:** limit order, market order, cancel, modify (reduce keeps queue priority,
increase goes to the back of the queue). Every call that matches returns the list of trades.

## Contents
1. [Matching rules](#matching-rules)
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
- `modify(id, new_qty)`: `new_qty` is the order's new **remaining** quantity, which will replace the resting quantity
  of the order. It does not track or figure with the total quantity with which the order was first added.
  `new_qty <= 0` is rejected. Equal or smaller keeps priority. Larger moves the order to the back of its queue.
- An order ID must not belong to a currently resting order. A cancelled or fully filled ID can be reused.
- `qty <= 0` is rejected. 
- Market orders on an empty opposite book will be a no-op. The operation still counts as a success however.
- The book is never crossed: after every operation, best bid < best ask.

## Design and complexity

| Component | Structure |
|---|---|
| Bids | `std::map<Price, Level, std::greater<Price>>`|
| Asks | `std::map<Price, Level>`|
| `Level` | `std::list<Order>` as a FIFO queue, plus a cached `total_qty` |
| Order index | `std::unordered_map<OrderId, Locator>`; a `Locator` holds side, price and the list iterator |

`std::list` is used because erasing one element never invalidates the other iterators,
so the iterators stored in the index stay valid for the life of each order.

| Operation | Cost |
|---|---|
| Rest a new limit order | O(log L) to find or create the level (L = number of price levels), O(1) average for the index insert |
| Cancel | O(1) average to locate and erase, plus O(log L) if the level becomes empty |
| Modify (reduce or equal) | O(1) average plus the level lookup |
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
| `check_invariants()` | Not crossed; no empty levels; each level's `total_qty` equals the sum of its orders; no non-positive quantities; every resting order is in the index with matching side, price and iterator; index size equals the number of resting orders. Called after every operation in the test suites. |
| Reference engine | A deliberately naive book (two unsorted vectors, a sequence number per order, linear scans) that shares no matching logic with the real engine. |
| Differential fuzzer | The same random operation stream goes to both engines. After every step the return values, trades, best bid/ask and depth at every price are compared, and `check_invariants()` runs on the real engine. [1,000 seeds x 5,000 steps at two price widths, plus longer single runs of up to 700,000 steps] |
| Coverage counters | The fuzzer reports: longest queue at one price, max live orders, trades and partial fills, modifies split into reduce / equal / increase / invalid / dead ID, cancels and modifies that hit a partially filled order, empty-book events |
| Mutation tests | The checker was tested by injecting [10] deliberate bugs into the engine, and the fuzzer by injecting [5], each of which must be detected. Detected: [10/10, 5/5]. This includes a priority bug that no invariant can see and only the differential comparison catches. |
| Checked containers | Unit tests and the fuzzer were run with `-D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS`. [State the result of the stale-iterator sensitivity check.] |

**Notes on the process**

- A stale list iterator can stay "valid" by accident when the allocator hands back the same memory,
  so an invariant that only compares iterators can miss it. It was found while testing the checker.
- The priority bug mentioned is the modify increase case where the order quantity grows in place. Since no other invariant changes here, only a comparison to a reference engine can catch this.
- The fuzzer's own generator had a bug (arguments to `uniform_int_distribution` swapped by mistake, which is undefined behaviour),
  which produced prices across the whole 64-bit range and quietly removed all queueing. The coverage counters exposed it.


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
| Power / load | plugged in, Balanced mode |
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
| Baseline | 5 | 13.67 | 73.14 | 12.78 | 13.96 | 8.6% | `0x329ecc77e01ae5e0` |

The checksum is an order-dependent hash of every trade plus the final book state.
It must be identical after every optimisation; a change means behaviour changed.
Run-to-run spread is about 10%, so only differences clearly larger than that are treated as real.

### Optimisations

Each row is one change on top of the previous one. The unit tests, invariants and fuzzer pass before benchmarking.

| Step | What changed | Median (M ops/s) | vs. baseline | Checksums match |
|---|---|---|---|---|
| 1 | `finder_.reserve(1<<20)` | 12.26 -> 14.45 | 1.12x (range 1.10x to 1.20x over 6 interleaved pairs) | yes |
| 2 | [Pooled node allocation] | [ ] | [ ] | [ ] |
| 3 | [Flat array by price tick] | [ ] | [ ] | [ ] |

Reserve: reserve space upfront for the finder map (8 MB). 
Measured by alternating the two configurations in one binary; every pair favoured the reserve. This was tested by moving the reserve out of the timed check and inside. In both cases, the reserve-up-front strategy shows ~1.12x gain, proving that this optimizes steady-state rehash cost rather than just startup work.


## What did not work
Compiler flags: Adding `-O3` and `-flto` flags have no measurable effect.



### Latency

A separate pass on a fresh book, timing a sample of about 50,000 operations of each kind.
Because the clock ticks every 100 ns and one `now()` call costs about 25 ns, **p50 and p90 are not resolvable**
(read p50 = 100 as "about one tick or less"). The p99 and p99.9 columns are the informative ones.

| Operation | p50 | p90 | p99 | p99.9 | Mean (raw) |
|---|---|---|---|---|---|
| Limit | 100 | 200 | 300 | 600 | 118 |
| Market | 100 | 200 | 300 | 600 | 95 |
| Cancel | 100 | 100 | 200 | 400 | 85 |
| Modify | 100 | 100 | 200 | 400 | 77 |

All values in nanoseconds (seed 4). The raw mean includes about 25 ns of clock overhead.

### Where the time goes

Sampled mean per operation (clock overhead subtracted) times the number of operations, approximate:
limit adds about 56% of runtime, cancels about 32%, modifies about 7%, market orders about 5%.
Limit adds and cancels dominate even though both are O(1) or O(log L) on paper, which points at memory
behaviour (heap allocation and cache misses) and not instruction count. [Update after the allocator experiment.]

## Limitations and future work

- Single thread, single instrument; no IOC / FOK / stop orders and no auction phases.
- Benchmarked on Windows with MinGW GCC only; numbers are not directly comparable to Linux results.
- Latency percentiles are limited by the 100 ns clock.
- Not implemented: market simulator, market-making strategy and markout analysis.