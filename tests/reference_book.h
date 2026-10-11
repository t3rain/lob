// "tests/reference_book.h"
#include "../inc/order_book.h"
#include <vector>
#include <algorithm>
#include <iostream>

struct RefOrder{ 
    OrderId id; 
    OrderType type; 
    Quantity qty; 
    Price price; 
    uint64_t seq; 
};

class ReferenceBook {

private:

    std::vector<RefOrder> bids_, asks_;
    uint64_t next_seq = 0;

    bool present(OrderId id) const{
        for(const auto& ref: bids_){
            if(ref.id == id){
                return true;
            }
        }
        for(const auto& ref: asks_){
            if(ref.id == id){
                return true;
            }
        }
        return false;
    }

    static bool compare_bids(const RefOrder& r1, const RefOrder& r2){
        if(r1.price == r2.price)    return r1.seq < r2.seq;
        return r1.price > r2.price;
    }

    static bool compare_asks(const RefOrder& r1, const RefOrder& r2){
        if(r1.price == r2.price)    return r1.seq < r2.seq;
        return r1.price < r2.price;
    }

    static bool crossing(OrderType type, Price incoming, Price resting){
        if(type == OrderType::BUY)  return incoming >= resting;
        else                        return incoming <= resting;
    }

public:

    bool add_order(OrderId id, OrderType type, Quantity qty, Price price, std::vector<Trade>& trades){

        if (qty <= 0)   return false;
        if(present(id)) return false;

        std::vector<RefOrder>& side = (type == OrderType::BUY) ? asks_ : bids_;

        while(qty > 0){

            auto idx = type == OrderType::BUY ? best_ask() : best_bid();

            if(!idx)    break;

            else{

                if(!crossing(type, price, side[*idx].price))    break;

                Quantity traded = std::min(qty, side[*idx].qty);
                qty -= traded;
                side[*idx].qty -= traded;

                trades.push_back({side[*idx].price, traded, id, side[*idx].id});

                if(side[*idx].qty == 0) side.erase(side.begin()+*idx);

            }
        }

        if(qty > 0){
            std::vector<RefOrder>& join = type == OrderType::BUY ? bids_ : asks_;
            join.push_back({id, type, qty, price, next_seq++});
        }

        return true;
    }

    bool add_market(OrderId id, OrderType type, Quantity qty, std::vector<Trade>& trades){
        
        if (qty <= 0)   return false;
        if(present(id)) return false;

        std::vector<RefOrder>& side = (type == OrderType::BUY) ? asks_ : bids_;

        while(qty > 0){

            auto idx = type == OrderType::BUY ? best_ask() : best_bid();

            if(!idx)    break;

            else{
                Quantity traded = std::min(qty, side[*idx].qty);
                qty -= traded;
                side[*idx].qty -= traded;

                trades.push_back({side[*idx].price, traded, id, side[*idx].id});

                if(side[*idx].qty == 0) side.erase(side.begin()+*idx);

            }
        }

        return true;
    }

    bool cancel(OrderId id){

        for(size_t i{0}; i<bids_.size(); i++){
            if(bids_[i].id == id){
                bids_.erase(bids_.begin()+i);
                return true;
            }
        }
        for(size_t i{0}; i<asks_.size(); i++){
            if(asks_[i].id == id){
                asks_.erase(asks_.begin()+i);
                return true;
            }
        }
        return false;
    }

    bool modify(OrderId id, Quantity new_qty){

        if(new_qty <= 0)    return false;

        for(size_t i{0}; i<bids_.size(); i++){
            if(bids_[i].id == id){
                if(new_qty > bids_[i].qty)
                    bids_[i].seq = next_seq++;
                bids_[i].qty = new_qty;
                return true;
            }
        }
        for(size_t i{0}; i<asks_.size(); i++){
            if(asks_[i].id == id){
                if(new_qty > asks_[i].qty)
                    asks_[i].seq = next_seq++;
                asks_[i].qty = new_qty;
                return true;
            }
        }
        return false;
    }

    Quantity depth_at(OrderType side, Price p) const{
        Quantity q = 0;
        std::vector<RefOrder> book = (side == OrderType::BUY) ? bids_ : asks_;
        for(const auto& o: book){
            if(o.price == p)   q += o.qty;
        }
        return q;
    }

    std::optional<size_t> best_bid() const{
        auto it = std::min_element(bids_.begin(), bids_.end(), compare_bids);
        if(it == bids_.end())   return std::nullopt;
        return static_cast<size_t>(it-bids_.begin());
    }

    std::optional<size_t> best_ask() const{
        auto it = std::min_element(asks_.begin(), asks_.end(), compare_asks);
        if(it == asks_.end())   return std::nullopt;
        return static_cast<size_t>(it-asks_.begin());
    }

    std::optional<Price> best_bid_price() const {
        auto idx = best_bid();
        if (!idx) return std::nullopt;
        return bids_[*idx].price;
    }
    std::optional<Price> best_ask_price() const {
        auto idx = best_ask();
        if (!idx) return std::nullopt;
        return asks_[*idx].price;
    }
};

