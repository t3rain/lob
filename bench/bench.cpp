#include <chrono>
#include <random>
#include <algorithm>
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

// ensure the vector is passed in sorted
uint64_t percentile(const std::vector<uint64_t>& timings, double p){
    size_t idx = static_cast<size_t>(std::ceil(p * timings.size()) - 1);
    return timings[idx];
}

struct Throughput {
    std::vector<double> ops_per_sec;
    std::vector<double> ns_per_op;
};

struct Latency {
    std::vector<uint64_t> limit_latencies_all;
    std::vector<uint64_t> market_latencies_all;
    std::vector<uint64_t> cancel_latencies_all;
    std::vector<uint64_t> modify_latencies_all;

    std::vector<uint64_t> limit_latencies_p50;
    std::vector<uint64_t> limit_latencies_p90;
    std::vector<uint64_t> limit_latencies_p99;
    std::vector<uint64_t> limit_latencies_p99_9;

    std::vector<uint64_t> market_latencies_p50;
    std::vector<uint64_t> market_latencies_p90;
    std::vector<uint64_t> market_latencies_p99;
    std::vector<uint64_t> market_latencies_p99_9;

    std::vector<uint64_t> cancel_latencies_p50;
    std::vector<uint64_t> cancel_latencies_p90;
    std::vector<uint64_t> cancel_latencies_p99;
    std::vector<uint64_t> cancel_latencies_p99_9;

    std::vector<uint64_t> modify_latencies_p50;
    std::vector<uint64_t> modify_latencies_p90;
    std::vector<uint64_t> modify_latencies_p99;
    std::vector<uint64_t> modify_latencies_p99_9;
};

void print_stats(Throughput& th, Latency& lt){

    std::cout << "THROUGHPUT: \n"; 
    for(size_t i{0}; i<20; i++){
        std::cout << "Iteration " << i+1 << " " << th.ops_per_sec[i]/1e6 << " M ops/sec " << th.ns_per_op[i] << " ns/op\n"; 
    }

    std::cout << "\nLATENCY: \n";
    for(size_t i{0}; i<20; i++){
        std::cout << "Iteration " << i+1 << ":\n" 
        << "limit latencies: p50 " << lt.limit_latencies_p50[i] << " p90 " << lt.limit_latencies_p90[i]
        << " p99 " << lt.limit_latencies_p99[i] << " p99.9 " << lt.limit_latencies_p99_9[i] << "\n"
        << "market latencies: p50 " << lt.market_latencies_p50[i] << " p90 " << lt.market_latencies_p90[i]
        << " p99 " << lt.market_latencies_p99[i] << " p99.9 " << lt.market_latencies_p99_9[i] << "\n"
        << "cancel latencies: p50 " << lt.cancel_latencies_p50[i] << " p90 " << lt.cancel_latencies_p90[i]
        << " p99 " << lt.cancel_latencies_p99[i] << " p99.9 " << lt.cancel_latencies_p99_9[i] << "\n"
        << "modify latencies: p50 " << lt.modify_latencies_p50[i] << " p90 " << lt.modify_latencies_p90[i]
        << " p99 " << lt.modify_latencies_p99[i] << " p99.9 " << lt.modify_latencies_p99_9[i] << "\n";
    }

    std::sort(lt.limit_latencies_all.begin(), lt.limit_latencies_all.end());
    std::sort(lt.market_latencies_all.begin(), lt.market_latencies_all.end());
    std::sort(lt.cancel_latencies_all.begin(), lt.cancel_latencies_all.end());
    std::sort(lt.modify_latencies_all.begin(), modify_latencies_all.end());
    
    std::cout << "Overall latencies:\n" 
        << "limit latencies: p50 " << percentile(lt.limit_latencies_all, 0.5) << " p90 " << percentile(lt.limit_latencies_all, 0.9) 
        << " p99 " << percentile(lt.limit_latencies_all, 0.99)  << " p99.9 " << percentile(lt.limit_latencies_all, 0.999)  << "\n"
        << "market latencies: p50 " << percentile(lt.market_latencies_all, 0.5) << " p90 " << percentile(lt.market_latencies_all, 0.9)
        << " p99 " << percentile(lt.market_latencies_all, 0.99) << " p99.9 " << percentile(lt.market_latencies_all, 0.999) << "\n"
        << "cancel latencies: p50 " << percentile(lt.cancel_latencies_all, 0.5) << " p90 " << percentile(lt.cancel_latencies_all, 0.9) 
        << " p99 " << percentile(lt.cancel_latencies_all, 0.99) << " p99.9 " << percentile(lt.cancel_latencies_all, 0.999) << "\n"
        << "modify latencies: p50 " << percentile(lt.modify_latencies_all, 0.5) << " p90 " << percentile(lt.modify_latencies_all, 0.9) 
        << " p99 " << percentile(lt.modify_latencies_all, 0.99) << " p99.9 " << percentile(lt.modify_latencies_all, 0.999) << "\n";
    std::cout << "\n";
    return;
}



int main(int argc, char** argv) {

    int seed = 1;
    uint64_t n = 5'000'000;

    if(argc >= 2)   seed = std::stoi(argv[1]);
    if(argc >= 3)   n  = std::stoull(argv[2]);

    auto ops = make_workload(seed, n);

    Throughput th;
    Latency lt;
    
    bool warmup = true;
    for (int rep = 0; rep < 21; ++rep) {

        if(rep == 1)    warmup = false;
        OrderBook b; std::vector<Trade> tr;

        uint64_t limits = 0;
        uint64_t markets = 0;
        uint64_t cancels = 0;
        uint64_t modifys = 0;

        std::vector<uint64_t> limit_latencies;
        limit_latencies.reserve(50000);

        std::vector<uint64_t> market_latencies;
        market_latencies.reserve(50000);

        std::vector<uint64_t> cancel_latencies;
        cancel_latencies.reserve(50000);

        std::vector<uint64_t> modify_latencies;
        modify_latencies.reserve(50000);
        
        auto t0 = std::chrono::steady_clock::now();

        for (const Op& o : ops) {
            tr.clear();
            if(o.kind == LIMIT){
                if(++limits%45 == 0){
                    auto start = std::chrono::steady_clock::now();
                    b.add_order(o.id, o.side, o.qty, o.price, tr);
                    auto end = std::chrono::steady_clock::now();
                    limit_latencies.push_back(
                        static_cast<std::chrono::nanoseconds>(end-start).count()
                    );
                }
                b.add_order(o.id, o.side, o.qty, o.price, tr);
            }
            else if(o.kind == CANCEL){
                if(++cancels%40 == 0){
                    auto start = std::chrono::steady_clock::now();
                    b.cancel(o.id);
                    auto end = std::chrono::steady_clock::now();
                    cancel_latencies.push_back(
                        static_cast<std::chrono::nanoseconds>(end-start).count()
                    );
                }
                b.cancel(o.id);
            }
            else if(o.kind == MODIFY){
                if(++modifys%10 == 0){
                    auto start = std::chrono::steady_clock::now();
                    b.modify(o.id, o.qty);
                    auto end = std::chrono::steady_clock::now();
                    modify_latencies.push_back(
                        static_cast<std::chrono::nanoseconds>(end-start).count()
                    );
                }
                b.modify(o.id, o.qty);
            }
            else{
                if(++markets%5 == 0){
                    auto start = std::chrono::steady_clock::now();
                    b.add_market(o.id, o.side, o.qty, tr);
                    auto end = std::chrono::steady_clock::now();
                    market_latencies.push_back(
                        static_cast<std::chrono::nanoseconds>(end-start).count()
                    );
                }
                b.add_market(o.id, o.side, o.qty, tr);
            }
            //for (const auto& t : tr) checksum += t.qty * 31 + t.resting;
        }
        auto t1 = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = t1 - t0;
        double seconds = elapsed.count();

        if(!warmup){
            double ops = n / seconds;
            double nspop = seconds*1e9/n ;
            th.ops_per_sec.push_back(ops);
            th.ns_per_op.push_back(nspop);

            std::sort(limit_latencies.begin(), limit_latencies.end());
            std::sort(market_latencies.begin(), market_latencies.end());
            std::sort(cancel_latencies.begin(), cancel_latencies.end());
            std::sort(modify_latencies.begin(), modify_latencies.end());

            lt.limit_latencies_p50.push_back(percentile(limit_latencies, 0.5));
            lt.limit_latencies_p90.push_back(percentile(limit_latencies, 0.9));
            lt.limit_latencies_p99.push_back(percentile(limit_latencies, 0.99));
            lt.limit_latencies_p99_9.push_back(percentile(limit_latencies, 0.999));

            lt.market_latencies_p50.push_back(percentile(market_latencies, 0.5));
            lt.market_latencies_p90.push_back(percentile(market_latencies, 0.9));
            lt.market_latencies_p99.push_back(percentile(market_latencies, 0.99));
            lt.market_latencies_p99_9.push_back(percentile(market_latencies, 0.999));

            lt.cancel_latencies_p50.push_back(percentile(cancel_latencies, 0.5));
            lt.cancel_latencies_p90.push_back(percentile(cancel_latencies, 0.9));
            lt.cancel_latencies_p99.push_back(percentile(cancel_latencies, 0.99));
            lt.cancel_latencies_p99_9.push_back(percentile(cancel_latencies, 0.999));

            lt.modify_latencies_p50.push_back(percentile(modify_latencies, 0.5));
            lt.modify_latencies_p90.push_back(percentile(modify_latencies, 0.9));
            lt.modify_latencies_p99.push_back(percentile(modify_latencies, 0.99));
            lt.modify_latencies_p99_9.push_back(percentile(modify_latencies, 0.999));

            lt.limit_latencies_all.insert(lt.limit_latencies_all.end(), limit_latencies.begin(), limit_latencies.end());
            lt.market_latencies_all.insert(lt.market_latencies_all.end(), market_latencies.begin(), market_latencies.end());
            lt.cancel_latencies_all.insert(lt.cancel_latencies_all.end(), cancel_latencies.begin(), cancel_latencies.end());
            lt.modify_latencies_all.insert(lt.modify_latencies_all.end(), modify_latencies.begin(), modify_latencies.end());
        }

        // print: ops/sec, checksum, final best bid/ask
    }

    print_stats(th, lt);
    // std::cout << "checksum " << checksum << "\n";
    // std::cout << "Iteration " << rep << ": " << ops_per_sec / 1e6 << " M ops/s, " << ns_per_op << " ns/op\n";

    return 0;
}