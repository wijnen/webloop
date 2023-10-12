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
#include <ctime>
#include "tools.hh"
#include "loop.hh"
#include "url.hh"

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

class Server;

class Socket { // {{{
	typedef void (*ReadCb)(std::string &buffer, void *user_data);
	typedef void (*ReadLinesCb)(std::string &&buffer, void *user_data);
	// Connection object.
	int fd;
	size_t mymaxsize;	// For read operations.
	Loop *current_loop;
	Loop::IoRecord read_item;
	ReadCb read_cb;
	ReadLinesCb read_lines_cb;
	std::string buffer;
	Server *server;
	std::list <Socket *>::iterator server_data;
	friend class Server;

	static bool read_impl(void *self_ptr);
	static bool read_lines_impl(void *self_ptr);
	bool handle_read_line_data(std::string &&data);
public:
	// Callback when socket is disconnected.
	std::string (*disconnect_cb)(std::string const &pending, void *user_data);
	// User data, which is passed to callbacks.
	void *user_data;
	// Read only address components; these are filled from address in the constructor.
	URL url;

	Socket(std::string const &address, void *user_data = nullptr);
	Socket(int fd = -1, void *user_data = nullptr)	 // Use an fd as a "socket", so it can use the functionality of this class. {{{
		:
			fd(fd),
			mymaxsize(0),
			read_item({nullptr, -1, 0, nullptr, nullptr, nullptr, -1}),
			read_cb(nullptr),
			buffer(),
			server(nullptr),
			server_data(),
			disconnect_cb(nullptr),
			user_data(user_data)
			{} // }}}
	Socket(Socket &&other);
	Socket &operator=(Socket &&other);
	std::string close();
	void send(std::string const &data);
	void sendline(std::string const &data);
	std::string recv();
	std::string rawread(Loop::Cb cb, Loop::Cb error = nullptr, Loop *loop = nullptr, void *udata = nullptr);
	void read(ReadCb callback, Loop::Cb error = Loop::Cb(), Loop *loop = nullptr, size_t maxsize = 4096);
	void read_lines(ReadLinesCb callback, Loop::Cb error = Loop::Cb(), Loop *loop = nullptr, size_t maxsize = 4096);
	std::string unread();
}; // }}}

class Server { // {{{
	friend class Socket;
public:
	// Callback for newly connected socket. Remote is destroyed after this is called, so it must be moved into a different Socket.
	typedef void (*ConnectedCb)(Socket &&remote, void *user_data);
private:
	struct Listener {	// Data for listening socket; used as user_data for poll events.
		Server *server;
		int fd;
		Socket socket;
		Listener(Server *server, int fd) : server(server), fd(fd), socket(fd, server) {}
	};
	Loop *listenloop;
	std::list <Listener> listeners;
	int backlog;
	void *user_data;
	ConnectedCb connected_cb;
	void open_socket(std::string const &service, int backlog);
	void remote_disconnect(std::list <Socket *>::iterator socket);
	static bool accept_remote(void *self_ptr);
public:
	std::list <Socket *> remotes;
	Server(std::string const &service, ConnectedCb cb, Loop::Cb error = nullptr, void *user_data = nullptr, Loop *loop = nullptr, int backlog = 5);
	void close();
	~Server() { close(); }
}; // }}}
