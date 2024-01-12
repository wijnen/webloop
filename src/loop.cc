#include "webloop/loop.hh"
#include "webloop/tools.hh"
#include <cassert>

using namespace std::literals;

namespace Webloop {

int Loop::PollItems::add(IoRecord const &item) { // {{{
	// Add an fd; return index.
	STARTFUNC;
	size_t ret;
	if (!empty_items.empty()) {
		// There is space at a removed index.
		ret = *empty_items.begin();
		empty_items.erase(empty_items.begin());
	}
	else {
		if (num == capacity) {
			// There is no space; extend capacity.
			capacity *= 8;
			struct pollfd *new_data = new struct pollfd[capacity];
			for (int i = 0; i < num; ++i)
				new_data[i] = data[i];
			delete[] data;
			data = new_data;
		}
		// Now there is space at the end.
		ret = num++;
	}
	data[ret].fd = item.fd;
	data[ret].events = item.events;
	data[ret].revents = 0;
	// Add item
	if (ret < items.size())
		items[ret] = item;
	else {
		//WL_log("adding new item; ret = " + std::to_string(ret) + "/" + std::to_string(items.size()));
		assert(ret == items.size());
		items.push_back(item);
	}
	//WL_log("adding loop item " + std::to_string(ret));
	return ret;
} // }}}

void Loop::PollItems::remove(int index) { // {{{
	// Remove fd using index as returned by add.
	STARTFUNC;
	if (DEBUG > 3)
		WL_log("removing loop item " + std::to_string(index));
	assert(index >= 0);
	assert(data[index].fd >= 0);
	if (index == num - 1) {
		--num;
		items.pop_back();
		// Remove newly last elements if they were empty.
		while (!empty_items.empty() && *--empty_items.end() == num - 1) {
			empty_items.erase(--empty_items.end());
			--num;
			items.pop_back();
		}
		// Shrink capacity if too many items are removed.
		if (num * 8 * 2 < capacity && capacity > min_capacity) {
			capacity /= 8;
			struct pollfd *new_data = new struct pollfd[capacity];
			for (int i = 0; i < num; ++i)
				new_data[i] = data[i];
			delete[] data;
			data = new_data;
		}
		return;
	}
	data[index].fd = -1;
	empty_items.insert(index);
} // }}}

std::string Loop::PollItems::print() { // {{{
	STARTFUNC;
	std::ostringstream ret;
	ret << "PollItems num = " << num << "/" << capacity << "(minimum " << min_capacity << "); Items:";
	for (size_t i = 0; i < (size_t)num; ++i) {
		ret << "\n\t" << data[i].fd << ": ";
		if (i < items.size()) {
			IoRecord &r = items[i];
			ret << r.name << " data:" << r.object << " fd:" << r.fd << " events:" << r.events << (r.read ? " read" : "") << (r.write ? " write" : "") << (r.error ? " error" : "");
		}
		else
			ret << "X";
	}
	return ret.str();
} // }}}

int Loop::handle_timeouts() { // {{{
	STARTFUNC;
	Time current = now();
	while (!aborting && !timeouts.empty() && timeouts.begin()->time <= current) {
		TimeoutRecord rec = *timeouts.begin();
		timeouts.erase(timeouts.begin());
		bool keep = (rec.object->*rec.cb)();
		if (keep && rec.interval > Duration()) {
			while (rec.time <= current)
				rec.time += rec.interval;
			add_timeout({rec.time, rec.interval, rec.object, rec.cb});
		}
	}
	if (timeouts.empty())
		return -1;
	return (timeouts.begin()->time - current) / 1ms;
} // }}}

Loop *Loop::get(Loop *arg) { // {{{
	if (arg) {
		if (!default_loop)
			default_loop = arg;
		return arg;
	}
	if (!default_loop)
		default_loop = &fallback_loop;
	return default_loop;
} // }}}

void Loop::iteration(bool block) { // {{{
	STARTFUNC;
	// Do a single iteration of the main loop.
	// @return None.
	int t = handle_timeouts();
	if (!block)
		t = 0;
	poll(items.data, items.num, t);
	for (int i = 0; !aborting && i < items.num; ++i) {
		if (items.data[i].fd < 0)
			continue;
		short ev = items.data[i].revents;
		if (ev == 0)
			continue;
		if (ev & (POLLERR | POLLNVAL)) {
			if (!items.items[i].error || !(items.items[i].object->*(items.items[i].error))())
				items.remove(i);
		}
		else {
			if (ev & (POLLIN | POLLPRI)) {
				if (!items.items[i].read || !(items.items[i].object->*(items.items[i].read))()) {
					if (items.data[i].fd >= 0)
						items.remove(i);
					continue;
				}
			}
			if (ev & POLLOUT) {
				if (!items.items[i].write || !(items.items[i].object->*(items.items[i].write))())
					if (items.data[i].fd >= 0)
						items.remove(i);
			}
		}
	}
	handle_timeouts();
} // }}}

void Loop::run() { // {{{
	STARTFUNC;
	// Wait for events and handle them.
	// @return None.
	assert(!running);
	running = true;
	aborting = false;
	while (running) {
		if (DEBUG > 4)
			WL_log("running, items = " + items.print());
		iteration(idle.empty());
		if (!running)
			continue;
		std::list <IdleRecord>::iterator i = idle.begin();
		std::list <IdleRecord>::iterator next;
		while (i != idle.end()) {
			next = i;
			++next;
			if (!(i->object->*(i->cb))())
				remove_idle(i);
			if (!running)
				break;
			i = next;
		}
	}
	running = false;
	aborting = false;
}
// }}}

void Loop::stop(bool force) { // {{{
	STARTFUNC;
	// Stop a running loop.
	// @return None.
	assert(running);
	running = false;
	if (force)
		aborting = true;
} // }}}

Loop *Loop::default_loop;
Loop Loop::fallback_loop;

}

// vim: set foldmethod=marker :
