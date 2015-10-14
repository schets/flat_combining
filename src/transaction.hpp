#ifndef FLAT_COMB_TRANSACTION_HPP
#define FLAT_COMB_TRANSACTION_HPP

#include "utils.hpp"
#include <atomic>

template<class T>
class transaction_list {
	typedef char buffer[128];

	constexpr size_t max_mess = 8;

	struct ptr_holder {
		std::atomic<T *> val;
		char buffer[128 - sizeof(val)];
	};

	buffer _bottom;

	std::atomic<size_t> count;

	buffer _shared;

	size_t maxc;
	ptr_holder *ptrs;

	void add_elem(T *elm) {
		auto curc = count.load(std::memory_order_relaxed);
	}
public:

	T *get_from(size_t rind) {
		auto ccount = count.load(std::memory_order_relaxed);
		act_ind = rin % ccount;

	}
};

class transaction {
public:

	transaction() {
	}

	~transaction() {
	}
};

#endif
