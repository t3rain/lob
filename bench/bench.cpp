#include <chrono>
#include <random>
#include "order_book.h"

struct Op { uint8_t kind; OrderId id; OrderType side; Price price; Quantity qty; };

std::vector<Op> make_workload(uint64_t seed, size_t n){

    std::vector<Op> workload;
    workload.reserve(n);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<Quantity> qty_d(1, 50);
    std::uniform_int_distribution<int> off_d(-1, 20);

    Price mid = 10'000;
    uint64_t next_id = 1;
    std::vector<uint64_t> live_ids;

    for(uint64_t i{0}; i<n; i++){

        if(i%50 == 0)   mid += (rng() & 1) ? 1 : -1;

        int p = pct(rng);
        OrderType s = (rng() & 1) ? OrderType::BUY : OrderType::SELL;

        if(p < 45) {
            // limit
            Quantity q = qty_d(rng);
            int k = off_d(rng);
            Price p = (s == OrderType::BUY) ? mid - k : mid + k;
            workload.push_back({LIMIT, next_id, s, p, q});  // is emplace() better?
            live_ids.push_back(next_id++);       
        }
        else if (p < 65){
            // cancel
            size_t window = std::min(live_ids.size(), size_t{100});
            size_t idx = (pct(rng) < 90) ? live_ids.size() - 1 - (rng() % window)   :   rng() % live_ids.size();
            uint64_t id = live_ids[idx];
            workload.push_back({CANCEL, id, s, 0, 0});
            live_ids.erase(live_ids.begin() + idx);
            next_id++;
        }
        else if (p < 90){
            // modify
            size_t window = std::min(live_ids.size(), size_t{100});
            size_t idx = (pct(rng) < 90) ? live_ids.size() - 1 - (rng() % window)   :   rng() % live_ids.size();
            uint64_t id = live_ids[idx];
            workload.push_back({MODIFY, id, s, 0, qty_d(rng)});
            next_id++;
        }
        else {
            workload.push_back({MARKET, next_id++, s, 0, qty_d(rng)});
        }
    }
}

int main(int argc, char** argv) {

    int seed = 1;
    uint64_t n = 5'000'000;

    if(argc >= 2)   seed = std::stoi(argv[1]);
    if(argc >= 3)   n  = std::stoull(argv[2]);

    auto ops = make_workload(seed, n);

    for (int rep = 0; rep < 5; ++rep) {
        OrderBook b; std::vector<Trade> tr; uint64_t checksum = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (const Op& o : ops) {
            tr.clear();
            switch (o.kind) { /* call add_limit / cancel / modify / add_market */ }
            for (const auto& t : tr) checksum += t.qty * 31 + t.resting;
        }
        auto t1 = std::chrono::steady_clock::now();
        // print: ops/sec, checksum, final best bid/ask
    }
}