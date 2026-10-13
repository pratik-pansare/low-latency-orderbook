#!/usr/bin/env python3
"""
Synthetic workload generator for Atlas Limit Order Book.
Generates reproducible sequenced market events (ADD, CANCEL, MODIFY) in CSV format.
"""

import sys
import argparse
import random

def generate_workload(filename: str, count: int = 50000, seed: int = 42, mid_price: int = 10000):
    random.seed(seed)
    
    active_orders = []
    next_order_id = 1
    seq = 1
    timestamp_ns = 1_700_000_000_000_000_000

    with open(filename, "w") as f:
        f.write("#seq,timestamp,action,order_id,side,price,qty,tif\n")
        
        for _ in range(count):
            timestamp_ns += random.randint(100, 2000)
            roll = random.random()

            if roll < 0.60 or not active_orders:
                # ADD
                order_id = next_order_id
                next_order_id += 1
                side = "BUY" if random.random() < 0.5 else "SELL"
                px = max(1, int(round(random.gauss(mid_price, 15.0))))
                qty = random.randint(1, 100)
                tif = "FAK" if random.random() < 0.10 else "GTC"

                f.write(f"{seq},{timestamp_ns},ADD,{order_id},{side},{px},{qty},{tif}\n")
                seq += 1
                if tif == "GTC":
                    active_orders.append(order_id)
            elif roll < 0.85:
                # CANCEL
                idx = random.randint(0, len(active_orders) - 1)
                order_id = active_orders.pop(idx)
                f.write(f"{seq},{timestamp_ns},CANCEL,{order_id},BUY,0,0,GTC\n")
                seq += 1
            else:
                # MODIFY
                idx = random.randint(0, len(active_orders) - 1)
                order_id = active_orders[idx]
                px = max(1, int(round(random.gauss(mid_price, 15.0))))
                qty = random.randint(1, 100)
                f.write(f"{seq},{timestamp_ns},MODIFY,{order_id},BUY,{px},{qty},{GTC}\n")
                seq += 1

    print(f"Generated {count} events -> {filename} (seed={seed})")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate synthetic LOB event logs")
    parser.add_argument("--output", "-o", default="workload.csv", help="Output CSV path")
    parser.add_argument("--count", "-n", type=int, default=50000, help="Number of events")
    parser.add_argument("--seed", "-s", type=int, default=42, help="Random seed")
    args = parser.parse_args()
    generate_workload(args.output, args.count, args.seed)
