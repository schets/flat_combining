#ifndef FLAT_COMBINING_HPP_
#define FLAT_COMBINING_HPP_

#include <atomic>
#include <thread>

#include <iostream>
using namespace std;

#include <type_traits>
#include <stdexcept>
#include <utility>

#include <stdint.h>
#include <stddef.h>

#include "message_alloc.hpp"

namespace flat_combining {

using namespace _private;

int *mtest;

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

	struct lw_mut {
		std::atomic<size_t> mut;
		bool try_acquire() {
			size_t dummy = 0;
			if (!mut.load(std::memory_order_relaxed)) {
				return mut.compare_exchange_weak(dummy,
											  1,
											  std::memory_order_acquire,
											  std::memory_order_relaxed);
			}
		}

		void release() {
			mut.store(0, std::memory_order_release);
		}
	};

	enum class not_reason {
		empty = 0,
		finished,
		take_over,
	};

	struct message {
		std::atomic<message *> next;
		void *fromwhich;
		std::exception_ptr exc_ptr;
		not_reason reason;
		uint8_t flags;
		mtype mess;
		message(mtype &m) :
			next(0),
			mess(std::move(m)),
			exc_ptr(nullptr),
			reason(not_reason::empty),
			flags(0)
			{}

	};

	using mpal_type = typename MPAlloc<message>::alloc_type;

	constexpr static uint8_t is_mp = 1 << 1;
	constexpr static uint8_t is_stack = 1 << 2;
	constexpr static uint8_t is_malloc = 1 << 3;
	constexpr static uint8_t alloc_mask = is_mp | is_stack | is_malloc;

	struct message;
    buffer _backb;
    std::atomic<message *> qtail;

	buffer _mutex2;
	lw_mut op_mut;

	buffer _waiting;

	std::atomic<char> waiting;

    buffer _bottom;

    std::atomic<size_t> current;

public:

	Derived *cur;


protected:

	bool try_to_message(mtype &m,
						uint16_t num_try) {
		if (op_mut.try_acquire()) {
			cur->handle_message(std::move(m));
			if (num_try > -1) {
				apply_to_messages<false>(-1);
			}
			else {
				apply_to_messages<true>(num_try);
			}
			op_mut.release();
			return true;
		}
		return false;
	}

public:

	simple_flat_combining() :
		qtail(nullptr),
		waiting(0),
		current(0) {
			op_mut.release();
			mtest = (int *)0xffffffff;
		}

	void send_operation(mtype m, int16_t num_try = -1) {
		if (!try_to_message(m, num_try)) {
			size_t n_micros = 0;
			message setm(m);
			set_flag(is_stack, setm.flags);
			send_message(&setm);
			//really shouldn't be hit...
		try_again:
			{
				if (n_micros == 0) {
					//spin
					for (volatile size_t i = 0; i < 10; i++) {}
					n_micros = 2;
				}
				else {
					std::this_thread::sleep_for(std::chrono::microseconds(n_micros));
					n_micros *= 1.3;
					n_micros = std::min(n_micros, (size_t)10);
				}
			}

			switch (setm.reason) {

			case not_reason::finished:
				std::atomic_thread_fence(std::memory_order_acquire);
				if (setm.exc_ptr != nullptr) {
					std::rethrow_exception(setm.exc_ptr);
				}
				break;

			case not_reason::empty:
			case not_reason::take_over:
				if (!try_to_message(setm.mess, num_try)) {
					goto try_again;
				}
			}
		}
	}

	void send_operation_async(mtype m, int16_t num_try = -1) {
		if (!try_to_message(m, num_try)) {
			send_message(get_message(m));
		}
	}

private:

	template<bool check = false>
	inline void _handle_message(message &m) {
		if (check && m.reason == not_reason::finished) {
			return;
		}

		cur->handle_message(std::move(m.mess));
		std::atomic_thread_fence(std::memory_order_release);
		m.reason = not_reason::finished;
		if (m.reason != not_reason::finished) {
			cout << "WFTMATE\n";
		}
	}
	//actual commit and message processing functions
	inline void _do_message(message &m) {
		try {
			_handle_message(m);
		}
		catch (...) {
			std::atomic_thread_fence(std::memory_order_release);
			m.exc_ptr = std::current_exception();
		}
		if (m.reason != not_reason::finished) {
			std::cout << "BAD BUG" << std::endl;
		}
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

	void return_message(message *m) {

		//delegate to the proper message handler
		//multi-producer, on the stack

		switch (m->flags & alloc_mask) {
		case is_mp:
			((mpal_type *)m->fromwhich)->return_message(m);
			break;
		case is_malloc:
			free(m);
		case is_stack:
		default:
			break;
		}
	}

	//allocates a message for
	//use by an asynchronous request
	message *get_message(mtype &m) {
		message *retm;

		auto calloc = MPAlloc<message>::current_alloc();
		retm = (message *)calloc->get_message();

		if (retm) {
			new (retm) message(m);
			retm->fromwhich = calloc;
			set_flag(is_mp, retm->flags);
		}
		else {
			retm = (message *)malloc(sizeof(*retm));
			new (retm) message(m);
			retm->fromwhich = nullptr;
			set_flag(is_malloc, retm->flags);
		}

		return retm;
	}

	void send_message(message *m) {
		if (test_flag(is_mp, m->flags)) {
			if (m->fromwhich == nullptr) {
				cout << "Bad ptr somehow! in send first" << endl;
				cout << *mtest;
			}
		}
		auto ctail = qtail.load(std::memory_order_relaxed);
		do {
			m->next.store(ctail, std::memory_order_relaxed);
		} while (!qtail.compare_exchange_weak(ctail,
											  m,
											  std::memory_order_release,
											  std::memory_order_relaxed));
		if (test_flag(is_mp, m->flags)) {
			if (m->fromwhich == nullptr) {
				cout << "Bad ptr somehow! in send second" << endl;
				cout << *mtest;
			}
		}
	}

	template<bool limited>
	bool apply_to_messages(int16_t l) {

		auto inhere = current.fetch_add(1);

		if (inhere != 0) {
			cout << "many threads " << inhere;
		}
		auto ctail = qtail.exchange(nullptr, std::memory_order_consume);

		if (ctail == nullptr) {
			current.fetch_sub(1);
			return false;
		}

		uint16_t cnum = 0;
		_begin();
		for (;;) {
			auto cur_m = ctail;
			if (test_flag(is_mp, cur_m->flags)) {
				if (cur_m->fromwhich == nullptr) {
					cout << "Bad ptr somehow!" << endl;
				}
			}
			ctail = ctail->next.load(std::memory_order_consume);

			if (limited) {
				++cnum;
			}

			//this wpn't throw! exceptions are stored in the notification
			if (cur_m->reason == not_reason::empty) {
				_do_message(*cur_m);
			}

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
		current.fetch_sub(1);
		return true;
	}
};

} //namespace flat_combining

#endif
