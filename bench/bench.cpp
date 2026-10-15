// "bench/bench.cpp"
//
// Usage:  bench_rel.exe [seed] [n_ops] [reps]      (defaults: 1  5000000  10)
// Build:  .\build.ps1 bench -Mode release          (-O2 -DNDEBUG; never benchmark a debug build)
//
// Two separate passes, each on a fresh OrderBook:
//   1. THROUGHPUT pass: no per-operation timing. One warm-up run (not reported), then `reps` timed runs.
//      Each run computes a checksum of every trade plus the final book. The checksum must be identical
//      across runs, and identical after every optimisation. If it changes, behaviour changed.
//   2. LATENCY pass: a sample of operations is timed individually (see the clock caveat printed at the top).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include "order_book.h"

using clk = std::chrono::steady_clock;

struct Op { uint8_t kind; OrderId id; OrderType side; Price price; Quantity qty; };

struct WorkloadInfo {
    uint64_t n_limit = 0, n_cancel = 0, n_modify = 0, n_market = 0;
    size_t   final_live = 0;                 // ids the generator still believes are resting
    Price    min_price = 0, max_price = 0;   // over limit orders
};

static std::vector<Op> make_workload(uint64_t seed, size_t n, WorkloadInfo& info) {

    std::vector<Op> workload;
    workload.reserve(n);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<Quantity> qty_d(1, 50);
    std::uniform_int_distribution<int> off_d(-1, 20);

    Price mid = 10'000;
    uint64_t next_id = 1;
    std::vector<uint64_t> live_ids;
    bool first_price = true;

    for (uint64_t i = 0; i < n; ++i) {

        if (i % 50 == 0) mid += (rng() & 1) ? 1 : -1;

        int p = pct(rng);
        OrderType s = (rng() & 1) ? OrderType::BUY : OrderType::SELL;

        if (p < 45 || live_ids.empty()) {
            // limit
            Quantity q = qty_d(rng);
            int k = off_d(rng);
            Price pr = (s == OrderType::BUY) ? mid - k : mid + k;
            workload.push_back({LIMIT, next_id, s, pr, q});
            live_ids.push_back(next_id++);
            ++info.n_limit;
            if (first_price) { info.min_price = info.max_price = pr; first_price = false; }
            info.min_price = std::min(info.min_price, pr);
            info.max_price = std::max(info.max_price, pr);
        }
        else if (p < 85) {
            // cancel: mostly recent orders, sometimes any
            size_t window = std::min(live_ids.size(), size_t{100});
            size_t idx = (pct(rng) < 90) ? live_ids.size() - 1 - (rng() % window)
                                         : rng() % live_ids.size();
            uint64_t id = live_ids[idx];
            workload.push_back({CANCEL, id, s, 0, 0});
            live_ids[idx] = live_ids.back();     // swap-and-pop: O(1) removal
            live_ids.pop_back();
            next_id++;
            ++info.n_cancel;
        }
        else if (p < 95) {
            // modify
            size_t window = std::min(live_ids.size(), size_t{100});
            size_t idx = (pct(rng) < 90) ? live_ids.size() - 1 - (rng() % window)
                                         : rng() % live_ids.size();
            uint64_t id = live_ids[idx];
            workload.push_back({MODIFY, id, s, 0, qty_d(rng)});
            next_id++;
            ++info.n_modify;
        }
        else {
            workload.push_back({MARKET, next_id++, s, 0, qty_d(rng)});
            ++info.n_market;
        }
    }
    info.final_live = live_ids.size();
    return workload;
}

// ---------------------------------------------------------------- helpers
static constexpr uint64_t FNV_PRIME = 1099511628211ULL;

static inline void mix(uint64_t& h, uint64_t v) { h = h * FNV_PRIME + v; }   // order-dependent hash

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t m = v.size() / 2;
    return (v.size() % 2) ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

// v must already be sorted
static uint64_t percentile(const std::vector<uint64_t>& v, double p) {
    if (v.empty()) return 0;
    size_t rank = static_cast<size_t>(std::ceil(p * v.size()));
    if (rank == 0) rank = 1;
    return v[rank - 1];
}

static uint64_t ns_between(clk::time_point a, clk::time_point b) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
}

// Smallest observed clock tick, and the average cost of one now() call.
static void measure_clock() {
    uint64_t tick = UINT64_MAX;
    for (int i = 0; i < 20000; ++i) {
        auto a = clk::now(), b = clk::now();
        while (b == a) b = clk::now();                     // spin until the clock advances
        tick = std::min(tick, ns_between(a, b));
    }
    const int N = 1'000'000;
    uint64_t sum = 0;
    for (int i = 0; i < N; ++i) { auto a = clk::now(); auto b = clk::now(); sum += ns_between(a, b); }
    std::cout << "clock: smallest tick " << tick << " ns, average now()-to-now() " << double(sum) / N << " ns\n"
              << "       latency values below about 2 ticks are not resolvable\n\n";
}

// ---------------------------------------------------------------- throughput pass
struct RunResult {
    double   seconds = 0;
    uint64_t checksum = 0;
    uint64_t cancel_ok = 0, cancel_tot = 0, modify_ok = 0, modify_tot = 0, n_trades = 0;
};

static RunResult run_throughput(const std::vector<Op>& ops, const WorkloadInfo& info, size_t reserve) {

    
    std::vector<Trade> tr;
    tr.reserve(64);

    RunResult r;
    uint64_t cs = 0;

    auto t0 = clk::now();
    OrderBook b(reserve);
    for (const Op& o : ops) {
        tr.clear();
        switch (o.kind) {
            case LIMIT:
                b.add_order(o.id, o.side, o.qty, o.price, tr);
                break;
            case MARKET:
                b.add_market(o.id, o.side, o.qty, tr);
                break;
            case CANCEL:
                ++r.cancel_tot;
                r.cancel_ok += b.cancel(o.id) ? 1 : 0;
                break;
            case MODIFY:
                ++r.modify_tot;
                r.modify_ok += b.modify(o.id, o.qty) ? 1 : 0;
                break;
            default:
                std::cout << "invalid op kind\n";
                break;
        }
        for (const auto& t : tr) {
            mix(cs, static_cast<uint64_t>(t.price));
            mix(cs, static_cast<uint64_t>(t.qty));
            mix(cs, static_cast<uint64_t>(t.resting));
            mix(cs, static_cast<uint64_t>(t.incoming));
        }
        r.n_trades += tr.size();
    }

    auto t1 = clk::now();
    r.seconds = std::chrono::duration<double>(t1 - t0).count();

    // Final-state snapshot, folded into the checksum. Outside the timed region.
    mix(cs, static_cast<uint64_t>(b.best_bid().value_or(0)));
    mix(cs, static_cast<uint64_t>(b.best_ask().value_or(0)));
    for (Price p = info.min_price - 2; p <= info.max_price + 2; ++p) {
        mix(cs, static_cast<uint64_t>(b.depth_at(OrderType::BUY, p)));
        mix(cs, static_cast<uint64_t>(b.depth_at(OrderType::SELL, p)));
    }
    r.checksum = cs;
    return r;
}

// ---------------------------------------------------------------- latency pass
struct LatencySamples {
    std::vector<uint64_t> limit, market, cancel, modify;
};

static LatencySamples run_latency(const std::vector<Op>& ops, size_t reserve) {

    OrderBook b(reserve);
    std::vector<Trade> tr;
    tr.reserve(64);

    LatencySamples ls;
    ls.limit.reserve(ops.size() / 45 / 20 + 16);
    ls.market.reserve(ops.size() / 5 / 20 + 16);
    ls.cancel.reserve(ops.size() / 40 / 20 + 16);
    ls.modify.reserve(ops.size() / 10 / 20 + 16);

    uint64_t c_limit = 0, c_market = 0, c_cancel = 0, c_modify = 0;

    // Each operation executes exactly once; only some of them are timed.
    for (const Op& o : ops) {
        tr.clear();
        switch (o.kind) {
            case LIMIT:
                if (++c_limit % 45 == 0) {
                    auto a = clk::now();
                    b.add_order(o.id, o.side, o.qty, o.price, tr);
                    auto z = clk::now();
                    ls.limit.push_back(ns_between(a, z));
                } else {
                    b.add_order(o.id, o.side, o.qty, o.price, tr);
                }
                break;
            case MARKET:
                if (++c_market % 5 == 0) {
                    auto a = clk::now();
                    b.add_market(o.id, o.side, o.qty, tr);
                    auto z = clk::now();
                    ls.market.push_back(ns_between(a, z));
                } else {
                    b.add_market(o.id, o.side, o.qty, tr);
                }
                break;
            case CANCEL:
                if (++c_cancel % 40 == 0) {
                    auto a = clk::now();
                    b.cancel(o.id);
                    auto z = clk::now();
                    ls.cancel.push_back(ns_between(a, z));
                } else {
                    b.cancel(o.id);
                }
                break;
            case MODIFY:
                if (++c_modify % 10 == 0) {
                    auto a = clk::now();
                    b.modify(o.id, o.qty);
                    auto z = clk::now();
                    ls.modify.push_back(ns_between(a, z));
                } else {
                    b.modify(o.id, o.qty);
                }
                break;
            default:
                break;
        }
    }
    return ls;
}

static void print_latency_row(const char* name, std::vector<uint64_t>& v) {
    std::sort(v.begin(), v.end());
    uint64_t sum = 0;
    for(const auto& x: v)   sum += x;
    double mean = v.empty() ? 0 : (double)sum/v.size();
    std::cout << std::left << std::setw(8) << name << std::right
              << " samples " << std::setw(8) << v.size()
              << "  p50 " << std::setw(6) << percentile(v, 0.50)
              << "  p90 " << std::setw(6) << percentile(v, 0.90)
              << "  p99 " << std::setw(6) << percentile(v, 0.99)
              << "  p99.9 " << std::setw(6) << percentile(v, 0.999)
              << "  mean " << std::setw(8) << mean << "   (ns)\n";
}

// ---------------------------------------------------------------- main
int main(int argc, char** argv) {

    uint64_t seed = 1;
    size_t   n    = 5'000'000;
    int      reps = 10;

    if (argc >= 2) seed = std::stoull(argv[1]);
    if (argc >= 3) n    = std::stoull(argv[2]);
    if (argc >= 4) reps = std::stoi(argv[3]);
    size_t reserve_n = (argc >= 5) ? std::stoull(argv[4]) : (1u << 20);

    measure_clock();

    WorkloadInfo info;
    auto ops = make_workload(seed, n, info);

    std::cout << "workload: seed " << seed << ", " << n << " ops\n"
              << "  limit " << info.n_limit << "  cancel " << info.n_cancel
              << "  modify " << info.n_modify << "  market " << info.n_market << "\n"
              << "  generator live ids at end: " << info.final_live
              << "   price range drawn: " << info.min_price << ".." << info.max_price << "\n\n";

    // throughput
    run_throughput(ops, info, reserve_n);                              // warm-up

    std::vector<double> ops_per_sec;
    std::vector<uint64_t> checksums;
    RunResult last;
    for (int rep = 0; rep < reps; ++rep) {
        RunResult r = run_throughput(ops, info, reserve_n);
        ops_per_sec.push_back(n / r.seconds);
        checksums.push_back(r.checksum);
        last = r;
        std::cout << "run " << std::setw(2) << rep + 1 << ": "
                  << std::fixed << std::setprecision(2) << ops_per_sec.back() / 1e6 << " M ops/s, "
                  << r.seconds * 1e9 / n << " ns/op\n";
    }

    bool same = std::all_of(checksums.begin(), checksums.end(),
                            [&](uint64_t c) { return c == checksums.front(); });

    double med = median(ops_per_sec);
    double mn  = *std::min_element(ops_per_sec.begin(), ops_per_sec.end());
    double mx  = *std::max_element(ops_per_sec.begin(), ops_per_sec.end());

    std::cout << "\nTHROUGHPUT  median " << med / 1e6 << " M ops/s (" << 1e9 / med << " ns/op)"
              << "   min " << mn / 1e6 << "   max " << mx / 1e6 << "   spread "
              << std::setprecision(1) << 100.0 * (mx - mn) / med << "%\n";
    std::cout << "checksum 0x" << std::hex << checksums.front() << std::dec
              << (same ? "   (identical across all runs)" : "   *** DIFFERS BETWEEN RUNS ***") << "\n";
    std::cout << std::setprecision(4)
              << "cancel hit rate " << double(last.cancel_ok) / last.cancel_tot
              << "   modify hit rate " << double(last.modify_ok) / last.modify_tot
              << "   trades " << last.n_trades << "\n\n";

    //  latency
    run_latency(ops, reserve_n);                                       // warm-up, discarded
    LatencySamples ls = run_latency(ops, reserve_n);
    std::cout << "LATENCY (one pass, sampled; nanoseconds)\n";
    print_latency_row("limit",  ls.limit);
    print_latency_row("market", ls.market);
    print_latency_row("cancel", ls.cancel);
    print_latency_row("modify", ls.modify);

    return 0;
}