#include "flat_queue.hpp"
#include <thread>
#include <iostream>
using namespace std;
flat_queue test;

bool go = false;
std::mutex mut;
std::condition_variable cond;

constexpr static size_t nthread = 3;
constexpr static size_t npush = 300000;
std::thread threads[nthread];

void pusht(unsigned int start) {
	{
		std::unique_lock<std::mutex> lck(mut);
		cond.wait(lck, [&]() {return go; });
	}
	for (size_t i = 0; i < npush; i++) {
		test.as_push(i + start);
	}
}

int main(int argc, char **argv) {
	test.as_push(1);
	int testi;
	auto rval = test.as_pop(testi);
	unsigned int cur = 0;
	go = false;
	for (auto &st : threads) {
		st = std::thread([cur]() {
			pusht(cur);
		});
		cur++;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	auto ctime = clock();
	go = true;
	cond.notify_all();
	for (auto &st : threads) {
		st.join();
	}
	ctime = clock() - ctime;
	int i;
	cout << "done" << endl;
	cout << "Took " << (ctime * 1.0) / CLOCKS_PER_SEC
		 << " seconds for " << nthread << " threads and "
		 << npush << " elements per thread" << endl;
	cin >> i;
	return 0;
}