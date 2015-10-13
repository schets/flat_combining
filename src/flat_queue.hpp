#pragma once
#include "flat_combining.hpp"
#include <deque>
#include <utility>

class flat_queue {
	std::deque<int> value;

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
		message_type m;
		m.data.add = i;
		m.mess = m_type::insert;
		combiner.send_operation(m);
	}

	bool as_pop(int &i) {
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
