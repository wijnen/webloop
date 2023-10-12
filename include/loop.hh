#ifndef _LOOP_HH
#define _LOOP_HH

#include <set>
#include <list>
#include <vector>
#include <chrono>
#include <poll.h>

class Loop {
public:
	// Types. {{{
	typedef std::chrono::time_point <std::chrono::steady_clock> Time;
	typedef std::chrono::steady_clock::duration Duration;
	typedef bool (*Cb)(void *user_data);

	struct IoRecord { // {{{
		void *user_data;
		int fd;
		short events;
		Cb read;
		Cb write;
		Cb error;
		int handle;
	}; // }}}

	struct IdleRecord {
		Cb cb;
		void *user_data;
	};
	typedef std::list <IdleRecord>::iterator IdleHandle;

	struct TimeoutRecord {
		Time time;
		Duration interval;	// 0 for single shot.
		Cb cb;
		void *user_data;
		bool operator<(TimeoutRecord const &other) const { return time < other.time; }
	};
	typedef std::list <TimeoutRecord>::iterator TimeoutHandle;

	typedef int IoHandle;
	// }}}

private:
	struct PollItems { // {{{
		struct pollfd *data;
		std::vector <IoRecord *> items;
		int num;
		int capacity;
		int min_capacity;
		std::set <int> empty_items;	// Empty items that have lower index than num - 1.
		PollItems(int initial_capacity = 32) : data(new struct pollfd[initial_capacity]), num(0), capacity(initial_capacity), min_capacity(initial_capacity) {}
		int add(IoRecord *item);
		void update(int handle, void *user_data) { items[handle]->user_data = user_data; }
		void remove(int index);
	}; // }}}

	static Loop *default_loop;
	bool running;
	bool aborting;
	std::list <IdleRecord> idle;
	PollItems items;
	std::list <TimeoutRecord> timeouts;

public:
	static Loop *get(Loop *arg = nullptr) { return arg ? arg : default_loop ? default_loop : new Loop(); }
	Loop() : running(false), aborting(false) { if (!default_loop) default_loop = this; }
	Time now() { return std::chrono::steady_clock::now(); }
	int handle_timeouts();
	void iteration(bool block = false);
	void run();
	void stop(bool force = false);

	IoHandle add_io(IoRecord &item) { return items.add(&item); }
	TimeoutHandle add_timeout(TimeoutRecord const &timeout) { timeouts.push_back(timeout); return --timeouts.end(); }
	IdleHandle add_idle(IdleRecord const &record) { idle.push_back(record); return --idle.end(); }

	void update_io(IoHandle handle, void *user_data) { items.update(handle, user_data); }
	void update_timeout(TimeoutHandle handle, void *user_data) { handle->user_data = user_data; }
	void update_idle(IdleHandle handle, void *user_data) { handle->user_data = user_data; }

	void remove_io(IoHandle handle) { items.remove(handle); }
	void remove_timeout(TimeoutHandle handle) { timeouts.erase(handle); }
	void remove_idle(IdleHandle handle) { idle.erase(handle); }
};

#endif

// vim: set foldmethod=marker :
