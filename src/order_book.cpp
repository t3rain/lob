// "src/order_book.cpp"

#include "order_book.h"
#include <iostream>
#include <cassert>

namespace {

template <typename BookSide, typename Crosses>
    void match(BookSide& target, Order& incoming, Crosses crossfn, 
        std::vector<Trade>& trades, std::unordered_map<OrderId, Locator>& finder){

        while(incoming.qty > 0 && !target.empty()){

            auto it = target.begin();
            Price resting_price = it->first;
            if(!crossfn(resting_price)) break;

            Level& lvl = it->second;
            while(incoming.qty > 0 && !lvl.orders.empty()){

                Order& resting = lvl.orders.front();

                Quantity traded = std::min(incoming.qty, resting.qty);
                trades.push_back({resting_price, traded, incoming.id, resting.id});

                resting.qty -= traded;
                incoming.qty -= traded;
                lvl.total_qty -= traded;

                if(resting.qty == 0){
                    finder.erase(resting.id);
                    lvl.orders.pop_front();
                }
            }

            if(lvl.orders.empty())  target.erase(it);
        }
    }

    template <typename BookSide>
    bool remove(BookSide& target, Locator& loc){

        Level& lvl = target[loc.price];
        Quantity q = loc.it->qty;
        lvl.orders.erase(loc.it);
        lvl.total_qty -= std::min(lvl.total_qty, q);
        if(lvl.total_qty == 0)  target.erase(loc.price);
        return true;

    }

    template <typename BookSide>
    Quantity depth(const BookSide& book, Price price){

        auto it = book.find(price);
        return it == book.end() ? 0 : it->second.total_qty;
    }

    template <typename BookSide>
    Level& find_level(BookSide& book, Price price){
        auto lvl_it = book.find(price);
        assert(lvl_it != book.end());
        return lvl_it->second;
    }

    template <typename BookSide>
    bool check_invariant_per_side(BookSide& book, OrderType type, 
        const std::unordered_map<OrderId, Locator>& finder, uint64_t& tot_ids){

        bool retval = true;

        for(const auto& [price, lvl]: book){
            if(lvl.orders.empty()){
                std::cout<<"Level empty\n";
                retval = retval & false;
            }

            Quantity tqty = 0;
            auto it = lvl.orders.begin();
            while(it != lvl.orders.end()){

                if(it->qty <= 0){
                    std::cout<<"Non-positive quantity\n";
                    retval = retval & false;
                }

                if(it->type != type || it->price != price){
                    std::cout<<"Order type mismatch\n";
                    retval = retval & false;
                }

                auto f_it = finder.find(it->id);
                if(f_it == finder.end() || f_it->second.type != type || f_it->second.price != price || f_it->second.it != it){
                    std::cout<<"Finder map invalid - entry not present\n";
                    retval = retval & false;
                }

                tqty += it->qty;
                tot_ids++;

                ++it;
            }
            
            if(tqty != lvl.total_qty){
                std::cout<<"Total quantity of level inconsistent\n";
                retval = retval & false;
            }
        }

        return retval;
    }
}


bool OrderBook::add_order(OrderId id, OrderType type, Quantity qty, Price price, std::vector<Trade>& trades){

    if(finder_.find(id) != finder_.end())   return false;
    if (qty <= 0)   return false;

    Order ord{id, type, qty, price};
    if (type == OrderType::BUY) 
        match(asks_, ord, [price](Price p){ return p <= price; }, trades, finder_);
    else                        
        match(bids_, ord, [price](Price p){ return p >= price; }, trades, finder_);


    if(ord.qty > 0){
        
        Level& lvl = (type == OrderType::BUY) ? bids_[price] : asks_[price]; 
        lvl.orders.push_back(ord);
        lvl.total_qty += ord.qty;

        Locator loc{type, price, std::prev(lvl.orders.end())};
        finder_[id] = loc;

    }

    return true;

}

bool OrderBook::add_market(OrderId id, OrderType type, Quantity qty, std::vector<Trade>& trades){

    if(finder_.find(id) != finder_.end())   return false;
    if(qty<=0)  return false;

    Order ord{id, type, qty, 0};

    if(type == OrderType::BUY)
        match(asks_, ord, [](Price){ return true; }, trades, finder_);
    else    
        match(bids_, ord, [](Price){ return true; }, trades, finder_);

    return true;
}

bool OrderBook::cancel(OrderId id){

    if(finder_.find(id) == finder_.end())    return false;
    OrderType type = finder_[id].type;
    bool cancelled = type == OrderType::BUY ? remove(bids_, finder_[id]) : remove(asks_, finder_[id]);
    if(cancelled){
        finder_.erase(id);
        return true;
    }
    return false;
}

bool OrderBook::modify(OrderId id, Quantity new_qty){

    if(new_qty <= 0)   return false;

    auto it = finder_.find(id);
    if(it == finder_.end()) return false;

    Locator& loc = it->second;
    if(loc.it->qty >= new_qty){
        Level& lvl = (loc.type == OrderType::BUY) ? find_level(bids_, loc.price) : find_level(asks_, loc.price);
        lvl.total_qty += (new_qty - loc.it->qty);
        loc.it->qty = new_qty;
    }
    else{
        Level& lvl = (loc.type == OrderType::BUY) ? bids_[loc.price] : asks_[loc.price];
        Order new_ord{loc.it->id, loc.it->type, new_qty, loc.it->price};

        lvl.total_qty += (new_qty - loc.it->qty);

        lvl.orders.erase(loc.it);
        lvl.orders.push_back(new_ord);

        // List splicing can be used instead of deletion and then insertion
        // Uncomment and fix it when testing optimizations
        // lvl.orders.splice(lvl.orders.end(), lvl.orders, loc.it);   // move node to the back
        // loc.it->qty = new_qty;                                      // same iterator, now last
        // lvl.total_qty += diff;
        
        loc.it = std::prev(lvl.orders.end());
    }

    return true;
}

std::optional<Price> OrderBook::best_bid() const{

    if(bids_.empty()) return std::nullopt;
    else return bids_.begin()->first;

}

std::optional<Price> OrderBook::best_ask() const{

    if(asks_.empty())   return std::nullopt;
    else return asks_.begin()->first;
}

Quantity OrderBook::depth_at(OrderType side, Price p) const{
    return (side == OrderType::BUY) ? depth(bids_, p) : depth(asks_, p);
}

bool OrderBook::check_invariants() const{

    bool valid = true;

    // inv 1: no crossed books
    if(!bids_.empty() && !asks_.empty() && bids_.begin()->first >= asks_.begin()->first){
        std::cout<<"Crossed book check failed\n";
        valid = valid & false;
    }

    // inv 2: sweep each side and check consistency
    uint64_t ids = 0;
    valid  = valid & check_invariant_per_side(bids_, OrderType::BUY, finder_, ids);
    valid  = valid & check_invariant_per_side(asks_, OrderType::SELL, finder_, ids);
    if(ids != finder_.size()){
        std::cout<<"Finder map invalid - idx does not match number of resting orders\n";
        valid = valid & false;
    }

    return valid;
}