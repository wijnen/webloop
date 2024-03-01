#ifndef _NETWORK_HH
#define _NETWORK_HH

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
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "tools.hh"
#include "loop.hh"
#include "url.hh"

using namespace std::literals;
// }}}

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

class ServerBase;

class SocketBase { // {{{
	friend class ServerBase;
public:
	struct UserBase {};	// Not actually a base for the user class, this class just pretends that it is.
	typedef void (UserBase::*RawReadType)();
	typedef void (UserBase::*ReadType)(std::string &data);
	typedef void (UserBase::*ReadLinesType)(std::string const &data);
	typedef void (UserBase::*DisconnectType)();
	typedef void (UserBase::*ErrorType)(std::string const &message);
private:

	int fd;
	size_t maxsize;	// For read operations.
	Loop *current_loop;
	Loop::IoHandle read_handle;

	// Pending received data.
	std::string buffer;

	// For server sockets: the Server that accepted them; for client sockets: nullptr.
	ServerBase *server;
	// For server sockets: iterator into Server's socket list.
	std::list <SocketBase *>::iterator server_data;

	std::string name; // for debugging.

	// Functions that are registered in the Loop.
	typedef bool (SocketBase::*CbType)();
	bool rawread_impl();
	bool read_impl();
	bool read_lines_impl();

	// Pass errors to user object and close the socket.
	bool error_impl() {
		buffer += unread();
		if (error_cb != nullptr)
			(user->*error_cb)("error on socket");
		close();
		return false;
	}

	bool handle_read_line_data(std::string &&data);
	void finish_move(SocketBase &&other);

protected:
	// User object, on which callbacks are called.
	UserBase *user;

	// Functions that register the above functions (called through a wrapper that uses that UserType-bound callbacks).
	std::string rawread_base(RawReadType callback);
	void read_base(ReadType callback);
	void read_lines_base(ReadLinesType callback);

	// Read callbacks.
	RawReadType rawread_cb;
	ReadType read_cb;
	ReadLinesType read_lines_cb;
	// Callback when socket is disconnected.
	DisconnectType disconnect_cb;
	// Error callback.
	ErrorType error_cb;

	// For debugging.
	int get_fd() const { return fd; }

public:

	// Read only address components; these are filled from address in the constructor.
	URL url;

	// Constructor.
	SocketBase(std::string const &name, int fd, URL const &url, UserBase *user, Loop *loop);

	// Move support.
	SocketBase(std::string const &name = "unconnected");
	SocketBase(SocketBase &&other);
	SocketBase &operator=(SocketBase &&other);

	// Close the connection.
	std::string close();

	// Retrieve a string of data.
	std::string recv();

	// Write data.
	// TODO: Sending data on a socket is currently blocking; it should instead be possible as coroutine and with callback.
	void send(std::string const &data);

	// Read event scheduling.
	std::string unread();

	// Check if socket is connected.
	operator bool() const { return fd >= 0; }

	// Set and get name.
	constexpr std::string const &get_name() const { return name; }
	void set_name(std::string const &n) { name = n; if (read_handle >= 0) current_loop->update_name(read_handle, n); }
}; // }}}

// Socket <UserType> {{{
template <class UserType>
class Socket : public SocketBase { // {{{
public:
	typedef void (UserType::*RawReadCb)();
	typedef void (UserType::*ReadCb)(std::string &buffer);
	typedef void (UserType::*ReadLinesCb)(std::string const &buffer);
	typedef void (UserType::*DisconnectCb)();
	typedef void (UserType::*ErrorCb)(std::string const &message);

	// Create client socket, which connects to server.
	Socket(std::string const &name, std::string const &address, UserType *user = nullptr);
	// Use existing socket (or other fd). This is used by Server to handle accepted requests, and can be used to fake sockets.
	Socket(std::string const &name, int fd = -1, UserType *user = nullptr, Loop *loop = nullptr)	 // Use an fd as a "socket", so it can use the functionality of this class. {{{
		:
			SocketBase(name, fd, URL(), reinterpret_cast <SocketBase::UserBase *>(user), Loop::get(loop))
			{ STARTFUNC; } // }}}

	// Move support.
	Socket(std::string const &name = "unconnected") : SocketBase(name) { STARTFUNC; }
	Socket(Socket <UserType> &&other) : SocketBase(std::move(other)) { STARTFUNC; }
	Socket &operator=(Socket <UserType> &&other) { STARTFUNC; *dynamic_cast <SocketBase *>(this) = std::move(other); return *this; }
	void update_user(UserType *new_user) { user = reinterpret_cast <UserBase *>(new_user); }
	// Move socket to new target class.
	template <class OtherType> explicit Socket(Socket <OtherType> &&other, UserType *new_user);

	// Read event scheduling.
	std::string rawread(RawReadCb callback) { STARTFUNC; return rawread_base(reinterpret_cast <RawReadType>(callback)); }
	void read(ReadCb callback) { STARTFUNC; read_base(reinterpret_cast <ReadType>(callback)); }
	void read_lines(ReadLinesCb callback) { STARTFUNC; read_lines_base(reinterpret_cast <ReadLinesType>(callback)); }
	// Other events.
	void set_disconnect_cb(DisconnectCb callback) { STARTFUNC; disconnect_cb = reinterpret_cast <DisconnectType>(callback); }
	void set_error_cb(ErrorCb callback) { STARTFUNC; error_cb = reinterpret_cast <ErrorType>(callback); }
}; // }}}

template <class UserType>
Socket <UserType>::Socket(std::string const &name, std::string const &address, UserType *user) // {{{
	:
		SocketBase(name, -1, address, reinterpret_cast <SocketBase::UserBase *>(user), Loop::get())
{
	STARTFUNC;
} // }}}

template <class UserType> template <class OtherType>
Socket <UserType>::Socket(Socket <OtherType> &&other, UserType *new_user) : // {{{
		SocketBase(std::move(other))
{
	STARTFUNC;
	unread();
	user = reinterpret_cast <SocketBase::UserBase *>(new_user);
} // }}}
// }}}

class ServerBase { // {{{
	friend class SocketBase;
public:
	struct OwnerBase {};	// Not actually used as base class for owner, but this class pretends that it is, so it can call pointers to members.
	typedef void (OwnerBase::*CreateType)(SocketBase *socket);
	typedef void (OwnerBase::*ClosedType)();
	typedef void (OwnerBase::*ErrorType)(std::string const &message);
private:
	struct Listener {	// Data for listening socket; used as user for poll events.
		ServerBase *server;
		int fd;
		Socket <Listener> socket;
		std::string name;
		Listener(std::string const &name, ServerBase *server, int fd) : server(server), fd(fd), socket("server listener " + name, fd, this), name(name) {}
		void accept_remote();
	};
	Loop *listenloop;
	std::list <Listener> listeners;
	int active_backlog;
	std::list <SocketBase *> remotes;

	OwnerBase *owner;
	void open_socket(std::string const &service, int backlog);
	void remote_disconnect(std::list <SocketBase *>::iterator socket);
protected:
	// Callbacks.
	CreateType create_cb;
	ClosedType closed_cb;
	ErrorType error_cb;

public:
	ServerBase(std::string const &service, OwnerBase *owner, CreateType create, ClosedType closed, ErrorType error, Loop *loop, int backlog);
	void close();
	~ServerBase() { close(); }

	// Move support.
	ServerBase(ServerBase &&other);
	ServerBase &operator=(ServerBase &&other);

	void set_loop(Loop *loop);
	void set_backlog(int backlog);
}; // }}}

template <class UserType, class OwnerType>
class Server : public ServerBase { // {{{
	friend class SocketBase;
public:
	typedef void (OwnerType::*CreateCb)(Socket <UserType> *socket);
	typedef void (OwnerType::*ClosedCb)();
	typedef void (OwnerType::*ErrorCb)(std::string const &message);

	Server( // {{{
			std::string const &service,
			OwnerType *owner,
			CreateCb create,
			ClosedCb closed = nullptr,
			ErrorCb error = nullptr,
			Loop *loop = nullptr,
			int backlog = 5)
		: ServerBase (service, reinterpret_cast <OwnerBase *>(owner), reinterpret_cast <ServerBase::CreateType>(create), reinterpret_cast <ServerBase::ClosedType>(closed), reinterpret_cast <ServerBase::ErrorType>(error), loop, backlog) {}
	// }}}

	// Move support.
	Server(Server <UserType, OwnerType> &&other) : ServerBase(std::move(other)) {}
	Server &operator=(Server <UserType, OwnerType> &&other) { *dynamic_cast <ServerBase *>(this) = *dynamic_cast <ServerBase *>(other); }
	// Move server to new target class.
	template <class OtherUser, class OtherOwner> explicit Server(Server <OtherUser, OtherOwner> &&other, OwnerType *new_owner, CreateCb create, ClosedCb closed, ErrorCb error) :
		ServerBase(std::move(other))
	{
		owner = new_owner;
		create_cb = create;
		closed_cb = closed;
		error_cb = error;
	}

	// Set callbacks.
	void set_create_cb(CreateCb callback) { create_cb = reinterpret_cast <CreateType>(callback); }
	void set_closed_cb(ClosedCb callback) { closed_cb = reinterpret_cast <ClosedType>(callback); }
	void set_error_cb(ErrorCb callback) { error_cb = reinterpret_cast <ErrorType>(callback); }
}; // }}}

}

#endif

// vim: set fileencoding=utf-8 foldmethod=marker :
