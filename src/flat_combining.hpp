#ifndef FLAT_COMBINING_HPP_
#define FLAT_COMBINING_HPP_

#include <atomic>
#include <mutex>
#include <condition_variable>

#include <type_traits>
#include <utility>

#include <stdint.h>
#include <stddef.h>

namespace flat_combining {
template<class Derived, class Insertable>
class flat_combining {

	static_assert(std::is_base_of<flat_combining, Derived>::value,
				  "Derived class must inherit from flat_combining");

	static_assert(std::is_nothrow_move_assignable<Insertable>::value,
				  "Value type inserted into this container must be nothrow movable");

	static_assert(std::is_nothrow_destructible<Insertable>::value,
				  "Insertion type must not throw in the destructor");

	static_assert(std::is_nothrow_move_assignable<mtype>::value,
				  "Message type must be nothrow movable");

	static_assert(std::is_nothrow_destructible<mtype>::value,
				  "Message type must not throw in the destructor");

protected:

    void commit() {};

	void send_operation(mtype m, Insertable In) {
		if (std::try_lock(op_mut) == -1) {
			try {
				_handle_message(std::move(m),
								std::move(In));
			}
			finally {
				op_mut.unlock();
			}
		}
		else {
			message *m = get_message();
			m->mess = std::move(m);
			m->in = std::move(In);
			send_message(m);
		}
	}

private:

	inline void _handle_message(mtype &&m, Insertable &&in) {
        ((Derived *)this)->handle_message(std::forward(m->mess),
										  std::forward(m->in));
	}
	//actual commit and message processing functions
    inline void _do_message(message *m) {
		try {
			_handle_message(std::move(m->mess), std::move(m->in));
		}
		finally {
			m->mess.~mtype();
			m->in.~Insertable();
		}
    }

    inline void _commit() {
        ((Derived *)this)->commit();
    }

	inline void set_flag(uint8_t flag,
						 uint8_t &flg) {
		flg |= flag;
	}

	inline bool test_flag(uint8_t flag,
						  uint8_t flg) {
		return flag & flg;
	}

	//this will always work! due to how messages work
	//no bounds checking since a message can only be returned
	//after removing it from the queue
	void return_message(message *m) {

		if (test_flag(m->flags_id)) {
			free(m);
			return;
		}

		auto ctail = ntail.load(std::memory_order_relaxed);
		auto cind = ctail & (qsize - 1);
		queue_ptr[cind] = m;
		ntail.store((cind + 1) & (qsize - 1), std::memory_order_release);
	}

	message *get_message() {
		auto chead = nhead.load(std::memory_order_relaxed);
		auto ccache = ntail_cache.load(std::memory_order_relaxed);
		do {
			if (chead == ccache) {
				//reload in case other thread got it
				ccache = ntail_cache.load(std::memory_order_relaxed);

				if (chead == ccache) {

					auto ctail = ntail.load(std::memory_order_acquire);
					ntail_cache.compare_exchange_strong(ccache,
														ctail,
														std::memory_order_relaxed,
														std::memory_order_relaxed);

					//have to allocate a new message
					if (chead == ccache) {
						message *rptr = malloc(sizeof(message));
						set_flag(alloced, rptr->flags_id);
						return rptr;
					}
				}
			}
		}
	}

	void send_message(message *m) {

		m->next.store(nullptr, std::memory_order_relaxed);
		auto oldtail = qtail.exchange(m, std::memory_order_acq_rel);
		oldtail->next.store(m, std::memory_order_relaxed);

	}

	bool apply_to_messages() {
		auto chead = qhead.load(std::memory_order_consume);
		auto nhead = chead->next.load(std::memory_order_consume)

			if (nhead == nullptr) {
				return false;
			}

		//in here, we assume that the message with m->next == nullptr
		//has already been processed - we also don't take responsibility
		//for destroying message values. This way, we process ntail
		//and this queue works just fine!
		do {
			return_message(chead);
			try {
				_do_message(std::move(nhead->mess), std::move(nhead->in));
			}
			catch (...) {
				return_message(nhead);
				throw;
			}
			
			chead = nhead;
		} while ((nhead = chead->next.load(std::memory_order_consume)));

		return true;
	}

    using mtype = Derived::message_enum;
    typedef char buffer[128];
    //this layout is on purpose, please forgive me...
    struct message {
        insertable in;
    private:
        std::atomic<message *> next;
    public:
        mtype mess;
    private:
        uint8_t flags_id;
    };

	constexpr static uint8_t alloced = 1;

    buffer _backb;
    //head of message queue
    std::atomic<message *> qhead;

    buffer _qtail;
    std::atomic<message *> qtail;

    buffer _nhead;
    std::atomic<uint32_t> nhead;
    std::atomic<uint32_t> ntail_cache;

    buffer _ntail;
    std::atomic<uint32_t> ntail;

    buffer _shared;

    message *message_holder;
    message **queue_ptr;
    uint32_t qsize;

	buffer _mutex;
	
	std::mutex mut;
	std::condition_variable cond;

	buffer _mutex2;
	std::mutex op_mut;

    buffer _bottom;

};

} //namespace flat_combining

#endif
