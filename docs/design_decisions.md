# Project Atlas: Data Structure Trade-offs & Design Decisions

## 1. Motivation: The Failure of Naïve STL in Low-Latency Trading

A common initial approach in academic or toy limit order books is:
```cpp
std::map<Price, std::list<Order>, std::greater<Price>> bids_;
std::map<Price, std::list<Order>, std::less<Price>> asks_;
std::unordered_map<OrderId, std::list<Order>::iterator> index_;
```

While functionally correct and intuitive, this design suffers from fatal performance bottlenecks in production HFT environments:
1. **Dynamic Heap Allocations on Hot Path**: Every `std::list::push_back` or `std::map::insert` calls `malloc`/`operator new`, inducing kernel memory management contention, cache pollution, and unpredictable tail latency ($p99 > 150\,\text{ns}$).
2. **Pointer Chasing & Cache Misses**: Red-Black trees (`std::map`) and standard linked lists (`std::list`) store nodes unpredictably across the heap. Traversing levels requires multiple random pointer dereferences, causing severe L1/L2 data cache misses.
3. **Dangling Iterator / Re-entrancy Risk**: Erasing elements from nested containers while modifying price levels creates stale iterators if lookups are not synchronized.
4. **Recalculation Overhead**: Scanning list elements to find aggregate volume or order counts causes $O(N)$ operations during depth snapshotting.

---

## 2. Backend Comparison Matrix

| Architectural Feature | Backend A (`OrderedMapOrderBook`) | Backend B (`OptimizedOrderBook`) |
| :--- | :--- | :--- |
| **Primary Level Container** | `std::map` (Self-balancing RB-tree) | Intrusive Doubly-Linked Active Level Chain |
| **Price Queue Container** | `std::list<Order>` (Heap nodes) | Zero-allocation `IntrusiveList<OrderNode>` |
| **Memory Allocation** | Dynamic per order/node | Pre-allocated `FixedPool<OrderNode>` |
| **Order Lookup Index** | `std::unordered_map<OrderId, iter>` | Pre-reserved index with Generational Handles |
| **Best Bid / Best Ask Lookup** | $O(\log L)$ or $O(1)$ begin iterator | **Strictly $O(1)$** single-pointer dereference |
| **Order Insertion Complexity** | $O(\log L)$ tree insertion + $O(1)$ list push | **$O(1)$** pool alloc + $O(1)$ queue push |
| **Order Cancellation Complexity**| $O(1)$ erase + $O(\log L)$ level erase | **$O(1)$** intrusive unlink + pool dealloc |
| **Hot-Path Allocations** | Multiple heap allocations per op | **Zero** heap allocations |
| **Tail Latency ($p99$)** | $167.0\,\text{ns}$ | **$42.0\,\text{ns}$ (4x reduction)** |

---

## 3. Key Design Decisions in Atlas Optimized Backend

### 3.1 Preallocated Generational Slab Pool (`FixedPool`)
- **Contiguous Memory**: Order nodes are stored in a single contiguous block of memory allocated once at engine startup.
- **Embedded Freelist**: O(1) allocation and deallocation using a LIFO index stack.
- **Generational Handles**: Each handle contains:
  ```cpp
  struct GenerationalHandle {
      uint32_t index;
      uint32_t generation;
  };
  ```
  When an order is cancelled or filled, its slot generation increments. If an outdated handle attempts access, the generation mismatch safely detects the stale lookup, preventing use-after-free and dangling pointer bugs.

### 3.2 Intrusive Doubly Linked Queues (`IntrusiveList`)
- Unlike `std::list`, which allocates a wrapper node (`{ prev, next, value }`) on the heap for every order, `OrderNode` directly embeds:
  ```cpp
  OrderNode* prev{nullptr};
  OrderNode* next{nullptr};
  PriceLevel* level{nullptr};
  ```
- Linking an order into a price level queue is a simple pointer assignment.
- Unlinking an order during cancellation is an $O(1)$ operation without searching the queue or accessing an external container.

### 3.3 Active Price Ladder vs. Dense Array Trade-offs
- **Dense Fixed-Array Ladder**: An array indexed by `price - min_price` provides $O(1)$ direct indexing, but requires huge memory footprints for wide price distributions (e.g. crypto or illiquid equities) and suffers from cache-line pollution over sparse levels.
- **Atlas Active Chain**: Atlas maintains an intrusive doubly linked list of **active non-empty price levels** sorted by price, supplemented by a fast level lookup table.
  - `best_bid_level_` and `best_ask_level_` are always the head of their respective chains ($O(1)$ access).
  - Removing an exhausted price level is an $O(1)$ unlink of the level node.
  - Accommodates arbitrarily sparse and wide price regimes with zero wasted memory.

### 3.4 Lock-Free SPSC Ring Buffer for Event Publishing
- To decouple the matching engine from downstream market data consumers (network gateways, algorithmic execution strategies), Atlas provides a bounded lock-free Single-Producer Single-Consumer (SPSC) ring buffer.
- **Cache-Line Alignment (`alignas(64)`)**: Producer `head_` and consumer `tail_` are isolated onto separate 64-byte cache lines to eliminate false sharing.
- **Memory Ordering**: Producer stores with `std::memory_order_release` and consumer loads with `std::memory_order_acquire`.
