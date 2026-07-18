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

#include <cassert>
#include <fcntl.h>
#include "webloop/webobject.hh"
#include "webloop/network.hh"

namespace Webloop {

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

// Network sockets. {{{
// Read internals. {{{
bool SocketBase::raw_read_impl()
{ // {{{
	STARTFUNC;
	if (m_raw_read_cb == nullptr || m_target == nullptr) {
		WL_log(std::format("raw cb: {}; target: {}", (bool)m_raw_read_cb, (bool)m_target));
		return false;
	}
	(m_target->*m_raw_read_cb)();
	return true;
} // }}}

bool SocketBase::read_impl()
{ // {{{
	STARTFUNC;
	if (m_buffer.empty())
		m_buffer = recv();	// Allow moving.
	else
		m_buffer += recv();
	if (DEBUG > 3)
		WL_log("new data; buffer:" + WebString(m_buffer).dump());
	if (m_read_cb == nullptr)
		return false;
	(m_target->*m_read_cb)(m_buffer);
	return true;
} // }}}

bool SocketBase::handle_read_line_data(std::string &&data)
{ // {{{
	STARTFUNC;
	if (m_buffer.empty())
		m_buffer = std::move(data);
	else
		m_buffer += data;
	if (m_read_lines_cb == nullptr)
		return false;
	auto p = m_buffer.find_first_of("\r\n");
	while (p != std::string::npos) {
		std::string line = m_buffer.substr(0, p);
		if (m_buffer[p] == '\r' && p + 1 < m_buffer.size() &&
				m_buffer[p + 1] == '\n') {
			m_buffer = m_buffer.substr(p + 2);
		} else {
			m_buffer = m_buffer.substr(p + 1);
		}
		(m_target->*m_read_lines_cb)(line);
		p = m_buffer.find_first_of("\r\n");
	}
	return true;
} // }}}

bool SocketBase::read_lines_impl()
{ // {{{
	STARTFUNC;
	return handle_read_line_data(recv());
} // }}}

std::string SocketBase::recv()
{ // {{{
	STARTFUNC;
	/* Read data from the network.
	Data is read from the network.  If the socket is not set to
	non-blocking, this call will block if there is no data.  It
	will return a short read if limited data is available.  The
	read data is returned as a bytes object.  If TLS is enabled,
	more than maxsize bytes may be returned.  On EOF, the socket is
	closed and if disconnected_cb is not set, an EOFError is raised.
	@param maxsize: passed to the underlaying recv call.  If TLS is
		enabled, no data is left pending, which means that more
		than maxsize bytes can be returned.
	@return The received data as a bytes object.
	*/
	if (m_in_fd < 0) {
		WL_log("recv on closed socket");
		throw "recv on closed socket";
	}
	WL_log(std::format("recv on fd {}, size {}", m_in_fd, m_maxsize));
	char *buffer = new char[m_maxsize];
	auto num = ::read(m_in_fd, buffer, m_maxsize);
	if (num < 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return std::string();
		WL_log(std::string("Error reading from socket: ") + strerror(errno));
		delete[] buffer;
		return close();
	}
	if (num == 0) {
		bool have_server = m_server.get() != nullptr;
		std::string ret = close();
		if (DEBUG > 3)
			WL_log("closed");
		if (!m_disconnected_cb && !have_server)
			throw "network connection closed";
		delete[] buffer;
		return ret;
	}
	std::string ret(buffer, num);
	delete[] buffer;
	return ret;
} // }}}
// }}}

// Constructor- and destructor-related. {{{
// Constructors. {{{
SocketBase::SocketBase(std::string const &name, Loop *loop)
	:
		m_in_fd(-1), m_out_fd(-1),
		m_maxsize(4096),
		m_current_loop(Loop::get(loop)),
		m_read_handle(m_current_loop->invalid_io()),
		m_target(nullptr),
		m_raw_read_cb(nullptr),
		m_read_cb(nullptr),
		m_read_lines_cb(nullptr),
		m_written_cb(nullptr),
		m_connected_cb(nullptr),
		m_disconnected_cb(nullptr),
		m_error_cb(nullptr),
		m_buffer{},
		m_server(nullptr),
		m_server_data{},
		m_name(name),
		m_url{}
{}

SocketBase::SocketBase(std::string const &name, URL const &address, Loop *loop)
	:
		m_in_fd(-1), m_out_fd(-1),
		m_maxsize(4096),
		m_current_loop(Loop::get(loop)),
		m_read_handle(m_current_loop->invalid_io()),
		m_target(nullptr),
		m_raw_read_cb(nullptr),
		m_read_cb(nullptr),
		m_read_lines_cb(nullptr),
		m_written_cb(nullptr),
		m_connected_cb(nullptr),
		m_disconnected_cb(nullptr),
		m_error_cb(nullptr),
		m_buffer(),
		m_server(nullptr),
		m_server_data(),
		m_name(name),
		m_url{}
{
	STARTFUNC;
	//WL_log("name " + m_name);
	/* Create a connection.
	@param address: connection target.  This is a unix domain
	socket if there is a / in it.  If it is not a unix domain
	socket, it is a port number or service name, optionally
	prefixed with a hostname and a :.  If no hostname is present,
	localhost is used.
	*/
	open(address);
}
// }}}

void SocketBase::open(int in_fd, int out_fd)
{ // {{{
	close();
	m_in_fd = in_fd;
	m_out_fd = out_fd;
} // }}}

void SocketBase::open(URL const &address)
{ // {{{
	WL_log("connecting to " + m_url.print());

	m_url = address;
	if (m_url.unix.empty() && m_url.service.empty()) {
		m_url.service = std::move(m_url.host);
		m_url.host = "localhost";
	}

	// Set up the connection.
	if (!m_url.unix.empty()) {
		// Unix domain socket.
		m_in_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		size_t maxlen = sizeof(addr.sun_path) - 1;
		strncpy(addr.sun_path, m_url.unix.c_str(), maxlen);
		connect(m_in_fd, reinterpret_cast <sockaddr *>(&addr), sizeof(addr));
	} else {
		struct addrinfo addr_hint;
		addr_hint.ai_family = AF_UNSPEC;
		addr_hint.ai_socktype = SOCK_STREAM;
		addr_hint.ai_protocol = IPPROTO_TCP;
		addr_hint.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
		struct addrinfo *addr;
		int code = getaddrinfo(m_url.host.c_str(), m_url.service.c_str(),
				&addr_hint, &addr);
		if (code != 0) {
			std::cerr << m_url.src << std::endl;
			std::cerr << "unable to open socket: " <<
				gai_strerror(code) << std::endl;
			throw "unable to open socket";
		}
		m_in_fd = -1;
		for (auto rp = addr; rp; rp = rp->ai_next) {
			m_in_fd = socket(rp->ai_family, rp->ai_socktype,
					rp->ai_protocol);
			if (DEBUG > 3) {
				WL_log("attempt to connect; fd = " +
						std::to_string(m_in_fd));
			}
			if (m_in_fd < 0) {
				std::cerr << "unable to open socket: " <<
					strerror(errno) << std::endl;
				continue;
			}
			if (connect(m_in_fd, rp->ai_addr, rp->ai_addrlen) < 0) {
				std::cerr << "Fail: " << m_url.src << std::endl;
				if (!rp->ai_next || errno != ECONNREFUSED) {
					std::cerr <<
						"Unable to connect socket: " <<
						strerror(errno) << std::endl;
				}
				::close(m_in_fd);
				m_in_fd = -1;
				continue;
			}
			break;
		}
		freeaddrinfo(addr);
		if (m_in_fd < 0) {
			std::cerr << "unable to connect any socket" <<
				std::endl;
			throw "unable to connect any socket";
		}
	}
	m_out_fd = m_in_fd;
	int flags = fcntl(m_in_fd, F_GETFL);
	if (flags == -1 || fcntl(m_in_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		std::cerr << "unable to set socket to nonblocking: " <<
			strerror(errno) << std::endl;
		throw "unable to set socket to nonblocking";
	}
	if (m_connected_cb != nullptr) {
		assert(m_target != nullptr);
		(m_target->*m_connected_cb)();
	}
} // }}}

std::string SocketBase::close()
{ // {{{
	STARTFUNC;
	// Close the network connection.
	// @return The data that was remaining in the line buffer, if any.

	if (m_out_fd >= 0) {
		::close(m_out_fd);
		m_out_fd = -1;
	}

	if (m_in_fd < 0)
		return "";

	std::string pending = unread();
	::close(m_in_fd);
	m_in_fd = -1;
	if (m_server) {
		//server->remote_disconnect(this->server_data);
	}
	if (m_disconnected_cb != nullptr) {
		assert(m_target != nullptr);
		(m_target->*m_disconnected_cb)();
	}
	return pending;
} // }}}
// }}}

// Reading. {{{
std::string SocketBase::handle_raw_read(RawReadType callback)
{ // {{{
	STARTFUNC;
	/* Register function to be called when data is ready for reading.
	The function will be called when data is ready.  The callback
	must read the function or call unread(), or it will be called
	again after returning.
	@param callback: function to be called when data can be read.
	@param error: function to be called if there is an error on the socket.
	@return The data that was remaining in the line buffer, if any.
	*/
	if (m_raw_read_cb == callback)
		return std::string();
	if (m_in_fd < 0)
		return std::string();
	std::string ret = unread();
	WL_log("raw read");
	m_raw_read_cb = callback;
	Loop::IoRecord read_item {m_name, this, m_in_fd, POLLIN | POLLPRI,
		&SocketBase::raw_read_impl, CbType(), &SocketBase::error_impl};
	m_read_handle = m_current_loop->add_io(read_item);
	return ret;
} // }}}

void SocketBase::handle_read(ReadType callback, size_t maxsize)
{ // {{{
	STARTFUNC;
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
	if (DEBUG > 4)
		WL_log("fd:" + std::to_string(m_in_fd));
	if (m_read_cb == callback)
		return;
	if (m_in_fd < 0)
		return;
	std::string first = unread();
	WL_log("read");
	if (maxsize > 0)
		m_maxsize = maxsize;
	m_read_cb = callback;
	Loop::IoRecord read_item {m_name, this, m_in_fd, POLLIN | POLLPRI,
		&SocketBase::read_impl, CbType(), &SocketBase::error_impl};
	m_read_handle = m_current_loop->add_io(read_item);
	if (!first.empty())
		(m_target->*m_read_cb)(first);
} // }}}

void SocketBase::handle_read_lines(ReadLinesType callback, size_t maxsize)
{ // {{{
	STARTFUNC;
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
	if (m_read_lines_cb == callback)
		return;
	if (m_in_fd < 0)
		return;
	std::string first = unread();
	WL_log("read lines");
	if (maxsize > 0)
		m_maxsize = maxsize;
	m_read_lines_cb = callback;
	Loop::IoRecord read_item {m_name, this, m_in_fd, POLLIN | POLLPRI,
		&SocketBase::read_lines_impl, CbType(),
		&SocketBase::error_impl};
	m_read_handle = m_current_loop->add_io(read_item);
	if (!first.empty())
		handle_read_line_data(std::move(first));
} // }}}

std::string SocketBase::unread()
{ // {{{
	STARTFUNC;
	/* Cancel a read() or raw_read() callback.
	Cancel any read callback.
	@return Bytes left in the line buffer, if any.  The line buffer
		is cleared.
	*/
	//WL_log("unreading");
	if (m_read_handle != m_current_loop->invalid_io()) {
		WL_log("unreading active");
		m_current_loop->remove_io(m_read_handle);
		m_read_handle = m_current_loop->invalid_io();
		m_raw_read_cb = nullptr;
		m_read_cb = nullptr;
		m_read_lines_cb = nullptr;
	} else {
		WL_log("not unreading");
	}
	std::string ret = std::move(m_buffer);
	m_buffer.clear();
	return ret;
} // }}}
// }}}

// Writing. {{{
void SocketBase::send(std::string const &data)
{ // {{{
	STARTFUNC;
	/* Send data over the network.
	Send data over the network.  Block until all data is in the buffer.
	// TODO: Do not block.
	@param data: data to write.  This should be of type bytes.
	@return None.
	*/
	if (m_out_fd < 0)
		return;
	if (DEBUG > 3)
		WL_log("Sending: " + WebString(data).dump());
	size_t p = 0;
	while (m_out_fd >= 0 && p < data.size()) {
		ssize_t n = write(m_out_fd, &data[p], data.size() - p);
		if (DEBUG > 4)
			WL_log("written " + std::to_string(n) + " bytes");
		if (n <= 0) {
			std::cerr << "failed to write data to socket";
			close();
			return;
		}
		p += n;
	}
	if (m_written_cb) {
		assert(m_target);
		(m_target->*m_written_cb)();
	}
} // }}}

void SocketBase::unwritten()
{ // {{{
	// TODO: implement non-blocking write.
	m_written_cb = nullptr;
} // }}}
// }}}
// }}}

#if 0
// Network server. {{{
void ServerBase::remote_disconnect(std::list <SocketBase *>::iterator socket) { // {{{
	STARTFUNC;
	(*socket)->server = nullptr;
	remotes.erase(socket);
} // }}}

void ServerBase::open_socket(std::string const &service, int backlog) { // {{{
	STARTFUNC;
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
		strncpy(addr.sun_path, service.c_str(), sizeof(addr.sun_path) - 1);
		if (bind(fd, reinterpret_cast <sockaddr const *>(&addr), sizeof(addr)) < 0) {
			std::cerr << "unable to bind unix socket: " << strerror(errno) << std::endl;
			throw "unable to open socket";
		}
		listeners.emplace_back("unix domain", this, fd);
	}
	else {
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
			int t = 1;
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t));
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
			listeners.emplace_back("tcp/ip", this, fd);
		}
		freeaddrinfo(addr);
		if (listeners.empty()) {
			std::cerr << "unable to bind socket" << std::endl;
			throw "unable to bind socket";
		}
	}
	for (auto &s: listeners) {
		int flags = fcntl(s.fd, F_GETFL);
		if (flags == -1 || fcntl(s.fd, F_SETFL, flags | O_NONBLOCK) != 0) {
			std::cerr << "unable to set socket to nonblocking: " << strerror(errno) << std::endl;
			throw "unable to set socket to nonblocking";
		}
	}
} // }}}

void ServerBase::Listener::accept_remote() { // {{{
	STARTFUNC;
	struct sockaddr_un addr;	// Use largest struct; cast down for others.
	socklen_t addrlen = sizeof(addr);
	int new_fd = ::accept(fd, reinterpret_cast <struct sockaddr *>(&addr), &addrlen);
	if (addrlen > sizeof(addr)) {
		std::cerr << "Warning: remote address is truncated" << std::endl;
		addrlen = sizeof(addr);
	}
	// Create a generic socket; move it to the correct type later.
	Socket <SocketBase::UserBase> remote("incoming on " + m_name, new_fd, nullptr);
	server->remotes.push_back(&remote);
	remote.server = server;
	remote.server_data = --server->remotes.end();
	switch (addr.sun_family) {
	case AF_INET:
	{
		struct sockaddr_in *addr4 = reinterpret_cast <struct sockaddr_in *>(&addr);
		std::ostringstream p;
		p << addr4->sin_port;
		remote.url.service = p.str();
		char buffer[INET_ADDRSTRLEN];
		if (!inet_ntop(addr4->sin_family, &addr4->sin_addr, buffer, sizeof(buffer))) {
			std::cerr << "unable to parse IPv4 address" << std::endl;
			break;
		}
		remote.url.host = buffer;
		break;
	}
	case AF_INET6:
	{
		struct sockaddr_in6 *addr6 = reinterpret_cast <struct sockaddr_in6 *>(&addr);
		std::ostringstream p;
		p << addr6->sin6_port;
		remote.url.service = p.str();
		char buffer[INET6_ADDRSTRLEN];
		if (!inet_ntop(addr6->sin6_family, &addr6->sin6_addr, buffer, sizeof(buffer))) {
			std::cerr << "unable to parse IPv6 address" << std::endl;
			break;
		}
		remote.url.host = buffer;
		break;
	}
	case AF_UNIX:
	{
		addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
		remote.url.unix = addr.sun_path;
		break;
	}
	default:
		std::cerr << "unknown address family for remote socket; not reading remote details." << std::endl;
		break;
	}
	(reinterpret_cast <ServerBase::OwnerBase *>(server->owner)->*(server->create_cb))(&remote);
} // }}}

ServerBase::ServerBase( // {{{
		std::string const &service,
		OwnerBase *owner,
		CreateType create,
		ClosedType closed,
		ErrorType error,
		Loop *loop,
		int backlog
	) :
		listenloop(Loop::get(loop)),
		listeners(),
		active_backlog(backlog),
		remotes(),
		owner(owner),
		create_cb(create),
		closed_cb(closed),
		error_cb(error)
{
	STARTFUNC;
	open_socket(service, backlog);
	for (auto i = listeners.begin(); i != listeners.end(); ++i) {
		i->socket.raw_read(&Listener::accept_remote);
	}
} // }}}

/*
ServerBase::ServerBase(ServerBase &&other) : // {{{
	listenloop(other.listenloop),
	listeners(std::move(other.listeners)),
	active_backlog(other.active_backlog),
	remotes(std::move(other.remotes)),
	owner(other.owner),
	create_cb(other.create_cb),
	closed_cb(other.closed_cb),
	error_cb(other.error_cb)
{
	STARTFUNC;
	for (auto &l: listeners)
		l.server = this;
} // }}}

ServerBase &ServerBase::operator=(ServerBase &&other) { // {{{
	STARTFUNC;
	listenloop = other.listenloop;
	listeners = std::move(other.listeners);
	active_backlog = other.active_backlog;
	remotes = std::move(other.remotes);
	owner = other.owner;
	create_cb = other.create_cb;
	closed_cb = other.closed_cb;
	error_cb = other.error_cb;
	for (auto &l: listeners)
		l.server = this;
	return *this;
} // }}}
*/

void ServerBase::close() { // {{{
	STARTFUNC;
	while (!remotes.empty())
		remotes.back()->close();
	while (!listeners.empty()) {
		listeners.back().socket.close();
		listeners.pop_back();
	}
} // }}}
// }}}
#endif

}

// vim: set fileencoding=utf-8 foldmethod=marker :
