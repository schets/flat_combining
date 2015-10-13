#pragma once
#include "flat_combining.hpp"
#include <deque>
#include <utility>
#include <mutex>

template<bool dolock = false>
class flat_queue {
	struct lwmut {
		std::atomic<uint64_t> mut;
		void get () {
			for(;;) {
				uint64_t dummy = 0;
				while (mut.load(std::memory_order_relaxed)) {}
				if (mut.compare_exchange_weak(dummy,
											  1,
											  std::memory_order_acquire,
											  std::memory_order_relaxed)) {
					return;
				}
			}
		}
		void release() {
			mut.store(0, std::memory_order_release);
		}
	};
	std::deque<int> value;
	lwmut mut;
public:
	enum class m_type {
		insert,
		remove
	};

	struct rmtype {
		int val;
		bool suc;
	};

	struct message_type {

		union {
			int add;
			rmtype *remove;
		} data;
		m_type mess;
	};

	flat_combining::simple_flat_combining<flat_queue> combiner;

	void handle_message(message_type m) {
		switch (m.mess) {
		case m_type::insert:
			value.push_back(m.data.add);
			break;
		case m_type::remove:
			if (value.empty()) {
				m.data.remove->suc = false;
			}
			else {
				m.data.remove->val = value.front();
				m.data.remove->suc = true;
				value.pop_front();
			}
		}
	}

	flat_queue()
	{
		combiner.cur = this;
	}

	void as_push(int i) {
		if (dolock) {
			mut.get();
			value.push_back(i);
			mut.release();
			return;
		}
		message_type m;
		m.data.add = i;
		m.mess = m_type::insert;
		combiner.send_operation_async(m);
	}

	bool as_pop(int &i) {
		if (dolock) {
			mut.get();
			if (value.empty()) {
				mut.release();
				return false;
			}
			i = value.front();
			value.pop_front();
			mut.release();
			return true;
		}
		rmtype rm;
		message_type m;
		m.data.remove = &rm;
		m.mess = m_type::remove;
		combiner.send_operation(m);
		if (rm.suc) {
			i = rm.val;
			return true;
		}
		else {
			return false;
		}
	}
};
