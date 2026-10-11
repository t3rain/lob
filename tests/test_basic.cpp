// "tests/test_basic.cpp"

#include <cassert>
#include <iostream>
#include "../inc/order_book.h"

const auto BUY = OrderType::BUY;
const auto SELL = OrderType::SELL;

// every call goes through these, so the invariants are checked after EVERY operation
static bool order(OrderBook& b, OrderId id, OrderType t, Price p, Quantity q, std::vector<Trade>& tr) {
    bool r = b.add_order(id, t, q, p, tr);
    assert(b.check_invariants());
    return r;
}
static bool market(OrderBook& b, OrderId id, OrderType t, Quantity q, std::vector<Trade>& tr) {
    bool r = b.add_market(id, t, q, tr);
    assert(b.check_invariants());
    return r;
}
static bool cancel(OrderBook& b, OrderId id) {
    bool r = b.cancel(id);
    assert(b.check_invariants());
    return r;
}
static bool modify(OrderBook& b, OrderId id, Quantity q) {
    bool r = b.modify(id, q);
    assert(b.check_invariants());
    return r;
}
static void print_trades(std::vector<Trade>& trade){

    for(const auto& t: trade){
        std::cout<<"Price: "<<t.price<<", Quantity: "<<t.qty<<", Incoming: "<<t.incoming<<", Resting: "<<t.resting<<"\n";
    }
    return;
}


int main() {
    // S1: resting orders on both sides, several levels, no trades
    {
        OrderBook b; std::vector<Trade> t;
        order(b, 1, BUY, 99, 10, t);  order(b, 2, BUY, 99, 20, t);  order(b, 3, BUY, 98, 30, t);
        order(b, 4, SELL, 101, 15, t); order(b, 5, SELL, 101, 5, t); order(b, 6, SELL, 102, 25, t);
        print_trades(t);
        assert(t.empty());
        assert(b.depth_at(BUY, 99) == 30 && b.depth_at(BUY, 98) == 30);
        assert(b.depth_at(SELL, 101) == 20 && b.depth_at(SELL, 102) == 25);
    }

    // S2: partial fill of a resting order (maker survives with reduced qty)
    {
        OrderBook b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 30, t);
        market(b, 2, BUY, 10, t);
        assert(t.size() == 1 && t[0].qty == 10);
        assert(b.depth_at(SELL, 101) == 20);
    }

    // S3: full fill of makers, including a sweep that erases whole levels
    {
        OrderBook b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t); order(b, 2, SELL, 102, 10, t); order(b, 3, SELL, 103, 10, t);
        market(b, 4, BUY, 25, t);
        assert(t.size() == 3);
        assert(b.depth_at(SELL, 101) == 0 && b.depth_at(SELL, 102) == 0 && b.depth_at(SELL, 103) == 5);
        assert(!cancel(b, 1) && !cancel(b, 2));        // filled makers are gone from the index
    }

    // S4: order order that crosses, partially fills, and rests its remainder
    {
        OrderBook b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 30, t);
        order(b, 2, BUY, 102, 100, t);
        assert(t.size() == 1 && t[0].price == 101 && t[0].qty == 30);
        assert(b.depth_at(BUY, 102) == 70);
        assert(cancel(b, 2));                           // the REMAINDER must be in the index
    }

    // S5: cancel front, middle, back, and the only order at a level
    {
        OrderBook b; std::vector<Trade> t;
        order(b, 1, BUY, 99, 10, t); order(b, 2, BUY, 99, 10, t);
        order(b, 3, BUY, 99, 10, t); order(b, 4, BUY, 98, 10, t);
        assert(cancel(b, 2));                           // middle
        assert(cancel(b, 1));                           // front
        assert(cancel(b, 3));                           // back (now the only one at 99)
        assert(b.depth_at(BUY, 99) == 0);               // level erased
        assert(*b.best_bid() == 98);
        assert(cancel(b, 4));
        assert(!b.best_bid());
        assert(!cancel(b, 4));
    }

    // S6: modify reduce, then cancel
    {
        OrderBook b; std::vector<Trade> t;
        order(b, 1, BUY, 100, 15, t); order(b, 2, BUY, 100, 25, t);
        assert(modify(b, 1, 10));
        assert(b.depth_at(BUY, 100) == 35);
        assert(cancel(b, 1));
        assert(b.depth_at(BUY, 100) == 25);
    }

    // S7: modify increase (moves to back), then cancel
    {
        OrderBook b; std::vector<Trade> t;
        order(b, 1, BUY, 100, 15, t); order(b, 2, BUY, 100, 25, t); order(b, 3, BUY, 100, 5, t);
        assert(modify(b, 1, 20));
        assert(b.depth_at(BUY, 100) == 50);
        assert(cancel(b, 1));                           // uses the REWRITTEN iterator
        assert(b.depth_at(BUY, 100) == 30);
    }

    // S8: modify increase, then the moved order gets filled by the matching loop
    {
        OrderBook b; std::vector<Trade> t;
        order(b, 1, SELL, 101, 10, t); order(b, 2, SELL, 101, 10, t);
        assert(modify(b, 1, 20));                       // 1 now behind 2
        market(b, 3, BUY, 30, t);
        assert(t.size() == 2);
        assert(t[0].resting == 2 && t[1].resting == 1 && t[1].qty == 20);
        assert(!b.best_ask());
        assert(!cancel(b, 1) && !modify(b, 2, 5));
    }

    // S9: ID reuse after cancel and after a full fill
    {
        OrderBook b; std::vector<Trade> t;
        order(b, 1, BUY, 99, 10, t);
        assert(cancel(b, 1));
        assert(order(b, 1, BUY, 98, 10, t));            // reuse after cancel
        order(b, 2, SELL, 98, 10, t);                   // fully fills order 1
        assert(t.size() == 1 && t[0].resting == 1);
        assert(order(b, 1, BUY, 97, 5, t));             // reuse after fill
        assert(!order(b, 1, BUY, 96, 5, t));            // but a LIVE id is rejected
    }

    std::cout << "invariant suite passed\n";
    return 0;
}