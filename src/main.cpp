#include "flat_queue.hpp"
#include "utils.hpp"
#include <thread>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <random>
using namespace std;
flat_queue<false> test;

bool go = false;
std::mutex mut;
std::condition_variable cond;

constexpr static size_t nthread = 8;
constexpr static size_t npush = 30000000 / nthread;
int stopval = npush + 1;
std::thread threads[nthread];

void pusht(unsigned int start) {
	mt19937 rng(start);
	cout << "waiting " <<  start << endl;
	{
		std::unique_lock<std::mutex> lck(mut);
		cond.wait(lck, [&]() {return go; });
	}
	cout << "started " << start << endl;
	for (size_t i = 0; i < npush; i++) {
		test.as_push(i + start);
	}
	cout << "finished " << start << endl;
}

void popt() {
	while (true) {
		int dummy;
		if (!test.as_pop(dummy)) {
			std::this_thread::sleep_for(std::chrono::microseconds(10));
		}
		else if (dummy == stopval) {
			cout << "Done popping" << endl;
			return;
		}
	}
}

int main(int argc, char **argv) {
	go = false;
	int cur = 0;
	test.as_push(1);
	test.as_push(1);
	for (auto &st : threads) {
		st = std::thread([cur]() {
			pusht(cur);
		});
		cur++;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	auto runst = std::thread(popt);
	auto cclock = std::chrono::steady_clock::now();
	go = true;
	cond.notify_all();
	for (auto &st : threads) {
		st.join();
	}
	test.as_push(stopval);
	test.as_push(stopval);
	runst.join();
	auto diff = std::chrono::steady_clock::now() - cclock;
	auto td = chrono::duration_cast<chrono::milliseconds>(diff).count() - 5;
	double tdiff = ((double)td / 1000.0);
	auto total_elem = (nthread * npush) * 2; //* 2 since popping them all
	auto elempt = total_elem / tdiff;
	auto elemptpt = elempt / nthread;
	cout << "Took " << tdiff
		 << " seconds for " << nthread << " threads and "
		 << npush << " elements per thread" << endl
		 << "This amounts to " << elemptpt
		 << " operations per thread per second" << endl;
	return 0;
}
