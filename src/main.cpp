#include "flat_queue.hpp"
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
constexpr static size_t npush = 100000 / nthread;
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
	for (size_t i = 0; i < 500; i++) {
		test.as_push(i + start);
	}
	for (size_t i = 0; i < npush; i++) {
		int rval;
		if (rng() % 3) {
			test.as_push(i + start);
		}
		else {
			if (!test.as_pop(rval)) {
				test.as_push(i + start);
			}
		}
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
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	auto runst = std::thread(popt);
	auto ctime = clock();
	go = true;
	cond.notify_all();
	for (auto &st : threads) {
		st.join();
	}
	test.as_push(stopval);
	runst.join();
	ctime = clock() - ctime;
	int i;
	cout << "done" << endl;
	cout << "Took " << (ctime * 1.0) / CLOCKS_PER_SEC
		 << " seconds for " << nthread << " threads and "
		 << npush << " elements per thread" << endl;
	cin >> i;
	return 0;
}
