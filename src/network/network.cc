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

#include "network.hh"
#include <cassert>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "../websocketd/webobject.hh"

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
std::ostream *log_output = &std::cerr;
bool log_date = false;

void set_log_output(std::ostream &target) { // {{{
	/* Change target for log().
	By default, log() sends its output to standard error.  This function is
	used to change the target.
	@param file: The new file to write log output to.
	@return None.
	*/
	log_output = &target;
	log_date = true;
} // }}}

void log_impl(std::string const &message, std::string const &filename, std::string const &funcname, int line) { // {{{
	/* Log a message.
	Write a message to log (default standard error, can be changed with
	set_log_output()).  A timestamp is added before the message and a
	newline is added to it.
	@param message: The message to log. Multiple arguments are logged on separate lines. Newlines in arguments cause the message to be split, so they should not contain a closing newline.
	@param filename: Override filename to report.
	@param line: Override line number to report.
	@param funcname: Override function name to report.
	@param depth: How deep to enter into the call stack for function info.
	@return None.
	*/
	char buffer[100];
	auto t = std::time(nullptr);
	strftime(buffer, sizeof(buffer), log_date ? "%F %T" : "%T", std::gmtime(&t));
	(*log_output) << buffer << ": " << filename << ":" << line << ":" << funcname << ": " << message << std::endl;
} // }}}
// }}}

// Main loop. {{{
int PollItems::add(Item *item) { // {{{
	// Add an fd; return index.
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
	data[ret].fd = item->fd;
	data[ret].events = item->events;
	// Add item
	if (ret < items.size())
		items[ret] = item;
	else {
		assert(ret == items.size());
		items.push_back(item);
	}
	item->handle = ret;
	return ret;
} // }}}

void PollItems::remove(int index) { // {{{
	// Remove fd using index as returned by add.
	assert(data[index].fd >= 0);
	if (index == num - 1) {
		--num;
		items.pop_back();
		// Remove newly last elements if they were empty.
		while (!empty_items.empty() && *empty_items.end() == num - 1) {
			empty_items.erase(empty_items.end());
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

int Loop::handle_timeouts() { // {{{
	Time current = now();
	while (!aborting && !timeouts.empty() && timeouts.begin()->time <= current) {
		TimeoutRecord rec = *timeouts.begin();
		timeouts.erase(timeouts.begin());
		bool keep = rec.cb(rec.user_data);
		if (keep && rec.interval > Duration()) {
			while (rec.time <= current)
				rec.time += rec.interval;
			add_timeout({rec.time, rec.interval, rec.cb, rec.user_data});
		}
	}
	if (timeouts.empty())
		return -1;
	return (timeouts.begin()->time - current) / 1ms;
} // }}}

void Loop::iteration(bool block) { // {{{
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
			if (!items.items[i]->error || !items.items[i]->error(items.items[i]->user_data))
				items.remove(i);
		}
		else {
			if (ev & (POLLIN | POLLPRI)) {
				if (!items.items[i]->read || !items.items[i]->read(items.items[i]->user_data)) {
					items.remove(i);
					continue;
				}
			}
			if (ev & POLLOUT) {
				if (!items.items[i]->write || !items.items[i]->write(items.items[i]->user_data))
					items.remove(i);
			}
		}
	}
	handle_timeouts();
} // }}}

void Loop::run() { // {{{
	// Wait for events and handle them.
	// @return None.
	assert(!running);
	running = true;
	aborting = false;
	while (running) {
		iteration(idle.empty());
		if (!running)
			continue;
		for (auto i = idle.begin(); i != idle.end(); ++i) {
			if (!i->cb(i->user_data))
				remove_idle(i);
			if (!running)
				break;
		}
	}
	running = false;
	aborting = false;
}
// }}}

void Loop::stop(bool force) { // {{{
	// Stop a running loop.
	// @return None.
	assert(running);
	running = false;
	if (force)
		aborting = true;
} // }}}

Loop *Loop::default_loop;
// }}}

// Network sockets. {{{
bool Socket::read_impl(void *self_ptr) { // {{{
	Socket *self = reinterpret_cast <Socket *>(self_ptr);
	self->buffer += self->recv();
	if (!self->read_cb)
		return false;
	self->read_cb(self->buffer, self->user_data);
	return true;
} // }}}

bool Socket::handle_read_line_data(std::string &&data) { // {{{
	buffer += data;
	if (!read_lines_cb)
		return false;
	auto p = buffer.find_first_of("\r\n");
	while (p != std::string::npos) {
		std::string line = buffer.substr(0, p);
		if (buffer[p] == '\r' && p + 1 < buffer.size() && buffer[p + 1] == '\n')
			buffer = buffer.substr(p + 2);
		else
			buffer = buffer.substr(p + 1);
		read_lines_cb(std::move(line), user_data);
		p = buffer.find_first_of("\r\n");
	}
	return true;
} // }}}

bool Socket::read_lines_impl(void *self_ptr) { // {{{
	Socket *self = reinterpret_cast <Socket *>(self_ptr);
	return self->handle_read_line_data(self->recv());
} // }}}

Socket::Socket(std::string const &address, void *user_data) // {{{
	:
		fd(-1),
		mymaxsize(4096),
		user_data(user_data),
		read_item({nullptr, -1, 0, nullptr, nullptr, nullptr, -1}),
		read_cb(nullptr),
		buffer(),
		server(nullptr),
		server_data(),
		disconnect_cb(nullptr)
{
	/* Create a connection.
	@param address: connection target.  This is a unix domain
	socket if there is a / in it.  If it is not a unix domain
	socket, it is a port number or service name, optionally
	prefixed with a hostname and a :.  If no hostname is present,
	localhost is used.
	*/

	// Parse string.
	auto p = address.find("://");
	if (p != std::string::npos) {
		protocol = address.substr(0, p);
		p += 3;
	}
	else
		p = 0;

	auto q = address.find_first_of(":/?#", p);
	if (q == std::string::npos) {
		hostname = address.substr(p);
		service = protocol;
	}
	else {
		hostname = address.substr(p, q - p);
		if (address[q] == ':') {
			p = q + 1;
			q = address.find_first_of("/?#", p);
			if (q == std::string::npos)
				service = address.substr(p);
			else {
				service = address.substr(p, q - p);
				extra = address.substr(q);
			}
		}
		else {
			service = protocol;
			extra = address.substr(q);
		}
	}
	
	if (protocol.empty() && service.empty() && !extra.empty()) {
		// Unix domain socket.
		protocol.clear();
		hostname.clear();
		service = address;
		extra.clear();
	}
	else {
		if (service.empty()) {
			service = hostname;
			hostname = "localhost";
		}
	}
	
	// Set up the connection.
	if (hostname.empty()) {
		// Unix domain socket.
		fd = socket(AF_UNIX, SOCK_STREAM, 0);
		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, service.c_str(), sizeof(addr.sun_path));
		connect(fd, reinterpret_cast <sockaddr *>(&addr), sizeof(addr));
	}
	else {
		struct addrinfo addr_hint;
		addr_hint.ai_family = AF_UNSPEC;
		addr_hint.ai_socktype = SOCK_STREAM;
		addr_hint.ai_protocol = IPPROTO_TCP;
		addr_hint.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
		struct addrinfo *addr;
		int code = getaddrinfo(hostname.c_str(), service.c_str(), &addr_hint, &addr);
		if (code != 0) {
			std::cerr << protocol << " / " << hostname << " / " << service << " / " << extra << std::endl;
			std::cerr << "unable to open socket: " << gai_strerror(code) << std::endl;
			throw "unable to open socket";
		}
		fd = -1;
		for (auto rp = addr; rp; rp = rp->ai_next) {
			fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
			if (fd < 0) {
				std::cerr << "unable to open socket: " << strerror(errno) << std::endl;
				continue;
			}
			if (connect(fd, rp->ai_addr, rp->ai_addrlen) < 0) {
				if (!rp->ai_next || errno != ECONNREFUSED) {
					std::cerr << protocol << " / " << hostname << " / " << service << " / " << extra << std::endl;
					std::cerr << "unable to connect socket: " << strerror(errno) << std::endl;
				}
				fd = -1;
				continue;
			}
		}
		freeaddrinfo(addr);
		if (fd < 0) {
			std::cerr << "unable to connect any socket" << std::endl;
			throw "unable to connect any socket";
		}
	}
} // }}}

std::string Socket::close() { // {{{
	//'Close the network connection.
	// @return The data that was remaining in the line buffer, if any.
	if (fd < 0)
		return "";
	std::string pending = unread();
	::close(fd);
	fd = -1;
	if (server)
		server->remote_disconnect(this->server_data);
	if (disconnect_cb)
		return disconnect_cb(pending, user_data);
	return pending;
} // }}}

void Socket::send(std::string const &data) { // {{{
	/* Send data over the network.
	Send data over the network.  Block until all data is in the buffer.
	@param data: data to write.  This should be of type bytes.
	@return None.
	*/
	if (fd < 0)
		return;
	log("Sending: " + WebString(data).dump());
	size_t p = 0;
	while (p < data.size()) {
		ssize_t n = write(fd, &data[p], data.size() - p);
		if (n <= 0) {
			std::cerr << "failed to write data to socket";
			close();
		}
		p += n;
	}
} // }}}

void Socket::sendline(std::string const &data) { // {{{
	// Send a line of text.
	// Identical to send(), but data is a str and a newline is added.
	// @param data: line to send.  A newline is added.  This should be
	//	 of type str.  The data is sent as utf-8.
	send(data + "\n");
} // }}}

std::string Socket::recv() { // {{{
	/* Read data from the network.
	Data is read from the network.  If the socket is not set to
	non-blocking, this call will block if there is no data.  It
	will return a short read if limited data is available.  The
	read data is returned as a bytes object.  If TLS is enabled,
	more than maxsize bytes may be returned.  On EOF, the socket is
	closed and if disconnect_cb is not set, an EOFError is raised.
	@param maxsize: passed to the underlaying recv call.  If TLS is
		enabled, no data is left pending, which means that more
		than maxsize bytes can be returned.
	@return The received data as a bytes object.
	*/
	if (fd < 0) {
		log("recv on closed socket");
		throw "recv on closed socket";
	}
	char buffer[mymaxsize];
	auto num = ::read(fd, buffer, mymaxsize);
	if (num < 0) {
		log("Error reading from socket");
		return close();
	}
	if (num == 0) {
		bool have_server = server;
		std::string ret = close();
		std::cerr << "closed" << std::endl;
		if (!disconnect_cb && !have_server)
			throw "network connection closed";
		return ret;
	}
	return std::string(buffer, num);
} // }}}

std::string Socket::rawread(Item::Cb cb, Item::Cb error, Loop *loop, void *udata) { // {{{
	/* Register function to be called when data is ready for reading.
	The function will be called when data is ready.  The callback
	must read the function or call unread(), or it will be called
	again after returning.
	@param callback: function to be called when data can be read.
	@param error: function to be called if there is an error on the socket.
	@return The data that was remaining in the line buffer, if any.
	*/
	if (fd < 0)
		return std::string();
	loop = Loop::get(loop);
	std::string ret = unread(loop);
	read_item = Item {udata ? udata : user_data, fd, POLLIN | POLLPRI, cb, nullptr, error, -1};
	read_item.handle = loop->add_io(read_item);
	return ret;
} // }}}

void Socket::read(ReadCb callback, Item::Cb error, Loop *loop, size_t maxsize) { // {{{
	/* Register function to be called when data is received.
	When data is available, read it and call this function.  The
	data that was remaining in the line buffer, if any, is sent to
	the callback immediately.
	@param callback: function to call when data is available.  The
	data is passed as a parameter.
	@param error: function to be called if there is an error on the
	socket.
	@param maxsize: buffer size that is used for the recv call.
	@return None.
	*/
	if (fd < 0)
		return;
	loop = Loop::get(loop);
	std::string first = unread(loop);
	mymaxsize = maxsize;
	read_cb = callback;
	rawread(read_impl, error, loop, this);
	if (!first.empty())
		read_cb(first, user_data);
} // }}}

void Socket::read_lines(ReadLinesCb callback, Item::Cb error, Loop *loop, size_t maxsize) { // {{{
	/* Buffer incoming data until a line is received, then call a function.
	When a newline is received, all data up to that point is
	decoded as an utf-8 string and passed to the callback.
	@param callback: function that is called when a line is
	received.  The line is passed as a str parameter.
	@param error: function that is called when there is an error on
	the socket.
	@param maxsize: used for the recv calls that are made.  The
	returned data accumulates until a newline is received; this is
	not a limit on the line length.
	@return None.
	*/
	if (fd < 0)
		return;
	loop = Loop::get(loop);
	std::string first = unread(loop);
	mymaxsize = maxsize;
	read_lines_cb = callback;
	rawread(read_lines_impl, error, loop, this);
	if (!first.empty())
		handle_read_line_data(std::move(first));
} // }}}

std::string Socket::unread(Loop *loop) { // {{{
	/* Cancel a read() or rawread() callback.
	Cancel any read callback.
	@return Bytes left in the line buffer, if any.  The line buffer
		is cleared.
	*/
	loop = Loop::get(loop);
	if (read_item.handle >= 0) {
		loop->remove_io(read_item.handle);
		read_item.handle = -1;
	}
	std::string ret = std::move(buffer);
	buffer.clear();
	return ret;
} // }}}
// }}}

void Server::remote_disconnect(std::list <Socket>::iterator socket) { // {{{
	socket->server = nullptr;
	remotes.erase(socket);
} // }}}

void Server::open_socket(std::string const &service, int backlog) { // {{{
	// Open the listening socket(s).
	auto p = service.find("/");
	if (p != std::string::npos) {
		// Unix domain socket.
		int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0) {
			std::cerr << "unable to open unix socket: " << strerror(errno) << std::endl;
			throw "unable to open socket";
		}
		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, service.c_str(), sizeof(addr.sun_path));
		if (bind(fd, reinterpret_cast <sockaddr const *>(&addr), sizeof(addr)) < 0) {
			std::cerr << "unable to bind unix socket: " << strerror(errno) << std::endl;
			throw "unable to open socket";
		}
		listeners.emplace(listeners.end(), fd, Socket {fd, this});
		return;
	}
	struct addrinfo addr_hint;
	addr_hint.ai_family = AF_UNSPEC;
	addr_hint.ai_socktype = SOCK_STREAM;
	addr_hint.ai_protocol = IPPROTO_TCP;
	addr_hint.ai_flags = AI_PASSIVE | AI_V4MAPPED | AI_ADDRCONFIG;
	struct addrinfo *addr;
	int code = getaddrinfo(nullptr, service.c_str(), &addr_hint, &addr);
	if (code != 0) {
		std::cerr << "unable to open socket: " << gai_strerror(code) << std::endl;
		throw "unable to open socket";
	}
	for (auto rp = addr; rp; rp = rp->ai_next) {
		int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0) {
			std::cerr << "unable to create socket: " << strerror(errno) << std::endl;
			continue;
		}
		if (bind(fd, rp->ai_addr, rp->ai_addrlen) < 0) {
			if (listeners.empty() || errno != EADDRINUSE)
				std::cerr << "unable to bind: " << strerror(errno) << std::endl;
			::close(fd);
			continue;
		}
		if (listen(fd, backlog) < 0) {
			std::cerr << "unable to listen: " << strerror(errno) << std::endl;
			::close(fd);
			continue;
		}
		listeners.emplace(listeners.end(), fd, Socket {fd, this});
	}
	freeaddrinfo(addr);
	if (listeners.empty()) {
		std::cerr << "unable to bind socket" << std::endl;
		throw "unable to bind socket";
	}
} // }}}

bool Server::accept_remote(void *self_ptr) { // {{{
	Acceptor *acceptor = reinterpret_cast <Acceptor *>(self_ptr);
	Server *self = acceptor->server;
	struct sockaddr_un addr;	// Use largest struct; cast down for others.
	socklen_t addrlen = sizeof(addr);
	int fd = ::accept(acceptor->listener->fd, reinterpret_cast <struct sockaddr *>(&addr), &addrlen);
	if (addrlen > sizeof(addr)) {
		std::cerr << "Warning: remote address is truncated" << std::endl;
		addrlen = sizeof(addr);
	}
	self->remotes.emplace(self->remotes.end(), fd, self->user_data);
	Socket &remote = self->remotes.back();
	remote.server = self;
	remote.server_data = --self->remotes.end();
	switch (addr.sun_family) {
	case AF_INET:
	{
		struct sockaddr_in *addr4 = reinterpret_cast <struct sockaddr_in *>(&addr);
		std::ostringstream p;
		p << addr4->sin_port;
		remote.service = p.str();
		char buffer[INET_ADDRSTRLEN];
		if (!inet_ntop(addr4->sin_family, &addr4->sin_addr, buffer, sizeof(buffer))) {
			std::cerr << "unable to parse IPv4 address" << std::endl;
			break;
		}
		remote.hostname = buffer;
		break;
	}
	case AF_INET6:
	{
		struct sockaddr_in6 *addr6 = reinterpret_cast <struct sockaddr_in6 *>(&addr);
		std::ostringstream p;
		p << addr6->sin6_port;
		remote.service = p.str();
		char buffer[INET6_ADDRSTRLEN];
		if (!inet_ntop(addr6->sin6_family, &addr6->sin6_addr, buffer, sizeof(buffer))) {
			std::cerr << "unable to parse IPv6 address" << std::endl;
			break;
		}
		remote.hostname = buffer;
		break;
	}
	case AF_UNIX:
	{
		addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
		remote.hostname = addr.sun_path;
		break;
	}
	default:
		std::cerr << "unknown address family for remote socket; not reading remote details." << std::endl;
		break;
	}
	self->connected_cb(remote, self->user_data);
	return true;
} // }}}

Server::Server(std::string const &service, ConnectedCb cb, Item::Cb error, void *user_data, Loop *loop, int backlog) : listeners(), backlog(backlog), user_data(user_data), connected_cb(cb), remotes() {
	open_socket(service, backlog);
	for (auto i = listeners.begin(); i != listeners.end(); ++i) {
		i->acceptor = Acceptor {this, i};
		i->socket.rawread(accept_remote, error, loop, &i->acceptor);
	}
}

void Server::close() {
	assert(!listeners.empty());
	while (!remotes.empty())
		remotes.back().close();
	while (!listeners.empty()) {
		listeners.back().socket.close();
		listeners.pop_back();
	}
}
// }}}
