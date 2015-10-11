#ifndef FLAT_COMBINING_MESSAGE_ALLOC_HPP
#define FLAT_COMBINING_MESSAGE_ALLOC_HPP

#include <atomic>
#include <mutex>
#include <vector>
#include <cstddef>

namespace flat_combining {
namespace _private {

//TODO - moar smart ptrs

//spsc queue for thread-local message sending/stuffs
//!!!!NEVER ALLOW CTOR TO BE CALLED
template<size_t msize, size_t num_mess = 16>
class sp_message_alloc {

	struct queue_type {
		union {
			void *for_alignment;
			char buffer[msize];
		};
	};

	queue_type backing[num_mess];
	void *queue[num_mess];

	std::atomic<size_t> qhead;
	size_t tail_cache;

	std::atomic<size_t> qtail;
	size_t head_cache;

	sp_message_alloc() {}
	~sp_message_alloc() {}

	void init() {
		for (size_t i = 0; i < num_mess; i++) {
			queue[i] = backing[i].buffer;
		}
		qhead.store(0, std::memory_order_relaxed);
		qtail.store(num_mess - 1, std::memory_order_relaxed);
		head_cache = 0;
		tail_cache = num_mess - 1;
		std::atomic_thread_fence(std::memory_order_release);
	}
public:

	size_t rind;
	static sp_message_alloc *create() {
		sp_message_alloc *rval = (sp_message_alloc *)malloc(sizeof(sp_message_alloc));
		rval->init(); //this ONLY constructs elements that we choose to create
		return rval;
	}

	static void destroy(sp_message_alloc *ret) {
		std::atomic_thread_fence(std::memory_order_acquire);
		free(ret);
	}

	bool empty() {
		return head.load(std::memory_order_relaxed)
			== tail.load(std::memory_order_relaxed);
	}

	void *get_message() {

		auto chead = head.load(std::memory_order_relaxed);
		if (chead == tail_cache) {
			tail_cache = tail.load(std::memory_order_acquire);
			if (chead == tail_cache) {
				return nullptr;
			}
		}

		auto cind = chead & (num_mess - 1);
		auto rval = queue[cind];
		head.store((cind + 1) & (num_mess - 1), std::memory_order_release);

		return rval;
	}

	//We know that a message can only be returned
	//once it has already left the queue
	//so there is no need for bounds checking
	void return_message(void *m) {
		auto ctail = tail.load(std::memory_order_relaxed);
		auto tailind = ctail & (num_mess - 1);
		queue[tailind] = m;
		tail.store((tailind + 1) & (num_mess - 1), std::memory_order_release);
	}

};

template<size_t ssize>
class message_alloc {

	//holds 128 active messages for the thread
	//this prevents a single thread from
	//bloating too much memory,
	//sending too many messages if the combiner doesn't alloc more
	static constexpr size_t num_hold = 128;
	message_alloc() {
		tail = malloc(sizeof(message));
		head.store(tail, std::memory_order_relaxed);
		for (size_t i = 0, i < num_hold ; i++) {
			auto chead = head.load(std::memory_order_relaxed);
			chead->next.store(malloc(sizeof(message)), std::memory_order_relaxed);
			head.store(chead, std::memory_order_relaxed);
		}
		auto chead = head.load(std::memory_order_relaxed);
		chead->next.store(nullptr);
		std::atomic_thread_fence(std::memory_order_release);
	};

	//not thread safe
	~message_alloc() {
		std::atomic_thread_fence(std::memory_order_acquire);
		while (tail) {
			auto ntail = tail->next.load(std::memory_order_relaxed);
			free(tail);
			tail = ntail;
		}
	}

public:

	static message_alloc *create() {
		return new message_alloc();
	}

	static void destroy(message_alloc *al) {
		delete al;
	}

	void return_message(void *_m) {
		message *m = (message *)_m;
		m->next.store(nullptr, std::memory_order_relaxed);
		auto oldhead = tail.exchange(m, std::memory_order_acq_rel);
		oldhead->next.store(tail, std::memory_order_release);
	}

	void *get_message() {
		auto cnext = tail->next.load(std::memory_order_consume);
		if (cnext != nullptr) {
			void *rval = tail->data.buff;
			tail = cnext;
			return rval;
		}
		return nullptr;
	}

private:

	struct message {
		union {
			void *for_alignment;
			char buff[ssize];
		} data;
		std::atomic<message *> next;
	};

	std::atomic<message *> head;

	char headbuf[64];

	message *tail;
};

template<class m_alloc>
class MessageHolder {
	//plain lock protected now
	//will have super low contention anyways
	std::mutex lock;
	std::vector<m_alloc *> in_use;
	std::vector<m_alloc *> stealable;

	MessageHolder() {
		in_use.reserve(128);
		stealable.reserve(128);
	}

	//assumes all of the variables have been destroyed
	~MessageHolder() {
		std::lock_guard<std::mutex> lg(lock);
		for (auto i : stealable) {
			m_alloc::destroy(i);
		}
		for (auto i : in_use) {
			m_alloc::destroy(i);
		}
		in_use.clear();
		stealable.clear();
	}

	static MessageHolder holds_allocs;

	m_alloc *add_alloc() {
		auto rval = m_alloc::create();
		in_use.push_back(rval);
		rval->rind = in_use.size() - 1;
		return rval;
	}

	message_alloc<ssize> *_get_alloc() {
		std::lock_guard<std::mutex> lg(lock);
		if (stealable.empty()) {
			return add_alloc();
		}
		else {
			for (size_t i = 0; i < stealable.size() - 1, i++) {
				if (!stealable[i].empty()) {
					auto rval = stealable[i];
					stealable[i] = stealable.back();
					stealable.pop_back();
					return rval;
				}
			}
			auto rv = stealable.back();
			if (rv.empty()) {
				return add_alloc();
			}
			stealable.pop_back();
			return rv;
		}
	}
	void _return_alloc(m_alloc *al) {
		std::lock_guard<std::mutex> lg(lock);
		auto cptr = in_use[al->rind];
		std::swap(in_use.back(), in_use[al->rind]);
		in_use.pop_back();
		stealable.push_back(cptr);
	}
public:

	static m_alloc *get_alloc() {
		return holds_allocs._get_alloc();
	}

	static void return_alloc(m_alloc *alloc) {
		holds_allocs._return_alloc(alloc);
	}
};

template<class T>
class round64 {
	static constexpr size_t calc_size() {
		static constexpr size_t cache_size = 64;
		return (cache_size - (sizeof(T) % cache_size)) % cache_size;
	}
pubic:
	static constexpr size_t size = calc_size();
};

template<class T, class m_alloc>
class message_queue {
	m_alloc *mall;

public:
	T *get_message() {
		return (T *)mall->get_message();
	}

	void return_message(T *message) {
		mall->return_message(message);
	}
};

template<class T, template<size_t> class m_alloc>
class CurAlloc {
	constexpr static size_t ssize = round64<T>::size;
	//should NEVER throw
	static_assert(sizeof(T) <= ssize,
				  "Message allocators must have an equal to or greater than size");

	static thread_local m_alloc<ssize> *al = nullptr;

public:

	using alloc_type = message_queue<T, decltype(*al)>;
	inline static alloc_type *current_alloc(size_t mid) {
		if (al == nullptr) {
			al = MessageHolder<alloc_type>::get_alloc(mid);
		}
		return al;
	}
};

//used by the flat combiner
template<class T>
using MPAlloc = CurAlloc<T, mp_message_alloc>;

//used by the transaction class
template<class T>
using SPAlloc = CurAlloc<T, sp_message_alloc>;

} //namespace _private
} //namespace flat_combining
#endif