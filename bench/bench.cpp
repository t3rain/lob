#include <chrono>
#include <random>
#include <iostream>
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

        if(p < 45 || live_ids.empty()) {
            // limit
            Quantity q = qty_d(rng);
            int k = off_d(rng);
            Price pr = (s == OrderType::BUY) ? mid - k : mid + k;
            workload.push_back({LIMIT, next_id, s, pr, q});
            live_ids.push_back(next_id++);       
        }
        else if (p < 85){
            // cancel
            size_t window = std::min(live_ids.size(), size_t{100});
            size_t idx = (pct(rng) < 90) ? live_ids.size() - 1 - (rng() % window)   :   rng() % live_ids.size();
            uint64_t id = live_ids[idx];
            workload.push_back({CANCEL, id, s, 0, 0});
            live_ids[idx] = live_ids.back();
            live_ids.pop_back();
            next_id++;
        }
        else if (p < 95){
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
    return workload;
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

        uint64_t cancel_tot = 0;
        uint64_t cancel_ok = 0;
        
        uint64_t modify_tot = 0;
        uint64_t modify_ok = 0;

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
                    cancel_tot++;
                    cancel_ok += b.cancel(o.id);
                    break;
                case MODIFY:
                    modify_tot++;
                    modify_ok += b.modify(o.id, o.qty);
                    break;
                default:
                    std::cout<<"Invalid";
                    break;
            }
            for (const auto& t : tr) checksum += t.qty * 31 + t.resting;
        }
        auto t1 = std::chrono::steady_clock::now();
        
        std::chrono::duration<double> elapsed = t1 - t0;
        double seconds = elapsed.count();
        double ops_per_sec = n / seconds;
        double ns_per_op = seconds*1e9/n;
        std::cout << "cancel hit " << double(cancel_ok)/cancel_tot 
                    << " modify hit " << double(modify_ok)/modify_tot << "\n";
        std::cout << "checksum " << checksum << "\n";
        std::cout << "Iteration " << rep << ": " << ops_per_sec / 1e6 << " M ops/s, " << ns_per_op << " ns/op\n";
        // print: ops/sec, checksum, final best bid/ask
    }

    return 0;
}