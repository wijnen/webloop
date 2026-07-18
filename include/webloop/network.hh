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
#include <format>
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

// At application level, a class is made which wraps the socket.
// It registers member functions that are called when events happen.
// Events that can be registered are:
// - Data is ready for reading. (Or for servers, accepting.): raw_read
// - Data has been read: read
// - A line of data has been read: read_done
// - The socket is ready to write data: write
// - Data has been written: write_done
// - The socket was disconnected: disconnected
// This happens through a template class that is used as the base class.
// The socket is not a template and it can be transferred to another
// application class object, even one of another type.

Classes:
- SocketBase: dynamically allocated; contains underlying logic.
	Pointers for internals are to this object.
- Socket <T>: base class for T; uses T as Cb target type.
	Contains a pointer to SocketBase.
	The SocketBase can be moved to a different object (of a different T).
// }}} */

class ServerBase;

class SocketBase { // {{{
	friend class ServerBase;
public:
	class Base {};	// Fake class for cb target object.
	typedef void (Base::*RawReadType)();
	typedef void (Base::*ReadType)(std::string &buffer);
	typedef void (Base::*ReadLinesType)(std::string const &line);
	typedef void (Base::*WrittenType)();
	typedef void (Base::*ConnectedType)();
	typedef void (Base::*DisconnectedType)();
	typedef void (Base::*ErrorType)(std::string const &message);
private:

	int m_in_fd;
	int m_out_fd;
	size_t m_maxsize;	// Per read operation.
	Loop *m_current_loop;
	Loop::IoHandle m_read_handle;	// For raw or normal read.

	Base *m_target;
	RawReadType m_raw_read_cb;
	ReadType m_read_cb;
	ReadLinesType m_read_lines_cb;
	WrittenType m_written_cb;
	ConnectedType m_connected_cb;
	DisconnectedType m_disconnected_cb;
	ErrorType m_error_cb;

	// Pending received data.
	std::string m_buffer;

	// For server sockets: the Server that accepted them;
	// for client sockets: nullptr.
	std::shared_ptr <ServerBase> m_server;

	// For server sockets: iterator into Server's socket list.
	std::list <std::shared_ptr <SocketBase> >::iterator m_server_data;

	std::string m_name; // for debugging.

	// Functions that are registered in the Loop.
	typedef bool (SocketBase::*CbType)();
	bool raw_read_impl();
	bool read_impl();
	bool read_lines_impl();

	// Pass errors to user object and close the socket.
	bool error_impl() {
		m_buffer += unread();
		if (m_error_cb != nullptr)
			(m_target->*(m_error_cb))(m_buffer);
		close();
		return false;
	}

	bool handle_read_line_data(std::string &&data);

public:
	void set_target(Base *target)
	{
		if (m_target) {
			if (m_disconnected_cb != nullptr)
				(m_target->*m_disconnected_cb)();
			unread();
			unwritten();
		}
		WL_log(std::format("Setting target {}", (bool)target));
		m_target = target;
	}

	std::string handle_raw_read(RawReadType callback);
	void handle_read(ReadType callback, size_t maxsize = 0);
	void handle_read_lines(ReadLinesType callback, size_t maxsize = 0);
	void handle_written(WrittenType callback) { m_written_cb = callback; }
	void handle_connected(ConnectedType callback)
	{ m_connected_cb = callback; }
	void handle_disconnected(DisconnectedType callback)
	{ m_disconnected_cb = callback; }
	void handle_error(ErrorType callback) { m_error_cb = callback; }
	std::string unread();
	void unwritten();

	// For debugging.
	int get_in_fd() const { return m_in_fd; }
	int get_out_fd() const { return m_out_fd; }

	// Read only address components; these are filled from address
	// in the constructor or open().
	URL m_url;

	// Constructors.
	SocketBase(std::string const &name, Loop *loop = nullptr);
	SocketBase(std::string const &name, URL const &url,
			Loop *loop = nullptr);

	// Open a connection.
	void open(int in_fd, int out_fd);
	void open(URL const &url);

	// Close the connection.
	std::string close();

	// Retrieve a string of data.
	std::string recv();

	void send(std::string const &data);

	// Check if socket is connected.
	operator bool() const { return m_in_fd >= 0; }

	// Set and get name.
	constexpr std::string const &get_name() const { return m_name; }
	void set_name(std::string const &n)
	{
		m_name = n;
		if (m_read_handle >= 0)
			m_current_loop->update_name(m_read_handle, n);
	}
}; // }}}

template <class TargetType>
class Socket { // {{{
public:
	template <class T> friend class Socket;
	typedef void (TargetType::*RawReadType)();
	typedef void (TargetType::*ReadType)(std::string &buffer);
	typedef void (TargetType::*ReadLinesType)(std::string const &line);
	typedef void (TargetType::*WrittenType)();
	typedef void (TargetType::*ConnectedType)();
	typedef void (TargetType::*DisconnectedType)();
	typedef void (TargetType::*ErrorType)(std::string const &message);

private:
	SocketBase *m_base;
	RawReadType m_raw_read_cb;
	ReadType m_read_cb;
	ReadLinesType m_read_lines_cb;
	WrittenType m_written_cb;
	ConnectedType m_connected_cb;
	DisconnectedType m_disconnected_cb;
	ErrorType m_error_cb;

	void sync_cbs();

public:
	// For sending.
	class Iterator { // {{{
	public:
		using value_type = std::string;
		using difference_type = std::ptrdiff_t;

	private:
		SocketBase *m_parent;
		value_type m_value;

	public:
		Iterator(SocketBase *parent) : m_parent(parent), m_value{} {}

		Iterator &operator++()
		{
			m_parent->send(m_value);
			m_value.clear();
			return *this;
		}
		Iterator operator++(int) { return ++*this; }
		std::string &operator*() { return m_value; }
	}; // }}}
	Iterator send() { assert(m_base); return Iterator(m_base); }

	template <class ...Args>
	void send(std::string_view const &fmt, Args &&...args)
	{ std::vformat_to(send(), fmt, std::make_format_args(args...)); }

	Socket()
		:
			m_base(nullptr),
			m_raw_read_cb(nullptr),
			m_read_cb(nullptr),
			m_read_lines_cb(nullptr),
			m_written_cb(nullptr),
			m_connected_cb(nullptr),
			m_disconnected_cb(nullptr),
			m_error_cb(nullptr)
	{ STARTFUNC; }

	Socket(SocketBase *base)
		:
			m_base(base),
			m_raw_read_cb(nullptr),
			m_read_cb(nullptr),
			m_read_lines_cb(nullptr),
			m_written_cb(nullptr),
			m_connected_cb(nullptr),
			m_disconnected_cb(nullptr),
			m_error_cb(nullptr)
	{
		STARTFUNC;
		auto target = reinterpret_cast <SocketBase::Base *> (this);
		m_base->set_target(target);
		sync_cbs();
	}

	Socket(URL const &url, std::string const &name = "socketbase")
		:
			m_base(new SocketBase(name, url)),
			m_raw_read_cb(nullptr),
			m_read_cb(nullptr),
			m_read_lines_cb(nullptr),
			m_written_cb(nullptr),
			m_connected_cb(nullptr),
			m_disconnected_cb(nullptr),
			m_error_cb(nullptr)
		{ STARTFUNC; }

	void open(URL const &url, std::string const &name = "socketbase")
	{
		STARTFUNC;
		if (!m_base) {
			m_base = new SocketBase(name, url);
			m_base->set_target
				(reinterpret_cast <SocketBase::Base *> (this));
		} else {
			m_base->open(url);
		}
		sync_cbs();
	}

	template <class T>
	void transfer_to(Socket <T> &target)
	{
		STARTFUNC;
		assert(m_base != nullptr);
		auto other = reinterpret_cast <SocketBase::Base *> (&target);
		m_base->set_target(other);
		target.m_base = m_base;
		m_base = nullptr;
		target.sync_cbs();
	}

	std::string close()
	{
		if (!m_base)
			return std::string();
		auto ret = m_base->close();
		delete m_base;
		m_base = nullptr;
		return ret;
	}

	~Socket() { close(); }

	std::string handle_raw_read(RawReadType callback)
	{
		auto ret = unread();
		m_raw_read_cb = callback;
		sync_cbs();
		return ret;
	}
	void handle_read(ReadType callback)
	{ unread(); m_read_cb = callback; sync_cbs(); }
	void handle_read_lines(ReadLinesType callback)
	{ unread(); m_read_lines_cb = callback; sync_cbs(); }
	void handle_written(WrittenType callback)
	{ unwritten(); m_written_cb = callback; sync_cbs(); }
	void handle_connected(ConnectedType callback)
	{ m_connected_cb = callback; sync_cbs(); }
	void handle_disconnected(DisconnectedType callback)
	{ m_disconnected_cb = callback; sync_cbs(); }
	void handle_error(ErrorType callback)
	{ m_error_cb = callback; sync_cbs(); }
	std::string unread()
	{
		std::string ret;
		if (m_base)
			ret = m_base->unread();
		m_raw_read_cb = nullptr;
		m_read_cb = nullptr;
		m_read_lines_cb = nullptr;
		return ret;
	}
	void unwritten()
	{
		if (m_base)
			m_base->unwritten();
		m_written_cb = nullptr;
	}
}; // }}}

template <class TargetType>
void Socket <TargetType>::sync_cbs()
{
	if (!m_base)
		return;
	auto raw_read_cb =
		reinterpret_cast <SocketBase::RawReadType> (m_raw_read_cb);
	m_base->handle_raw_read(raw_read_cb);
	auto read_cb = reinterpret_cast <SocketBase::ReadType> (m_read_cb);
	m_base->handle_read(read_cb);
	auto read_lines_cb =
		reinterpret_cast <SocketBase::ReadLinesType> (m_read_lines_cb);
	m_base->handle_read_lines(read_lines_cb);
	auto written_cb =
		reinterpret_cast <SocketBase::WrittenType> (m_written_cb);
	m_base->handle_written(written_cb);
	auto connected_cb =
		reinterpret_cast <SocketBase::ConnectedType> (m_connected_cb);
	m_base->handle_connected(connected_cb);
	auto disconnected_cb = reinterpret_cast <SocketBase::DisconnectedType>
		(m_disconnected_cb);
	m_base->handle_disconnected(disconnected_cb);
	auto error_cb =
		reinterpret_cast <SocketBase::ErrorType> (m_error_cb);
	m_base->handle_error(error_cb);
}

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
		ServerBase *m_server;
		int m_fd;
		Socket <Listener> socket;
		std::string m_name;
		Listener(std::string const &name, ServerBase *server, int fd) : m_server(server), m_fd(fd), socket("server listener " + m_name, fd, this), m_name(name) {}
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
	ErrorType m_error_cb;

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
	void set_error_cb(ErrorCb callback) { m_error_cb = reinterpret_cast <ErrorType>(callback); }
}; // }}}
#endif

}

#endif

// vim: set fileencoding=utf-8 foldmethod=marker :
