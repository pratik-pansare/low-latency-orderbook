#include <iostream>
#include "book/ordered_map_book.hpp"

int main() {
    std::cout << "Project Atlas: Limit Order Book & Market Replay Engine\n";
    atlas::book::OrderedMapOrderBook book;
    book.add_order(1, atlas::core::Side::Buy, 100, 10);
    book.add_order(2, atlas::core::Side::Sell, 102, 10);
    std::cout << book.get_depth(5).format() << std::endl;
    return 0;
}
