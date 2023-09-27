// vim: set fileencoding=utf-8 foldmethod=marker :

/* {{{ Copyright 2013-2023 Bas Wijnen <wijnen@debian.org>
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or(at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// }}} */

// Documentation {{{
/* @mainpage
Python-network is a module which intends to make networking easy.  It supports
tcp and unix domain sockets.  Connection targets can be specified in several
ways.
*/

/* @file
Python module for easy networking.  This module intends to make networking
easy.  It supports tcp and unix domain sockets.  Connection targets can be
specified in several ways.
*/

/* @package network Python module for easy networking.
This module intends to make networking easy.  It supports tcp and unix domain
sockets.  Connection targets can be specified in several ways.
*/
// }}}

// {{{ Includes.
#include <algorithm>
#include <iostream>
#include <vector>
#include <list>
#include <unistd.h>
#include <set>
#include <chrono>
#include <ctime>
#include <poll.h>

using namespace std::literals;
// }}}

/* {{{ Interface description
// - connection setup
//   - connect to server
//   - listen on port
// - when connected
//   - send data
//   - asynchronous read
//   - blocking read for data

// implementation:
// - Server: listener, creating Sockets on accept
// - Socket: used for connection; symmetric
// }}} */

// Logging. {{{
extern std::ostream *log_output;
extern bool log_date;

void set_log_output(std::ostream &target);
void log_impl(std::string const &message, std::string const &filename, std::string const &funcname, int line);
#define log(msg) log_impl(msg, __FILE__, __PRETTY_FUNCTION__, __LINE__)
// }}}

// Main loop. {{{
struct Item { // {{{
	typedef bool (*Cb)(void *user_data);
	void *user_data;
	int fd;
	short events;
	Cb read;
	Cb write;
	Cb error;
	int handle;
}; // }}}

struct PollItems { // {{{
	struct pollfd *data;
	std::vector <Item *> items;
	int num;
	int capacity;
	int min_capacity;
	std::set <int> empty_items;	// Empty items that have lower index than num - 1.
	PollItems(int initial_capacity = 32) : data(new struct pollfd[initial_capacity]), num(0), capacity(initial_capacity), min_capacity(initial_capacity) {}
	int add(Item *item);
	void remove(int index);
}; // }}}

class Loop { // {{{
public:
	// Types. {{{
	typedef std::chrono::time_point <std::chrono::steady_clock> Time;
	typedef std::chrono::steady_clock::duration Duration;
	typedef bool (*Cb)(void *user_data);
	struct IdleRecord {
		Cb cb;
		void *user_data;
	};
	struct TimeoutRecord {
		Time time;
		Duration interval;	// 0 for single shot.
		Cb cb;
		void *user_data;
		bool operator<(TimeoutRecord const &other) const { return time < other.time; }
	};
	// }}}

private:
	static Loop *default_loop;
	bool running;
	bool aborting;
	std::list <IdleRecord> idle;
	PollItems items;
	std::set <TimeoutRecord> timeouts;

public:
	static Loop *get(Loop *arg = nullptr) { return arg ? arg : default_loop ? default_loop : new Loop(); }
	Loop() : running(false), aborting(false) { if (!default_loop) default_loop = this; }
	Time now() { return std::chrono::steady_clock::now(); }
	int handle_timeouts();
	void iteration(bool block = false);
	void run();
	void stop(bool force = false);

	int add_io(Item &item) { return items.add(&item); }
	std::set <TimeoutRecord>::iterator add_timeout(TimeoutRecord &&timeout) { return timeouts.insert(timeout).first; }
	std::list <IdleRecord>::iterator add_idle(IdleRecord &&record) { idle.push_back(record); return --idle.end(); }

	void remove_io(int handle) { items.remove(handle); }
	void remove_timeout(std::set <TimeoutRecord>::iterator handle) { timeouts.erase(handle); }
	void remove_idle(std::list <IdleRecord>::iterator handle) { idle.erase(handle); }
}; // }}}
// }}}

// Network sockets. {{{
class Server;
class Socket { // {{{
	typedef void (*ReadCb)(std::string &buffer, void *user_data);
	typedef void (*ReadLinesCb)(std::string &&buffer, void *user_data);
	// Connection object.
	int fd;
	size_t mymaxsize;	// For read operations.
	void *user_data;
	Item read_item;
	ReadCb read_cb;
	ReadLinesCb read_lines_cb;
	std::string buffer;
	Server *server;
	std::list <Socket>::iterator server_data;
	friend class Server;

	static bool read_impl(void *self_ptr);
	static bool read_lines_impl(void *self_ptr);
	bool handle_read_line_data(std::string &&data);
public:
	// Callback when socket is disconnected.
	std::string (*disconnect_cb)(std::string const &pending, void *user_data);
	// Read only address components; these are filled from address in the constructor.
	// <protocol>://<hostname>:<service>[?/#]<extra>
	std::string protocol;
	std::string hostname;
	std::string service;
	std::string extra;

	Socket(std::string const &address, void *user_data = nullptr);
	Socket(int fd, void *user_data = nullptr)	 // Use an fd as a "socket", so it can use the functionality of this class. {{{
		:
			fd(fd),
			mymaxsize(0),
			user_data(user_data),
			read_item({nullptr, -1, 0, nullptr, nullptr, nullptr, -1}),
			read_cb(nullptr),
			buffer(),
			server(nullptr),
			server_data(),
			disconnect_cb(nullptr)
			{} // }}}
	std::string close();
	void send(std::string const &data);
	void sendline(std::string const &data);
	std::string recv();
	std::string rawread(Item::Cb cb, Item::Cb error = nullptr, Loop *loop = nullptr, void *udata = nullptr);
	void read(ReadCb callback, Item::Cb error = Item::Cb(), Loop *loop = nullptr, size_t maxsize = 4096);
	void read_lines(ReadLinesCb callback, Item::Cb error = Item::Cb(), Loop *loop = nullptr, size_t maxsize = 4096);
	std::string unread(Loop *loop = nullptr);
}; // }}}

class Server { // {{{
	friend class Socket;
public:
	typedef void (*ConnectedCb)(Socket &remote, void *user_data);
private:
	struct Listener;
	struct Acceptor {
		Server *server;
		std::list <Listener>::iterator listener;
	};
	struct Listener {
		int fd;
		Socket socket;
		Acceptor acceptor;
	};
	std::list <Listener> listeners;
	int backlog;
	void *user_data;
	ConnectedCb connected_cb;
	void open_socket(std::string const &service, int backlog);
	void remote_disconnect(std::list <Socket>::iterator socket);
	static bool accept_remote(void *self_ptr);
public:
	std::list <Socket> remotes;
	Server(std::string const &service, ConnectedCb cb, Item::Cb error = nullptr, void *user_data = nullptr, Loop *loop = nullptr, int backlog = 5);
	void close();
	~Server() { close(); }
}; // }}}
// }}}
