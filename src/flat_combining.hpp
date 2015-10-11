#ifndef FLAT_COMBINING_HPP_
#define FLAT_COMBINING_HPP_

#include <atomic>
#include <mutex>
#include <condition_variable>

#include <type_traits>
#include <utility>

#include <stdint.h>
#include <stddef.h>

#include "message_alloc.hpp"

namespace flat_combining {
using namespace _private;

template<class Derived>
class simple_flat_combining {

	static_assert(std::is_base_of<simple_flat_combining, Derived>::value,
				  "Derived class must inherit from flat_combining");
	static_assert(std::is_nothrow_move_assignable<mtype>::value,
				  "Message type must be nothrow movable");

	static_assert(std::is_nothrow_destructible<mtype>::value,
				  "Message type must not throw in the destructor");

protected:

    void commit() {};
    void begin() {};

	void send_operation(mtype m, int16_t num_try=-1) {
		if (std::try_lock(op_mut) == -1) {
			try {
				_handle_message(std::move(m));
			}
			finally {
				op_mut.unlock();
			}
			if (num_try > -1) {
				apply_to_messages<false>(-1);
			}
			else {
				apply_to_messages<true>(num_try);
			}
		}
		else {
			message *m = get_message();
			try {
				m->mess = std::move(m);
				send_message(m);
			}
			finally {
				return_message(m);
			}
		}
	}

private:

	inline void _handle_message(mtype &&m) {
        ((Derived *)this)->handle_message(std::forward(m),
										  std::forward(in));
	}
	//actual commit and message processing functions
    inline void _do_message(message *m) {
		try {
			_handle_message(std::move(m->mess));
		}
		finally {
			m->mess.~mtype();
		}
    }

    inline void _commit() {
        ((Derived *)this)->commit();
    }

    inline void _begin() {
        ((Derived *)this)->begin();
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

		if (m->fromwhich) {
			if (m->fromwhich & is_mp) {
				mpal_type *from = (mpal_type *)(m->fromwhich & (~is_mp));
				from->return_message(m);
			}
			else {
				spal_type *from = (spal_type *)m->fromwhich;
				from->return_message(m);
			}
		}
		else {
			free(m);
		}
	}

	template<bool use_mp>
	message *get_message() {
		message *retm;
		uintptr_t calloc;

		if (use_mp) {
			auto _calloc = MPAlloc<message>::current_alloc();
			retm = calloc->get_message();
			calloc = (uintptr_t)_calloc;
			calloc |= is_mp;
		}
		else {
			auto _calloc = SPAlloc<message>::current_alloc();
			retm = calloc->get_message();
			calloc = (uintptr_t)_calloc;
		}


		if (retm) {
			retm->from_which = calloc;
		}
		else {
			retm = malloc(sizeof(*retm));
			retm->fromwhich = 0;
		}

		return retm;
	}

	void send_message(message *m) {

		m->next.store(nullptr, std::memory_order_relaxed);

		//release to synchronize with other producer
		//acquire to synchronize with oldtail->next write
		auto oldtail = qtail.exchange(m, std::memory_order_acq_rel);

		//release to synchronize with consumer
		oldtail->next.store(m, std::memory_order_release);
	}

    template<bool limited>
	bool apply_to_messages(int16_t l) {

		auto chead = qhead.load(std::memory_order_relaxed);
		auto nhead = chead->next.load(std::memory_order_consume);

        if (nhead == nullptr) {
            return false;
        }

        uint16_t cnum = 0;
        _begin();
		//in here, we assume that the message with m->next == nullptr
		//has already been processed - we also don't take responsibility
		//for destroying message values. This way, we process ntail
		//and this queue works just fine!
		do {

			return_message(chead);
            if (limited) {
                ++cnum;
            }

			try {
				_do_message(std::move(nhead->mess));
			}
			catch (...) {
				//move on with the messages
				qhead.store(nhead, std::memory_order_relaxed);
				throw;
			}

			chead = nhead;

            if (limited && (cnum >= l)) {
                break;
            }

		} while ((nhead = chead->next.load(std::memory_order_consume)));

        _commit();
        qhead.store(chead, std::memory_order_relaxed);
		return true;
	}

    using mtype = Derived::message_enum;
	using mpal_type = MPAlloc<message>::alloc_type;
	using spal_type = SPAlloc<message>::alloc_type;

    typedef char buffer[128];

    struct message {
        std::atomic<message *> next;
		uintptr_t fromwhich;
        mtype mess;
    };

	constexpr static uintptr_t is_mp = 1;

    buffer _backb;
    //head of message queue
    std::atomic<message *> qhead;

    buffer _qtail;
    std::atomic<message *> qtail;

	buffer _mutex;

	std::mutex mut;
	std::condition_variable cond;

	buffer _mutex2;
	std::mutex op_mut;

    buffer _bottom;

};

} //namespace flat_combining

#endif
