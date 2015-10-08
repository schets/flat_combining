#ifndef FLAT_COMBINING_HPP_
#define FLAT_COMBINING_HPP_

#include <atomic>
#include <stdint.h>
#include <stddef.h>

namespace flat_combining {
template<class Derived, class Insertable>
class flat_combining {


protected:

    void commit() {};

private:

    inline void _do_message(mtype&& m, Insertable&& i) {
        ((Derived *)this)->handle_message(std::move(m), std::move(i));
    }

    inline void _commit() {
        ((Derived *)this)->commit();
    }

    using mtype = Derived::message_enum;
    typedef char buffer[128];
    //this layout is on purpose, please forgive me...
    struct _message {
        insertable in;
    private:
        std::atomic<message *> next;
    public:
        mtype mess;
    private:
        std::atomic<uint8_t> isdone;
    };

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

    buffer _bottom;

    //this will always work! due to how messages work
    //no bounds checking since a message can only be returned
    //after removing it from the queue
    void return_message(message *m) {
        auto ctail = ntail.load(std::memory_order_relaxed);
        auto cind = ctail & (qsize - 1);
        queue_ptr[cind] = m;
        ntail.store((cind + 1) & (qsize - 1), std::memory_order_release);
    }

    void get_message(message *m) {
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
            _do_message(std::move(nhead->mess), std::move(nhead->in));
            nhead->mess.~mtype();
            nhead->in.~Insertable;
            chead = nhead;
        } while ((nhead = chead->next.load(std::memory_order_consume)));

        return true;
    }

};

} //namespace flat_combining

#endif
