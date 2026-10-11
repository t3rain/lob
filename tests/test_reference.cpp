// "tests/test_reference.cpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <vector>
#include "order_book.h"
#include "reference_book.h"

const auto BUY = OrderType::BUY;
const auto SELL = OrderType::SELL;

// ---- adapters: change these if your signatures differ ----
template <class B> bool order(B& b, OrderId id, OrderType t, Price p, Quantity q, std::vector<Trade>& tr) { return b.add_order(id, t, q, p, tr); }
template <class B> bool market(B& b, OrderId id, OrderType t, Quantity q, std::vector<Trade>& tr) { return b.add_market(id, t, q, tr); }
template <class B> bool cancel(B& b, OrderId id) { return b.cancel(id); }
template <class B> bool modify(B& b, OrderId id, Quantity q) { return b.modify(id, q); }
template <class B> Quantity depth(const B& b, OrderType t, Price p) { return b.depth_at(t, p); }

// best prices as optional<Price> for both engines
inline std::optional<Price> bid_px(const OrderBook& b) { return b.best_bid(); }
inline std::optional<Price> ask_px(const OrderBook& b) { return b.best_ask(); }
inline std::optional<Price> bid_px(const ReferenceBook& b) { return b.best_bid_price(); }
inline std::optional<Price> ask_px(const ReferenceBook& b) { return b.best_ask_price(); }

// static void print_trades(std::vector<Trade>& trade){
//     for(const auto& t: trade){
//         std::cout<<"Price: "<<t.price<<", Quantity: "<<t.qty<<", Incoming: "<<t.incoming<<", Resting: "<<t.resting<<"\n";
//     }
//     return;
// }
// ----------------------------------------------------------

template <class B>
void run_suite(const char* name) {

    // N1
    {
        ReferenceBook r; std::vector<Trade> t;
        r.add_order(1, OrderType::BUY, 10, 100, t);
        r.add_order(2, OrderType::BUY, 10, 100, t);
        r.add_order(3, OrderType::BUY, 10, 100, t);
        r.add_order(4, OrderType::BUY, 10, 99,  t);
        std::cout<<r.longest_queue()<<"\n";
        assert(r.longest_queue() == 3);
    }
    // R1: empty book
    {
        B b; std::vector<Trade> t;
        assert(!bid_px(b) && !ask_px(b));
        assert(depth(b, BUY, 100) == 0 && depth(b, SELL, 100) == 0);
        assert(!cancel(b, 1) && !modify(b, 1, 5));
        market(b, 1, BUY, 10, t);
        assert(t.empty() && !ask_px(b) && !bid_px(b));   // market order never rests
    }

    // R2: rest on both sides, no trades; best prices are the BEST ones
    {
        B b; std::vector<Trade> t;
        order(b, 1, BUY, 99, 10, t);  order(b, 2, BUY, 100, 10, t);  order(b, 3, BUY, 98, 10, t);
        order(b, 4, SELL, 103, 10, t); order(b, 5, SELL, 101, 10, t); order(b, 6, SELL, 102, 10, t);
        assert(t.empty());
        assert(*bid_px(b) == 100 && *ask_px(b) == 101);
        assert(depth(b, BUY, 99) == 10 && depth(b, SELL, 102) == 10);
    }

    // R3: price priority beats arrival time (catches a backwards comparator)
    {
        B b; std::vector<Trade> t;
        order(b, 1, BUY, 99, 10, t);                  // arrived first, worse price
        order(b, 2, BUY, 100, 10, t);                 // arrived later, better price
        market(b, 3, SELL, 10, t);
        assert(t.size() == 1 && t[0].resting == 2 && t[0].price == 100);
    }
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 102, 10, t);
        order(b, 2, SELL, 101, 10, t);
        market(b, 3, BUY, 10, t);
        assert(t.size() == 1 && t[0].resting == 2 && t[0].price == 101);
    }

    // R4: FIFO within a price level
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t); order(b, 2, SELL, 101, 10, t); order(b, 3, SELL, 101, 10, t);
        market(b, 4, BUY, 15, t);
        assert(t.size() == 2);
        assert(t[0].resting == 1 && t[0].qty == 10 && t[0].price == 101 && t[0].incoming == 4);
        assert(t[1].resting == 2 && t[1].qty == 5);
        assert(depth(b, SELL, 101) == 15);
    }

    // R5: multi-level sweep, levels erased
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t); order(b, 2, SELL, 102, 10, t); order(b, 3, SELL, 103, 10, t);
        market(b, 4, BUY, 25, t);
        assert(t.size() == 3);
        assert(t[0].price == 101 && t[0].qty == 10);
        assert(t[1].price == 102 && t[1].qty == 10);
        assert(t[2].price == 103 && t[2].qty == 5);
        assert(depth(b, SELL, 101) == 0 && depth(b, SELL, 102) == 0 && depth(b, SELL, 103) == 5);
        assert(*ask_px(b) == 103);
    }

    // R6: price improvement, trade at the RESTING price
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 50, t);
        order(b, 2, BUY, 102, 50, t);
        assert(t.size() == 1 && t[0].price == 101 && t[0].qty == 50);
        assert(!bid_px(b) && !ask_px(b));
    }

    // R7: order crosses partially, remainder rests at ITS OWN order price
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 30, t);
        order(b, 2, BUY, 102, 100, t);
        assert(t.size() == 1 && t[0].qty == 30);
        assert(!ask_px(b) && *bid_px(b) == 102 && depth(b, BUY, 102) == 70);
    }

    // R8: order that does NOT cross just rests
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t);
        order(b, 2, BUY, 100, 10, t);
        assert(t.empty() && *bid_px(b) == 100 && *ask_px(b) == 101);
    }

    // R9: market order larger than the whole side; leftover discarded
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t); order(b, 2, SELL, 102, 10, t);
        market(b, 3, BUY, 50, t);
        assert(t.size() == 2 && t[0].qty == 10 && t[1].qty == 10);
        assert(!ask_px(b) && !bid_px(b));
    }

    // R10: cancel front, middle, back, only-order-at-level
    {
        B b; std::vector<Trade> t;
        order(b, 1, BUY, 99, 10, t); order(b, 2, BUY, 99, 10, t);
        order(b, 3, BUY, 99, 10, t); order(b, 4, BUY, 98, 10, t);
        assert(cancel(b, 2));
        market(b, 5, SELL, 10, t);
        assert(t.size() == 1 && t[0].resting == 1);          // front intact, middle gone
        assert(cancel(b, 3));                                  // back
        assert(depth(b, BUY, 99) == 0 && *bid_px(b) == 98);    // level gone
        assert(cancel(b, 4) && !bid_px(b));
    }

    // R11: unknown / double / filled-order cancel
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t);
        assert(!cancel(b, 42));
        market(b, 2, BUY, 10, t);                              // fills order 1 completely
        assert(!cancel(b, 1));
        order(b, 3, SELL, 101, 10, t);
        assert(cancel(b, 3) && !cancel(b, 3));
    }

    // R12: modify reduce keeps priority
    {
        B b; std::vector<Trade> t;
        order(b, 1, BUY, 100, 15, t); order(b, 2, BUY, 100, 25, t); order(b, 3, BUY, 100, 5, t);
        assert(modify(b, 1, 10));
        market(b, 4, SELL, 10, t);
        assert(t.size() == 1 && t[0].resting == 1 && t[0].qty == 10);
        assert(depth(b, BUY, 100) == 30);
    }

    // R13: modify increase loses priority
    {
        B b; std::vector<Trade> t;
        order(b, 1, BUY, 100, 15, t); order(b, 2, BUY, 100, 25, t); order(b, 3, BUY, 100, 5, t);
        assert(modify(b, 1, 20));
        market(b, 4, SELL, 35, t);
        assert(t.size() == 3);
        assert(t[0].resting == 2 && t[0].qty == 25);
        assert(t[1].resting == 3 && t[1].qty == 5);
        assert(t[2].resting == 1 && t[2].qty == 5);
        assert(depth(b, BUY, 100) == 15);
    }

    // R14: modify equal qty keeps priority; invalid modifies change nothing
    {
        B b; std::vector<Trade> t;
        order(b, 1, BUY, 100, 10, t); order(b, 2, BUY, 100, 10, t);
        assert(modify(b, 1, 10));
        assert(!modify(b, 1, 0) && !modify(b, 2, -5) && !modify(b, 99, 5));
        assert(depth(b, BUY, 100) == 20);
        market(b, 3, SELL, 10, t);
        assert(t.size() == 1 && t[0].resting == 1);
    }

    // R15: modify a partially filled order, then keep trading
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 30, t);
        market(b, 2, BUY, 10, t);                              // 20 left
        assert(modify(b, 1, 5));
        assert(depth(b, SELL, 101) == 5);
        market(b, 3, BUY, 10, t);
        assert(t.size() == 2 && t[1].qty == 5 && !ask_px(b));
    }

    // R16: modify then cancel, after increase and after reduce
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t); order(b, 2, SELL, 101, 10, t);
        assert(modify(b, 1, 20));
        assert(cancel(b, 1) && depth(b, SELL, 101) == 10);
        assert(modify(b, 2, 4) && cancel(b, 2) && !ask_px(b));
    }

    // R17: duplicate and reused IDs
    {
        B b; std::vector<Trade> t;
        assert(order(b, 1, BUY, 99, 10, t));
        assert(!order(b, 1, BUY, 98, 5, t));                   // live duplicate rejected
        assert(depth(b, BUY, 98) == 0);
        assert(!order(b, 1, SELL, 105, 5, t));                 // duplicate on the OTHER side too
        assert(depth(b, SELL, 105) == 0);
        assert(cancel(b, 1) && order(b, 1, BUY, 97, 5, t));    // reuse after cancel
        order(b, 2, SELL, 97, 5, t);                           // fills order 1
        assert(order(b, 1, BUY, 96, 5, t));                    // reuse after fill
    }

    // R18: zero / negative quantity rejected, nothing rests
    {
        B b; std::vector<Trade> t;
        assert(!order(b, 1, BUY, 99, 0, t) && !order(b, 2, BUY, 99, -5, t));
        assert(!market(b, 3, BUY, 0, t));
        assert(!bid_px(b) && t.empty());
    }

    // R19: after a modify-increase, a new order at the same price goes BEHIND it? No: modified goes to back
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t); order(b, 2, SELL, 101, 10, t);
        assert(modify(b, 1, 15));                              // order 1 now behind 2
        order(b, 3, SELL, 101, 10, t);                         // arrives after the modify: behind 1
        market(b, 4, BUY, 35, t);
        assert(t.size() == 3);
        assert(t[0].resting == 2 && t[1].resting == 1 && t[2].resting == 3);
    }

    // R20: conservation: filled + resting + discarded == submitted
    {
        B b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t); order(b, 2, SELL, 102, 10, t);
        market(b, 3, BUY, 25, t);                              // 5 discarded
        Quantity filled = 0; for (auto& x : t) filled += x.qty;
        Quantity resting = depth(b, SELL, 101) + depth(b, SELL, 102);
        assert(filled == 20 && resting == 0);                  // 20 filled + 0 resting + 5 discarded = 25
    }

    std::cout << name << ": all reference-suite tests passed\n";
}

int main() {
    run_suite<ReferenceBook>("ReferenceBook");
    // /run_suite<OrderBook>("OrderBook");        // the real engine must agree on every case
}