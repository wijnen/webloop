#ifndef _LOOP_HH
#define _LOOP_HH

#include <set>
#include <list>
#include <vector>
#include <chrono>
#include <poll.h>
#include <cassert>

class Loop {
public:
	// Types. {{{
	struct CbBase {};	// Classes that provide callbacks should inherit from this.
	typedef std::chrono::time_point <std::chrono::steady_clock> Time;
	typedef std::chrono::steady_clock::duration Duration;
	typedef bool (CbBase::*Cb)();

	struct IoRecord { // {{{
		CbBase *object;
		int fd;
		short events;
		Cb read;
		Cb write;
		Cb error;
		int handle;
		template <class UserType>
		IoRecord(UserType *obj, int fd, short events, bool (UserType::*r)(), bool (UserType::*w)(), bool (UserType::*e)()) :
			object(reinterpret_cast <CbBase *>(obj)),
			fd(fd),
			events(events),
			read(reinterpret_cast <bool (CbBase::*)()>(r)),
			write(reinterpret_cast <bool (CbBase::*)()>(w)),
			error(reinterpret_cast <bool (CbBase::*)()>(e)),
			handle(-1)
		{}
	}; // }}}

	struct IdleRecord {
		CbBase *object;
		Cb cb;
	};
	typedef std::list <IdleRecord>::iterator IdleHandle;

	struct TimeoutRecord {
		Time time;
		Duration interval;	// 0 for single shot.
		CbBase *object;
		Cb cb;
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
		void update(int handle, CbBase *object) { assert(items[handle]->fd < 0); items[handle]->object = object; }
		void remove(int index);
		std::string print();
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

	void update_io(IoHandle handle, CbBase *object) { items.update(handle, object); }
	// Update is only allowed when the item is not active, and Timeout and Idle do not have a handle when inactive, so they can't be updated.

	void remove_io(IoHandle handle) { items.remove(handle); }
	void remove_timeout(TimeoutHandle handle) { timeouts.erase(handle); }
	void remove_idle(IdleHandle handle) { idle.erase(handle); }
};

#endif

// vim: set foldmethod=marker :
