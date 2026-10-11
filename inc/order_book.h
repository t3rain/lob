// "inc/order_book.h"
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <list>
#include <vector>
#include<unordered_map>
#include <ostream>

using OrderId = uint64_t;
using Quantity = int64_t;
using Price = int64_t;

enum class OrderType {
    BUY,
    SELL
};

inline const char* to_str(OrderType type){
    switch(type){
        case OrderType::BUY  : return "BUY";
        case OrderType::SELL : return "SELL";
    }
    return "?";
}

inline std::ostream& operator<<(std::ostream& os, OrderType type){
    return os << to_str(type);
}

struct Order {
    OrderId id;
    OrderType type;
    Quantity qty;
    Price price;
};

struct Level {
    std::list<Order> orders;
    Quantity total_qty = 0;
};

struct Trade {
    Price price;
    Quantity qty;
    OrderId incoming;
    OrderId resting;
};

struct Locator{
    OrderType type;
    Price price;
    std::list<Order>::iterator it;
};

class OrderBook {

public:

    /*
    Adds the order to the specified side
    Returns: FALSE, if the orderId is a duplicate or qty is 0
            TRUE otherwise
    */
    bool add_order(OrderId id, OrderType type, Quantity qty, Price price, std::vector<Trade>& trades);

    /*
    Sweeps the given qty on the other side
    This order never rests, so if the qty is more than
    the total qty on the target side, it will simply
    sweep the entire book and then expire
    Returns: FALSE, if the orderId is a duplicate or qty is 0
            TRUE, otherwise
    NOTE: Since this never rests, market orders can have duplicate IDs of other market orders.
    The only criteria is that it should not have an ID of an active order.
    */
    bool add_market(OrderId id, OrderType type, Quantity qty, std::vector<Trade>& trades);


    // returns the best bid and best ask
    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    /*
    Cancel a resting order by OrderID
    Returns: TRUE, if order was cancelled
            FALSE, if order does not exist
    */
    bool cancel(OrderId id);

    /*
    Modify an existing order by changing its quantity
    In case the quantity is to be increased, the current order is
    removed and a new order with the new quantity is inserted to 
    the end of the given level
    In case it is to be reduced, reduce in place
    Returns: TRUE, if order was modified
            FALSE, if order does not exist or new quantity is non-positive
    NOTE: It modifies the remaining resting order quantity, not the total
    that was queued first.
    */
   bool modify(OrderId id, Quantity new_qty);

    // qty at price p
    Quantity depth_at(OrderType side, Price p)  const;

    // utilities for test
    void display_book() const;
    void display_map() const;
    void display_level(const Level& lvl) const;
    bool check_invariants() const;

private:

    // the bid and ask side books
    std::map<Price, Level, std::greater<Price>> bids_;
    std::map<Price, Level> asks_;

    // OrderID mapper for O(1) lookup for cancel
    std::unordered_map<OrderId, Locator> finder_;

};