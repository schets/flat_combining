#ifndef FLAT_COMBINING_HPP_
#define FLAT_COMBINING_HPP_

#include <atomic>
#include <mutex>
#include <condition_variable>

#include <iostream>

#include <type_traits>
#include <stdexcept>
#include <utility>

#include <stdint.h>
#include <stddef.h>

#include "message_alloc.hpp"

namespace flat_combining {

namespace _private {
struct cond_var {
	std::condition_variable cond;
	std::mutex mut;
};
thread_local cond_var ccond;

}
using namespace _private;

template<class T>
class combining_defaults {
public:
	static void commit(T &) {}
	static void begin(T &) {}
};

template<class Derived>
class simple_flat_combining {

	using mtype = typename Derived::message_type;
	//using spal_type = SPAlloc<message>::alloc_type;

	typedef char buffer[128];

	enum class not_reason {
		empty = 0,
		finished,
		take_over,
	};

	struct notification {
		cond_var &cond;
		std::exception_ptr exc_ptr;
		not_reason reason;
		notification() :
			cond(ccond),
			exc_ptr(nullptr),
			reason(not_reason::empty) {}
	};

	struct message {
		std::atomic<message *> next;
		void *fromwhich;
		mtype mess;
		notification* n;
		uint8_t flags;
		message(mtype &m,
				notification *_n) :
			next(0),
			n(_n),
			mess(std::move(m)),
			flags(0) {}

	};

	using mpal_type = typename MPAlloc<message>::alloc_type;

	constexpr static uint8_t is_mp = 1 << 1;
	constexpr static uint8_t is_stack = 1 << 2;
	constexpr static uint8_t is_malloc = 1 << 3;
	constexpr static uint8_t alloc_mask = is_mp | is_stack | is_malloc;

	struct message;
    buffer _backb;
    std::atomic<message *> qtail;

	buffer _mutex;

	std::mutex mut;
	std::condition_variable cond;

	buffer _mutex2;
	std::mutex op_mut;

	buffer _waiting;

	std::atomic<char> waiting;

    buffer _bottom;

public:

	Derived *cur;


protected:

	bool try_to_message(std::unique_lock<std::mutex>& lck,
						mtype &m,
						uint16_t num_try) {
		if (lck.try_lock()) {
			cur->handle_message(std::move(m));
			if (num_try > -1) {
				apply_to_messages<false>(-1);
			}
			else {
				apply_to_messages<true>(num_try);
			}
			return true;
		}
		return false;
	}

public:

	simple_flat_combining() :
		qtail(nullptr),
		waiting(0) {}

	void send_operation(mtype m, int16_t num_try = -1) {
		std::unique_lock<std::mutex> lck(op_mut, std::defer_lock);
		if (!try_to_message(lck, m, num_try)) {
			notification not;
			message setm(m, &not);
			set_flag(is_stack, setm.flags);
			send_message(&setm);
			//really shouldn't be hit...
		try_again:
			{
				std::unique_lock<std::mutex> lk(not.cond.mut);
				not.cond.cond.wait_for(lk, std::chrono::microseconds(10));
			}

			switch (not.reason) {

			case not_reason::finished:
				if (not.exc_ptr != nullptr) {
					std::rethrow_exception(not.exc_ptr);
				}
				break;

			case not_reason::empty:
			case not_reason::take_over:
				if (!try_to_message(lck, setm.mess, num_try)) {
					goto try_again;
				}
			}
		}
	}

private:

	template<bool check = false>
	inline void _handle_message(message &m) {
		if (m.n) {
			if (check && m.n->reason == not_reason::finished) {
				return;
			}
		}
		m.n->reason = not_reason::finished;
		if (m.n->reason != not_reason::finished) {
			cout << "WFTMATE;ASDASDLKJASD\n";
		}
		cur->handle_message(std::move(m.mess));
		if (m.n->reason != not_reason::finished) {
			cout << "WFTMATE\n";
		}
	}
	//actual commit and message processing functions
	inline void _do_message(message &m) {
		try {
			_handle_message(m);
		}
		catch (...) {
			if (m.n) {
				m.n->exc_ptr = std::current_exception();
			}
		}
		if (m.n->reason != not_reason::finished) {
			std::cout << "BAD BUG" << std::endl;
		}
		signal_message(m);
	}

	inline void _commit() {
		combining_defaults<Derived>::commit(*cur);
	}

	inline void _begin() {
		combining_defaults<Derived>::begin(*cur);
	}

	inline void set_flag(uint8_t flag,
						 uint8_t &flg) {
		flg |= flag;
	}

	inline bool test_flag(uint8_t flag,
						  uint8_t flg) {
		return flag & flg;
	}

	void signal_message(message &m) {
		if (!m.n) {
			return;
		}
		std::unique_lock<std::mutex> lck(m.n->cond.mut);
		m.n->cond.cond.notify_one();
	};

	void return_message(message *m) {

		//delegate to the proper message handler
		//multi-producer, on the stack

		switch (m->flags & alloc_mask) {
		case is_mp:
			((mpal_type *)m->fromwhich)->return_message(m);
			break;
		case is_malloc:
			free(m);
			break;
		case is_stack:
		default:
			break;
		}
	}

	//allocates a message for
	//use by an asynchronous request
	message *get_message() {
		message *retm;

		auto calloc = MPAlloc<message>::current_alloc();
		retm = calloc->get_message();

		if (retm) {
			retm->fromwhich = calloc;
			set_flag(is_mp, retm->flags);
		}
		else {
			retm = malloc(sizeof(*retm));
			retm->fromwhich = nullptr;
			set_flag(is_malloc, retm->flags);
		}

		return retm;
	}

	void send_message(message *m) {

		auto ctail = qtail.load(std::memory_order_relaxed);
		do {
			m->next.store(ctail, std::memory_order_relaxed);
		} while (!qtail.compare_exchange_weak(ctail,
											  m,
											  std::memory_order_release,
											  std::memory_order_relaxed));
	}

	template<bool limited>
	bool apply_to_messages(int16_t l) {

		auto ctail = qtail.exchange(nullptr, std::memory_order_consume);

		if (ctail == nullptr) {
			return false;
		}

		uint16_t cnum = 0;
		_begin();
		for (;;) {
			auto cur_m = ctail;
			ctail = ctail->next.load(std::memory_order_consume);

			if (limited) {
				++cnum;
			}

			//this wpn't throw! exceptions are stored in the notification
			_do_message(*cur_m);

			return_message(cur_m);

			if (limited && (cnum >= l)) {
				break;
			}

			if (ctail == nullptr) {
				if (qtail.load(std::memory_order_relaxed) != nullptr) {
					ctail = qtail.exchange(nullptr, std::memory_order_consume);
				}
				else {
					break;
				}
			}
		};

		_commit();
		return true;
	}
};

} //namespace flat_combining

#endif
