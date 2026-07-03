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
class SocketBase;

class SocketSettingsBase { // {{{
	friend class SocketBase;

public:

	// Callback types.
	typedef void (SocketSettingsBase::*RawReadType)();
	typedef void (SocketSettingsBase::*ReadType)(std::string &data);
	typedef void (SocketSettingsBase::*ReadLinesType)
		(std::string const &data);
	typedef void (SocketSettingsBase::*DisconnectType)();
	typedef void (SocketSettingsBase::*ErrorType)
		(std::string const &message);

protected:

	SocketBase *parent;

	// Pretend to use this class for callbacks.
	// SocketSettings makes sure actual callbacks match target object.
	SocketSettingsBase *target;

	// Read callbacks.
	RawReadType rawread_cb;
	ReadType read_cb;
	ReadLinesType read_lines_cb;
	// Callback when socket is disconnected.
	DisconnectType disconnect_cb;
	// Error callback.
	ErrorType error_cb;

	SocketSettingsBase(SocketSettingsBase *target_obj,
			DisconnectType disconnect, ErrorType error)
	: target(target_obj), rawread_cb(nullptr), read_cb(nullptr),
		read_lines_cb(nullptr), disconnect_cb(disconnect),
		error_cb(error)
	{
		STARTFUNC;
	}
}; // }}}

template <class UserType>
class SocketSettings : public SocketSettingsBase { // {{{

public:
	typedef void (UserType::*RawReadCb)();
	typedef void (UserType::*ReadCb)(std::string &buffer);
	typedef void (UserType::*ReadLinesCb)(std::string const &buffer);
	typedef void (UserType::*DisconnectCb)();
	typedef void (UserType::*ErrorCb)(std::string const &message);

	// Read event scheduling.
	void set_rawread(RawReadCb callback)
	{
		STARTFUNC;
		rawread_cb = reinterpret_cast <RawReadType> (callback);
	}
	void set_read(ReadCb callback)
	{
		STARTFUNC;
		read_cb = reinterpret_cast <ReadType>(callback);
	}
	void set_read_lines(ReadLinesCb callback)
	{
		STARTFUNC;
		read_lines_cb = reinterpret_cast <ReadLinesType> (callback);
	}
	// Other events.
	void set_disconnect_cb(DisconnectCb callback)
	{
		STARTFUNC;
		disconnect_cb = reinterpret_cast <DisconnectType>(callback);
	}
	void set_error_cb(ErrorCb callback)
	{
		STARTFUNC;
		error_cb = reinterpret_cast <ErrorType>(callback);
	}

	SocketSettings(UserType *target,
			RawReadCb rawread = nullptr, ReadCb read = nullptr,
			ReadLinesCb read_lines = nullptr,
			DisconnectCb disconnect = nullptr,
			ErrorCb error = nullptr)
		: SocketSettingsBase(target,
				reinterpret_cast <RawReadType> (rawread),
				reinterpret_cast <ReadType> (read),
				reinterpret_cast <ReadLinesType> (read_lines),
				reinterpret_cast <DisconnectType> (disconnect),
				reinterpret_cast <ErrorType> (error))
	{
		STARTFUNC;
	}
}; // }}}

class SocketBase { // {{{
	friend class ServerBase;
private:

	int fd;
	size_t maxsize;	// For read operations.
	Loop *current_loop;
	Loop::IoHandle read_handle;

	// Pending received data.
	std::string buffer;

	// For server sockets: the Server that accepted them;
	// for client sockets: nullptr.
	std::shared_ptr <ServerBase> server;

	// For server sockets: iterator into Server's socket list.
	std::list <std::shared_ptr <SocketBase> >::iterator server_data;

	std::string name; // for debugging.

	// Functions that are registered in the Loop.
	typedef bool (SocketBase::*CbType)();
	bool rawread_impl();
	bool read_impl();
	bool read_lines_impl();

	// Pass errors to user object and close the socket.
	bool error_impl() {
		buffer += unread();
		if (my_settings.error_cb != nullptr) {
			auto &target = my_settings.target;
			auto &cb = my_settings.error_cb;
			(target->*(cb))("error on socket");
		}
		close();
		return false;
	}

	bool handle_read_line_data(std::string &&data);

protected:
	SocketSettingsBase my_settings;

	std::string set_rawread(SocketSettingsBase::RawReadType callback);
	std::string set_read(SocketSettingsBase::ReadType callback);
	std::string set_read_lines(SocketSettingsBase::ReadLinesType callback);

	// For debugging.
	int get_fd() const { return fd; }

public:

	// Read only address components; these are filled from address
	// in the constructor.
	URL url;

	// Constructor.
	SocketBase(std::string const &name, int fd, URL const &url,
			SocketSettingsBase *settings, Loop *loop);

	// Close the connection.
	std::string close();

	// Retrieve a string of data.
	std::string recv();

	// Write data.
	// TODO: Sending data on a socket is currently blocking;
	// TODO: it should instead be possible as coroutine and with callback.
	void send(std::string const &data);

	// Read event scheduling.
	std::string unread();

	// Check if socket is connected.
	operator bool() const { return fd >= 0; }

	// Set and get name.
	constexpr std::string const &get_name() const { return name; }
	void set_name(std::string const &n)
	{
		name = n;
		if (read_handle >= 0)
			current_loop->update_name(read_handle, n);
	}
}; // }}}

// Socket <UserType> {{{
template <class UserType>
class Socket : public SocketBase { // {{{
public:

	// Create client socket, which connects to server.
	Socket(std::string const &name, std::string const &address,
			SocketSettings <UserType> const *settings = nullptr,
			Loop *loop = nullptr);
	// Use existing socket (or other fd). This is used by Server to handle
	// accepted requests, and can be used to fake sockets.
	Socket(std::string const &name, int fd = -1, 
			SocketSettings <UserType> const *settings = nullptr,
			Loop *loop = nullptr)
		:
			SocketBase(name, fd, URL(), settings, Loop::get(loop))
			{ STARTFUNC; }

	SocketSettings <UserType> &settings()
	{ return reinterpret_cast <SocketSettings <UserType> &> (my_settings); }

}; // }}}

template <class UserType>
Socket <UserType>::Socket(std::string const &name, std::string const &address,
		SocketSettings <UserType> const *settings, Loop *loop) // {{{
	:
		SocketBase(name, -1, address,
		reinterpret_cast <SocketSettingsBase *>(settings),
		Loop::get(loop))
{
	STARTFUNC;
} // }}}

// }}}

#if 0
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

	void open_socket(std::string const &service, int backlog);
	void remote_disconnect(std::list <SocketBase *>::iterator socket);
protected:
	OwnerBase *owner;
	// Callbacks.
	CreateType create_cb;
	ClosedType closed_cb;
	ErrorType error_cb;

public:
	ServerBase(std::string const &service, OwnerBase *owner, CreateType create, ClosedType closed, ErrorType error, Loop *loop, int backlog);
	void close();
	~ServerBase() { close(); }

	// Move support.
	//ServerBase(ServerBase &&other);
	//ServerBase &operator=(ServerBase &&other);

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
	//Server(Server <UserType, OwnerType> &&other) = default;
	//Server &operator=(Server <UserType, OwnerType> &&other) = default;

	// Move server to new target class.
	/*
	template <class OtherUser, class OtherOwner> explicit Server(Server <OtherUser, OtherOwner> &&other, OwnerType *new_owner, CreateCb create, ClosedCb closed, ErrorCb error) :
		ServerBase(std::move(other))
	{
		owner = reinterpret_cast <OwnerBase *>(new_owner);
		set_create_cb(create);
		set_closed_cb(closed);
		set_error_cb(error);
	}
	*/

	// Set callbacks.
	void set_create_cb(CreateCb callback) { create_cb = reinterpret_cast <CreateType>(callback); }
	void set_closed_cb(ClosedCb callback) { closed_cb = reinterpret_cast <ClosedType>(callback); }
	void set_error_cb(ErrorCb callback) { error_cb = reinterpret_cast <ErrorType>(callback); }
}; // }}}
#endif

}

#endif

// vim: set fileencoding=utf-8 foldmethod=marker :
