#ifndef _LOOP_HH
#define _LOOP_HH

#include <iostream>
#include <set>
#include <list>
#include <vector>
#include <chrono>
#include <poll.h>
#include <cassert>

namespace Webloop {

class Loop {
public:
	// Types. {{{
	struct CbBase {};	// Classes that provide callbacks should inherit from this.
	typedef std::chrono::time_point <std::chrono::steady_clock> Time;
	typedef std::chrono::steady_clock::duration Duration;
	typedef bool (CbBase::*Cb)();

	struct IoRecord { // {{{
		typedef bool (CbBase::*CbType)();
		CbBase *object;
		int fd;
		short events;
		Cb read;
		Cb write;
		Cb error;
		std::string name;	// For debugging.
		template <class UserType>
		IoRecord(std::string const &name, UserType *obj, int fd, short events, bool (UserType::*r)(), bool (UserType::*w)(), bool (UserType::*e)()) :
			object(reinterpret_cast <CbBase *>(obj)),
			fd(fd),
			events(events),
			read(reinterpret_cast <CbType>(r)),
			write(reinterpret_cast <CbType>(w)),
			error(reinterpret_cast <CbType>(e)),
			name(name)
		{}
		void set_name(std::string const &n) { name = n; }
	}; // }}}

	struct IdleRecord { // {{{
		CbBase *object;
		Cb cb;
		// Allow initializing this struct from any base class, as long as the cb matches the object.
		template <class Base> IdleRecord(Base *object, bool (Base::*cb)()) : object(reinterpret_cast <CbBase *>(object)), cb(reinterpret_cast <Cb>(cb)) {}
	}; // }}}
	typedef std::list <IdleRecord>::iterator IdleHandle;

	struct TimeoutRecord { // {{{
		Time time;
		Duration interval;	// 0 for single shot.
		CbBase *object;
		Cb cb;
		bool operator<(TimeoutRecord const &other) const { return time < other.time; }
		template <class Base> TimeoutRecord(Time const &time, Duration interval, Base *object, bool (Base::*cb)()) : time(time), interval(interval), object(reinterpret_cast <CbBase *>(object)), cb(reinterpret_cast <Cb>(cb)) {}
	}; // }}}
	typedef std::set <TimeoutRecord>::iterator TimeoutHandle;

	typedef int IoHandle;
	// }}}

private:
	struct PollItems { // {{{
		struct pollfd *data;
		std::vector <IoRecord> items;
		int num;
		int capacity;
		int min_capacity;
		std::set <int> empty_items;	// Empty items that have lower index than num - 1.
		PollItems(int initial_capacity = 32) : data(new struct pollfd[initial_capacity]), num(0), capacity(initial_capacity), min_capacity(initial_capacity) {}
		~PollItems() { delete[] data; }
		int add(IoRecord const &item);
		void remove(int index);
		std::string print();
		void update_name(IoHandle handle, std::string const &n) { items[handle].set_name(n); }
	}; // }}}

	static Loop fallback_loop;
	static Loop *default_loop;
	bool running;
	bool aborting;
	std::list <IdleRecord> idle;
	std::list <IdleRecord>::iterator next_idle_item;
	PollItems items;
	std::set <TimeoutRecord> timeouts;

public:
	static Loop *get(Loop *arg = nullptr);
	Loop() : running(false), aborting(false), idle{}, next_idle_item(idle.end()) { if (!default_loop) default_loop = this; }
	Time now() { return std::chrono::steady_clock::now(); }
	constexpr bool is_running () const { return running; }
	int handle_timeouts();
	void iteration(bool block = false);
	void run();
	void stop(bool force = false);

	IoHandle add_io(IoRecord const &item) { return items.add(item); }
	TimeoutHandle add_timeout(TimeoutRecord const &timeout) { timeouts.insert(timeout); return --timeouts.end(); }
	IdleHandle add_idle(IdleRecord const &record) { idle.push_back(record); return --idle.end(); }

	void remove_io(IoHandle handle) { items.remove(handle); }
	void remove_timeout(TimeoutHandle handle) { timeouts.erase(handle); }
	void remove_idle(IdleHandle handle);

	IoHandle invalid_io() const { return -1; }
	TimeoutHandle invalid_timeout() { return timeouts.end(); }
	std::set <TimeoutRecord>::const_iterator invalid_timeout() const { return timeouts.end(); }
	IdleHandle invalid_idle() { return idle.end(); }
	std::list <IdleRecord>::const_iterator invalid_idle() const { return idle.end(); }

	void update_name(IoHandle handle, std::string const &n) { items.update_name(handle, n); }
};

}

#endif

// vim: set foldmethod=marker :
