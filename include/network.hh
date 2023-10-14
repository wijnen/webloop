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

template <class UserType, class OwnerType> class Server;
template <class UserType> class ServerGeneric;
class ServerBase;

class SocketBase { // {{{
	friend class ServerBase;
public:
	struct UserBase {};	// Not actually a base for the user class, this class just pretends that it is.
private:

	int fd;
	size_t mymaxsize;	// For read operations.
	Loop *current_loop;
	Loop::IoHandle read_handle;

	// Pending received data.
	std::string buffer;

	// For server sockets: the Server that accepted them; for client sockets: nullptr.
	ServerBase *server;
	// For server sockets: iterator into Server's socket list.
	std::list <SocketBase *>::iterator server_data;

	// Callback when socket is disconnected.
	std::string (UserBase::*disconnect_cb)(std::string const &pending);
	// User object, on which callbacks are called.
	UserBase *user;

public:
	// Read only address components; these are filled from address in the constructor.
	URL url;

	// Constructor.
	template <class UserType>
	SocketBase(int fd, URL &&url, UserType *user) : fd(fd), mymaxsize(4096), current_loop(Loop::get(nullptr)), read_handle(-1), buffer(), server(nullptr), server_data(), disconnect_cb(nullptr), user(reinterpret_cast <UserBase *>(user)), url(std::move(url)) {}

	// Move support.
	SocketBase(SocketBase &&other);
	SocketBase &operator=(SocketBase &&other);

	// Close the connection.
	std::string close();

	// Retrieve a string of data. TODO: The socket should be set to non-blocking, so this can be called when no data is waiting.
	std::string recv();

	// Write data. TODO: This is currently blocking; it should instead be possible as coroutine and with callback.
	void send(std::string const &data);
	void sendline(std::string const &data);

	// Read event scheduling.
	std::string unread();
}; // }}}

template <class UserType>
class Socket : public SocketBase { // {{{
public:
	typedef void (UserType::*RawReadCb)();
	typedef void (UserType::*ReadCb)(std::string &buffer);
	typedef void (UserType::*ReadLinesCb)(std::string &&buffer);
	typedef void (UserType::*ErrorCb)(std::string const &final_data);
private:
	// Callbacks, stored for move operations.
	RawReadCb rawread_cb;
	ReadCb read_cb;
	ReadLinesCb read_lines_cb;
	ErrorCb error_cb;

	// Functions that are registered in the Loop.
	bool rawread_impl();
	bool read_impl();
	bool read_lines_impl();

	// Pass errors to user object and close the socket.
	bool error_impl() {
		buffer += unread();
		if (error_cb)
			(user->*error_cb)(buffer);
		close();
		return false;
	}

	bool handle_read_line_data(std::string &&data);
public:
	// Create client socket, which connects to server.
	Socket(std::string const &address, UserType *user = nullptr);
	// Use existing socket (or other fd). This is used by Server to handle accepted requests, and can be used to fake sockets.
	Socket(int fd = -1, UserType *user = nullptr)	 // Use an fd as a "socket", so it can use the functionality of this class. {{{
		:
			SocketBase(fd, URL(), user),
			read_cb(nullptr)
			{} // }}}

	// Move support.
	Socket(Socket <UserType> &&other);
	Socket &operator=(Socket <UserType> &&other);
	template <class OtherType> explicit Socket(Socket <OtherType> &&other); // Move socket to new target class.

	// Read event scheduling.
	std::string rawread(void (UserType::*cb)());
	void read(ReadCb callback, size_t maxsize = 4096);
	void read_lines(ReadLinesCb callback, size_t maxsize = 4096);
}; // }}}

class ServerBase { // {{{
	friend class SocketBase;
public:
	struct OwnerBase {};	// Not actually used as base class for owner, but this class pretends that it is, so it can call pointers to members.
	typedef void (OwnerBase::*ErrorType)(std::string const &message);
private:
	struct Listener {	// Data for listening socket; used as user for poll events.
		ServerBase *server;
		int fd;
		Socket <Listener> socket;
		Listener(ServerBase *server, int fd) : server(server), fd(fd), socket(fd, this) {}
		bool accept_remote();
	};
	Loop *listenloop;
	std::list <Listener> listeners;
	int backlog;
	std::list <SocketBase *> remotes;

	OwnerBase *owner;
	void (OwnerBase::*error)(std::string const &message);
	void open_socket(std::string const &service, int backlog);
	void remote_disconnect(std::list <SocketBase *>::iterator socket);
public:
	ServerBase(std::string const &service, void *owner, ErrorType error, Loop *loop = nullptr, int backlog = 5);
	void close();
	~ServerBase() { close(); }
}; // }}}

template <class UserType>
class ServerGeneric : public ServerBase {
	typedef UserType *(OwnerBase::*CreateType)(Socket <UserType> &&socket);
	CreateType create;
public:
	template <class OwnerType>
	ServerGeneric(std::string const &service, OwnerType *owner, UserType *(OwnerType::*create)(Socket <UserType> &&socket), void (OwnerType::*error)(std::string const &message), Loop *loop, int backlog) : ServerBase(service, owner, reinterpret_cast <ErrorType>(error), loop, backlog), create(reinterpret_cast <CreateType>(create)) {}
};

template <class UserType, class OwnerType>
class Server : public ServerGeneric <UserType> { // {{{
	friend class Socket <UserType>;
public:
	Server( // {{{
			std::string const &service,
			OwnerType *owner,
			UserType *(OwnerType::*create)(Socket <UserType> &&socket),
			void (OwnerType::*error)(std::string const &message) = nullptr,
			Loop *loop = nullptr,
			int backlog = 5)
		: ServerGeneric <UserType> (service, owner, reinterpret_cast <ServerGeneric <UserType>::CreateType>(create), reinterpret_cast <ServerGeneric <UserType>::ErrorType>(error), loop, backlog) {}
	// }}}
}; // }}}

#endif

// vim: set fileencoding=utf-8 foldmethod=marker :
