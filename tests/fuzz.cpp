// "tests/fuzz.cpp"

#include <iostream>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <set>
#include "order_book.h"
#include "reference_book.h"

const auto BUY = OrderType::BUY;
const auto SELL = OrderType::SELL;

template <class B>
bool order(B& b, OrderId id, OrderType t, Price p, Quantity q, std::vector<Trade>& tr) { return b.add_order(id, t, q, p, tr); }

template <class B>
bool market(B& b, OrderId id, OrderType t, Quantity q, std::vector<Trade>& tr) { return b.add_market(id, t, q, tr); }

template <class B>
bool cancel(B& b, OrderId id) { return b.cancel(id); }

template <class B>
bool modify(B& b, OrderId id, Quantity q) { return b.modify(id, q); }

struct stats{
    uint64_t trades = 0, partial_maker_fills = 0;
    uint64_t mod_reduce = 0, mod_equal = 0, mod_increase = 0, mod_invalid = 0, mod_dead = 0;
    uint64_t cancel_on_partial = 0, modify_on_partial = 0;
    uint64_t limit_cnt_t = 0, limit_cnt_f = 0;
    uint64_t market_cnt_t = 0, market_cnt_f = 0;
    uint64_t cancel_cnt_t = 0, cancel_cnt_f = 0;
    uint64_t modify_cnt_t = 0, modify_cnt_f = 0;
    uint64_t book_empty = 0;
    uint64_t max_live = 0;
    uint64_t max_queue = 0;
};

void print_stats(const stats& s) {
    std::cout << "STATISTICS:\n"
              << "limit   TOTAL: " << (s.limit_cnt_t + s.limit_cnt_f)
              << "  success: " << s.limit_cnt_t << "  rejected: " << s.limit_cnt_f << "\n"
              << "market  TOTAL: " << (s.market_cnt_t + s.market_cnt_f)
              << "  success: " << s.market_cnt_t << "  rejected: " << s.market_cnt_f << "\n"
              << "cancel  TOTAL: " << (s.cancel_cnt_t + s.cancel_cnt_f)
              << "  success: " << s.cancel_cnt_t << "  failed: " << s.cancel_cnt_f << "\n"
              << "modify  TOTAL: " << (s.modify_cnt_t + s.modify_cnt_f)
              << "  success: " << s.modify_cnt_t << "  failed: " << s.modify_cnt_f << "\n"
              << "trades: " << s.trades
              << "  partial maker fills: " << s.partial_maker_fills << "\n"
              << "modify kinds: reduce " << s.mod_reduce
              << ", equal " << s.mod_equal
              << ", increase " << s.mod_increase
              << ", invalid qty " << s.mod_invalid
              << ", dead id " << s.mod_dead << "\n"
              << "cancel on partially filled: " << s.cancel_on_partial
              << "  modify on partially filled: " << s.modify_on_partial << "\n"
              << "book empty: " << s.book_empty << "\n"
              << "max live: " << s.max_live << "  max queue: " << s.max_queue << "\n";
}

stats st;
std::set<OrderId> partial;



[[noreturn]] void fail(uint64_t seed, int step, const std::string& op, const std::string& reason){
    std::cout<<"MISMATCH seed: "<<seed<<" step: "<<step<<"\n"
            <<"     operation: "<<op<<"\n"
            <<"     problem:   "<<reason<<"\n";
    std::exit(1);
}

OrderId pick_id(std::mt19937_64& rng, const std::vector<OrderId>& live, OrderId next_id){

    std::uniform_int_distribution<int> pct(0, 99);
    if(!live.empty() && pct(rng) < 90){
        std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
        return live[pick(rng)];
    }
    std::uniform_int_distribution<OrderId> any(1, next_id);
    return any(rng);

}

int main(int argc, char** argv){

    uint64_t seed = 1;
    int steps = 1e4;
    Price lo = 97, hi = 103;
    if(argc >= 2)   seed = std::stoull(argv[1]);
    if(argc >= 3)   steps = std::stoi(argv[2]);
    if (argc >= 4) { Price w = std::stoll(argv[3]); lo = 100 - w; hi = 100 + w; }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int> side_d(0, 1);
    std::uniform_int_distribution<Price> price_d(lo, hi);
    std::uniform_int_distribution<Quantity> qty_d(1, 10);
    std::uniform_int_distribution<Quantity> lim_qty_d(5, 30);

    OrderBook lob;
    ReferenceBook ref;
    OrderId next_id = 1;

    auto note_trades = [&](const std::vector<Trade>& tr) {
        for (const auto& t : tr) {
            ++st.trades;
            if (ref.qty_of(t.resting)) ++st.partial_maker_fills;   
            for (OrderId oid : {t.resting, t.incoming}) {
                if (ref.qty_of(oid)) partial.insert(oid);         
                else                 partial.erase(oid);       
            }
        }
    };


    for(int step{0}; step<steps; step++){

        std::vector<Trade> lob_trade, ref_trade;
        bool ok_lob, ok_ref;
        std::string op;

        int choose = pct(rng);
        OrderType side = side_d(rng) ? BUY : SELL;
        OrderId id = next_id++;

        if(choose < 45){

            // limit
            Price p = price_d(rng);

            if(pct(rng) < 5){
                std::vector<OrderId> live_ids = ref.live_ids();
                id = pick_id(rng, live_ids, id);
            }

            Quantity q = lim_qty_d(rng);
            op = "limit id = " + std::to_string(id) + " " + (side == BUY ? "BUY" : "SELL")
                + " price = " + std::to_string(p) + " quantity = " + std::to_string(q);
            ok_lob = order(lob, id, side, p, q, lob_trade);
            ok_ref = order(ref, id, side, p, q, ref_trade);
            note_trades(ref_trade);

            if(ok_lob)  st.limit_cnt_t++;
            else        st.limit_cnt_f++;
        }

        else if(choose < 55){

            // cancel
            std::vector<OrderId> live_ids = ref.live_ids();

            if(live_ids.empty())    st.book_empty++;

            OrderId cancel_id = pick_id(rng, live_ids, id);
            op = "cancel id = " + std::to_string(cancel_id);

            bool was_partial = partial.count(cancel_id) && ref.qty_of(cancel_id);

            ok_lob = cancel(lob, cancel_id);
            ok_ref = cancel(ref, cancel_id);

            if (ok_ref) {
                if (was_partial) ++st.cancel_on_partial;
                partial.erase(cancel_id);
            }
            
            if(ok_lob)  st.cancel_cnt_t++;
            else        st.cancel_cnt_f++;
        }

        else if(choose < 90){

            // modify 
            std::vector<OrderId> live_ids = ref.live_ids();

            if(live_ids.empty())    st.book_empty++;

            OrderId modify_id = pick_id(rng, live_ids, id);
            auto old = ref.qty_of(modify_id);
            bool was_partial = partial.count(modify_id) && old;
            
            Quantity q = qty_d(rng) * 2;
            int r = pct(rng);
            if      (r < 5)             q = 0;                 
            else if (r < 8)             q = -qty_d(rng);       
            else if (r < 18 && old)     q = *old;              

            op = "modify id = " + std::to_string(id) + " quantity = " + std::to_string(q);

            ok_lob = modify(lob, modify_id, q);
            ok_ref = modify(ref, modify_id, q);
            
            if (!old)            ++st.mod_dead;           
            else if (q <= 0)    ++st.mod_invalid;         
            else {
                if      (q < *old)  ++st.mod_reduce;
                else if (q == *old) ++st.mod_equal;
                else                 ++st.mod_increase;
                if (was_partial) ++st.modify_on_partial;
            }
            if(ok_lob)  st.modify_cnt_t++;
            else        st.modify_cnt_f++;
        }
        else{
            
            // market
            Quantity q = qty_d(rng);

            if(pct(rng) < 5){
                std::vector<OrderId> live_ids = ref.live_ids();
                id = pick_id(rng, live_ids, id);
            }

            op = "market id = " + std::to_string(id) + " " + (side == BUY ? "BUY" : "SELL")
                + " quantity = " + std::to_string(q);
            ok_lob = market(lob, id, side, q, lob_trade);
            ok_ref = market(ref, id, side, q, ref_trade);
            if(ok_lob)  st.market_cnt_t++;
            else        st.market_cnt_f++;
        }

        st.max_live = std::max(st.max_live, ref.live_count());
        st.max_queue = std::max(st.max_queue, ref.longest_queue());


        if(ok_lob != ok_ref){
            fail(seed, step, op, "return values differ: lob - " + std::to_string(ok_lob) + ", ref - " + std::to_string(ok_ref));
        }

        if (lob_trade.size() != ref_trade.size())
            fail(seed, step, op, "trade count differs: real=" + std::to_string(lob_trade.size())
                                 + " ref=" + std::to_string(ref_trade.size()));

        for (size_t i = 0; i < lob_trade.size(); ++i) {
            const Trade& a = lob_trade[i]; const Trade& b = ref_trade[i];
            if (a.price != b.price || a.qty != b.qty || a.resting != b.resting || a.incoming != b.incoming)
                fail(seed, step, op, "trade #" + std::to_string(i) + " differs: lob=(px "
                     + std::to_string(a.price) + ", qty " + std::to_string(a.qty) + ", resting "
                     + std::to_string(a.resting) + ") ref=(px " + std::to_string(b.price)
                     + ", qty " + std::to_string(b.qty) + ", maker " + std::to_string(b.resting) + ")");
        }

        auto rb = lob.best_bid(); auto fb = ref.best_bid_price();
        auto ra = lob.best_ask(); auto fa = ref.best_ask_price();
        if (rb != fb) fail(seed, step, op, "best bid differs");
        if (ra != fa) fail(seed, step, op, "best ask differs");

        for (Price p = 95; p <= 105; ++p) {
            if (lob.depth_at(BUY, p)  != ref.depth_at(BUY, p))  fail(seed, step, op, "bid depth differs at " + std::to_string(p));
            if (lob.depth_at(SELL, p) != ref.depth_at(SELL, p)) fail(seed, step, op, "ask depth differs at " + std::to_string(p));
        }

        if (!lob.check_invariants()) fail(seed, step, op, "check_invariants() failed on the real engine");

    }
    
    std::cout << "seed " << seed << ": " << steps << " steps OK\n";
    print_stats(st);

    return 0;
}