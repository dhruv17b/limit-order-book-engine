#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include "ob/order_book.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: replay <command-file>\n";
        return 1;
    }

    OrderBook book;
    std::ifstream file(argv[1]);
    std::string line;
    uint64_t event_count = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;  // skip blanks/comments

        std::istringstream in(line);
        char kind;
        in >> kind;

        Command cmd{};

        if (kind == 'N') {                       // New order
            uint64_t id, qty;
            char side_c;
            std::string type_s;
            int64_t price;
            in >> id >> side_c >> type_s >> price >> qty;

            Side side = (side_c == 'B') ? Side::Buy : Side::Sell;

            OrderType type = OrderType::Limit;
            if (type_s == "MARKET") type = OrderType::Market;
            else if (type_s == "IOC") type = OrderType::IOC;
            else if (type_s == "FOK") type = OrderType::FOK;
            // else stays Limit

            Order order{id, side, type, price, qty, 0};
            cmd = Command{CommandType::New, order, 0};
        }
        else if (kind == 'C') {                  // Cancel
            uint64_t target_id;
            in >> target_id;
            cmd = Command{CommandType::Cancel, {}, target_id};
        }
        else if (kind == 'M') {                  // Modify
            uint64_t target_id, new_qty;
            int64_t new_price;
            in >> target_id >> new_price >> new_qty;

            Order order{};                        // only price/qty matter for Modify
            order.price = new_price;
            order.quantity = new_qty;
            cmd = Command{CommandType::Modify, order, target_id};
        }
        else {
            continue;                             // unknown line, skip it
        }
        std::vector<Event> events = book.apply(cmd);
        event_count += events.size();
    }

    std::cout << "Replayed file, produced " << event_count << " events.\n";
    return 0;
}