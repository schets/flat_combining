#ifndef FLAT_COMBINING_UTILS_HPP
#define FLAT_COMBINING_UTILS_HPP

#ifdef __INTEL_COMPILER
//icc 13 outputs a function call for signal fence...
//lol
#define FLAT_COMB_CONSUME_FENCE asm volatile("" ::: "memory");
#else
#include <atomic>
//acquire fences prevent the reordering of future reads
//with previous reads/writes - this works great,
//since we want to keep x->val from being compiler reordered with
//a prior x = load(...). This is valid on x64, powerpc, arm,
//anything not the dec alpha
#define FLAT_COMB_CONSUME_FENCE std::atomic_signal_fence(std::memory_order_acquire);
#endif

#endif