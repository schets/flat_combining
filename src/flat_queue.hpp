#pragma once
#include "flat_combining.hpp"
#include <deque>
#include <queue>
#include <utility>
#include <mutex>
#include <thread>

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
			std::this_thread::sleep_for(std::chrono::nanoseconds(500));
			}
		}
		void release() {
			mut.store(0, std::memory_order_release);
		}
	};
	std::priority_queue<int> value;
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
			value.push(m.data.add);
			break;
		case m_type::remove:
			if (value.empty()) {
				m.data.remove->suc = false;
			}
			else {
				m.data.remove->val = value.top();
				m.data.remove->suc = true;
				value.pop();
			}
		}
	}

	flat_queue()
	{
		combiner.cur = this;
	}

	~flat_queue() {
		cout << "killing!";
	}

	static constexpr int numrun = 10;
	void as_push(int i) {
		if (dolock) {
			mut.get();
			value.push(i);
			mut.release();
			return;
		}
		message_type m;
		m.data.add = i;
		m.mess = m_type::insert;
		combiner.send_operation_async(m, 10);
	}

	bool as_pop(int &i) {
		if (dolock) {
			mut.get();
			if (value.empty()) {
				mut.release();
				return false;
			}
			i = value.top();
			value.pop();
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
