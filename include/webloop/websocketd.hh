/* {{{ Copyright 2013-2023 Bas Wijnen <wijnen@debian.org>
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or(at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * }}} */

#ifndef _WEBSOCKETD_HH
#define _WEBSOCKETD_HH

/** @file {{{
This file defines 3 template classes:
- Websocket, for http connections that use the Websocket protocol.
- Httpd, for Http servers that allow Websockets to be served (as well as static and dynamic pages).
- RPC, for Websockets that are used with a protocol to make remote procedure calls.

Note that Httpd is not optimized for high traffic. If you need that, use
something like Apache to handle all the other content and set up a virtual
proxy to this server just for the websocket.
}}} */

// includes.  {{{
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>
#include <filesystem>
#include "network.hh"
#include "webobject.hh"
#include "coroutine.hh"
#include "url.hh"
#include "tools.hh"
#include "loop.hh"
#include "fhs.hh"
// }}}

namespace Webloop {

/// This type is used for function arguments in RPC calls.
typedef std::shared_ptr <WebVector> Args;

/// This type is used for keyword function arguments in RPC calls.
typedef std::shared_ptr <WebMap> KwArgs;

/// Http response codes.
extern std::map <int, char const *> http_response;

// Websockets. {{{
/// This class implements the Websocket protocol over a Webloop::Socket object.
template <class UserType>
class Websocket { // {{{
public:
	/// This is the function signature for the callback to inform the owner of a new packet.
	typedef void (UserType::*Receiver)(std::string const &data);

	/// This is the function signature for the callback to inform the owner that the connection is lost.
	typedef void (UserType::*DisconnectCb)();

	/// This is the function signature for the callback to inform the owner that there was an error.
	typedef void (UserType::*ErrorCb)(std::string const &message);
private:
	std::string buffer;	// Data that is currently being received.
	std::string fragments;	// Fragments for packet that is being received; data is added to it when new packets arrive.
	Loop::TimeoutHandle keepalive_handle;
	bool is_closed;		// true if the connection is not open.
	bool pong_seen;		// true if a pong was seen since last ping command.
	uint8_t current_opcode;
	Receiver receiver;	// callback for new data packets.
	bool send_mask;		// whether masks are used to sent data (true for client, false for server).
	UserType *user;		// callback functions are called on this object.
	DisconnectCb disconnect_cb;
	ErrorCb error_cb;
	void inject(std::string &data);	// Make the class handle incoming data.
	void disconnect_impl() { if (disconnect_cb != nullptr) (user->*disconnect_cb)(); else WL_log("disconnect"); }
	void error_impl(std::string const &message) { if (error_cb != nullptr) (user->*error_cb)(message); else WL_log("error: " + message); }
	enum HttpState { HTTP_INACTIVE, HTTP_START, HTTP_HEADER, HTTP_DONE };
	HttpState http_state;
	coroutine::handle_type init_waiter;
	void recv_http(std::string &hdrdata);	// Receive http headers and non-websocket body data.
public:
	/// Settings that are only used for connecting websockets.
	struct ConnectSettings {
		std::string method;				// default: GET
		std::string user;				// login name; if both this and password are empty: don't send them.
		std::string password;				// login password
		std::map <std::string, std::string> sent_headers;	// extra headers, sent to server.
		ConnectSettings(std::string const &method = "GET", std::string const &user = {}, std::string const &password = {}, std::map <std::string, std::string> const &sent_headers = {}) : method(method), user(user), password(password), sent_headers(sent_headers) {}
	};
	/// Settings used for all websockets (both connecting and accepted).
	struct RunSettings {
		Loop *loop;					// Main loop to use; usually nullptr, so the default is used.
		Loop::Duration keepalive;			// keepalive timer.
	};
	/// Connection settings; not used for websockets that are accepted through Httpd.
	ConnectSettings connect_settings;
	/// Settings that are used by all websockets (both connecting and accepted).
	RunSettings run_settings;

	/// For accepted websockets only: Http headers that were received from the browser.
	std::map <std::string, std::string> received_headers;	// Headers that are received from server. (Only for accepted websockets.)
private:
	// Make this the last member, so all other members are initialized before it.
	Socket <Websocket <UserType> > socket;

	// Internal callback for keepalive pings.
	bool keepalive();
public:
	/// Disconnect this websocket.
	void disconnect(bool send_to_websocket = true);

	/// Constructor. This is mostly usefor to assign a connected websocket to the object later.
	Websocket();

	/// Constructor for a websocket that connects to a server.
	Websocket(std::string const &address, ConnectSettings const &connect_settings = {}, UserType *user = nullptr, Receiver receiver = nullptr, RunSettings const &run_settings = {});

	/// Constructor for a (possibly fake) websocket that is constructed around an already open file descriptor.
	Websocket(int socket_fd, UserType *user = nullptr, Receiver receiver = nullptr, RunSettings const &run_settings = {});

	/// Constructor for a websocket that was accepted by a server.
	template <class ServerType>
	Websocket(Socket <ServerType> &&src, UserType *user = nullptr, Receiver receiver = nullptr, RunSettings const &run_settings = {});

	coroutine wait_for_init() {
		if (http_state != HTTP_DONE) {
			std::cout << "yielding " << std::hex << (long)this << std::dec << std::endl;
			init_waiter = GetHandle();
			co_yield WebNone::create();
		}
		std::cout << "returning " << std::hex << (long)this << std::dec << std::endl;
		co_return WebNone::create();
	}

	/// Websocket move constructor.
	Websocket(Websocket <UserType> &&src);
	/// Websocket move assignment.
	Websocket <UserType> &operator=(Websocket <UserType> &&src);

	/// Set a new user object (of the same type as the old one) on the websocket.
	void update_user(UserType *new_user) { user = new_user; }

	/// Destructor.
	~Websocket() { disconnect(); }

	// Set callbacks.
	/// Register callback that is called when the websocket is disconnected.
	void set_disconnect_cb(DisconnectCb callback) { disconnect_cb = callback; }

	/// Register callback that is called when the websocket encounters an error.
	void set_error_cb(ErrorCb callback) { error_cb = callback; }

	/// Register callback that is called when the websocket receives a data packet.
	void set_receiver(Receiver callback) { receiver = callback; }

	/// Send a data frame.
	void send(std::string const &data, int opcode = 1); // Send a WebSocket frame.

	/// Send a ping frame.
	/// @Returns: true if a pong was received since previous ping was sent; false if not.
	bool ping(std::string const &data = std::string());

	/// Query connection state.
	/// @Returns: false if socket is open, true if not.
	bool closed() const { return is_closed; }

	/// Get the socket name (for debugging).
	/// @Returns: The socket name.
	constexpr std::string const &get_name() const { return socket.get_name(); }

	/// Set the socket name (for debugging).
	/// @param n: New socket name.
	void set_name(std::string const &n) { socket.set_name(n); }
}; // }}}

// Websocket internals. {{{
template <class UserType>
void Websocket <UserType>::disconnect(bool send_to_websocket) { // {{{
	STARTFUNC;
	if (is_closed)
		return;
	is_closed = true;
	if (keepalive_handle != Webloop::Loop::TimeoutHandle()) {
		Loop::get()->remove_timeout(keepalive_handle);
		keepalive_handle = Webloop::Loop::TimeoutHandle();
	}
	if (send_to_websocket)
		send(std::string(), 8);
	socket.close();
} // }}}

template <class UserType>
Websocket <UserType>::Websocket() : // {{{
	buffer(),
	fragments(),
	keepalive_handle(),
	is_closed(true),
	pong_seen(true),
	current_opcode(uint8_t(-1)),
	receiver(),
	send_mask(false),
	user(),
	disconnect_cb(),
	error_cb(),
	http_state(HTTP_INACTIVE),
	init_waiter(),
	connect_settings(),
	run_settings(),
	received_headers(),
	socket()
{
	STARTFUNC;
} // }}}

template <class UserType>
void Websocket <UserType>::recv_http(std::string &hdrdata) { // {{{
	switch (http_state) {
	case HTTP_INACTIVE:
		throw "receiving data before socket is active";
	case HTTP_START:
	{
		std::string::size_type pos = hdrdata.find("\n");
		if (pos == std::string::npos)
			return;
		std::istringstream firstline(hdrdata.substr(0, pos));
		std::string namecode;
		int numcode;
		firstline >> namecode >> numcode;
		if (numcode != 101) {
			WL_log("Unexpected reply: " + hdrdata);
			throw "wrong reply code";
		}
		hdrdata = hdrdata.substr(pos + 1);
		http_state = HTTP_HEADER;
	}
		// Fall through.
	case HTTP_HEADER:
	{
		while (true) {
			std::string::size_type pos = hdrdata.find("\n");
			if (pos == std::string::npos)
				return;
			std::string line = hdrdata.substr(0, pos);
			hdrdata = hdrdata.substr(pos + 1);
			if (strip(line).empty())
				break;
			if (DEBUG > 2)
				WL_log("Header: " + line);
			std::string::size_type sep = line.find(":");
			if (sep == std::string::npos) {
				WL_log("invalid header line");
				throw "invalid header line";
			}
			std::string key = line.substr(0, sep);
			std::string value = line.substr(sep + 1);
			received_headers[strip(key)] = strip(value);
			continue;
		}
		is_closed = false;
		socket.read(&Websocket <UserType>::inject);
		socket.set_disconnect_cb(&Websocket <UserType>::disconnect_impl);
		socket.set_error_cb(&Websocket <UserType>::error_impl);
		// Set up keepalive heartbeat.
		if (run_settings.keepalive != Loop::Duration()) {
			Loop *loop = Loop::get(run_settings.loop);
			keepalive_handle = loop->add_timeout(Loop::TimeoutRecord(loop->now() + run_settings.keepalive, run_settings.keepalive, this, &Websocket <UserType>::keepalive));
		}
		if (!hdrdata.empty())
			inject(hdrdata);
		if (DEBUG > 2)
			WL_log("opened websocket " + get_name());
		http_state = HTTP_DONE;

		// Wake init waiter.
		if (init_waiter != coroutine::handle_type()) {
			if (DEBUG > 3)
				WL_log("waking init waiter");
			init_waiter();
		}
		else {
			if (DEBUG > 3)
				WL_log("not waking");
		}
		break;
	}
	default:
		throw "Invalid HttpState in Websocket";
	}
} // }}}

template <class UserType>
Websocket <UserType>::Websocket(std::string const &address, ConnectSettings const &connect_settings, UserType *user, Receiver receiver, RunSettings const &run_settings) : // {{{
	buffer(),
	fragments(),
	keepalive_handle(),
	is_closed(true),
	pong_seen(true),
	current_opcode(uint8_t(-1)),
	receiver(receiver),
	send_mask(true),
	user(user),
	disconnect_cb(),
	error_cb(),
	http_state(HTTP_INACTIVE),
	init_waiter(),
	connect_settings(connect_settings),
	run_settings(run_settings),
	received_headers(),
	socket("websocket to " + address, address, this)
{
	STARTFUNC;
	/* When constructing a Websocket, a connection is made to the
	requested port, and the websocket handshake is performed.  This
	constructor passes any extra arguments to the network.Socket
	constructor (if it is called), in particular "tls" can be used
	to control the use of encryption.  There objects are also
	created by the websockets server.  For that reason, there are
	some arguments that should not be used when calling it
	directly.
	@param port: Host and port to connect to, same format as
		python-network uses, or None for an incoming connection (internally used).
	@param recv: Function to call when a data packet is received
		asynchronously.
	@param method: Connection method to use.
	@param user: Username for authentication.  Only plain text
		authentication is supported; this should only be used
		over a link with TLS encryption.
	@param password: Password for authentication.
	@param extra: Extra headers to pass to the host.
	@param socket: Existing socket to use for connection, or None
		to create a new socket.
	@param mask: Mostly for internal use by the server.  Flag
		whether or not to send and receive masks.  (None, True)
		is the default, which means to accept anything, and
		send masked packets.  Note that the mask that is used
		for sending is always (0,0,0,0), which is effectively
		no mask.  It is sent to follow the protocol.  No real
		mask is sent, because masks give a false sense of
		security and provide no benefit.  The unmasking
		implementation is rather slow.  When communicating
		between two programs using this module, the non-mask is
		detected and the unmasking step is skipped.
	@param websockets: For interal use by the server.  A set to remove the socket from on disconnect.
	@param data: For internal use by the server.  Data to pass through to callback functions.
	@param real_remote: For internal use by the server.  Override detected remote.  Used to have proper remotes behind virtual proxy.
	@param keepalive: Seconds between keepalive pings, or None to disable keepalive pings.
	*/
	// Use real_remote if it was provided.
	/*if (settings.real_remote.empty()) {
		if isinstance(socket.remote, (tuple, list)):
			self.remote = [real_remote, socket.remote[1]]
		else:
			self.remote = [real_remote, None]
	else:
		self.remote = socket.remote}*/
	std::string extra_headers;
	for (auto e: connect_settings.sent_headers)
		extra_headers += e.first + ": " + e.second + "\r\n";
	std::string userpwd;
	if (!connect_settings.user.empty() || !connect_settings.password.empty())
		userpwd = connect_settings.user + ":" + connect_settings.password + "\r\n";
	// Sec-Websocket-Key is not random, because that has no
	// value. The example value from the RFC is used.
	// Differently put: it uses a special random generator
	// which always returns range(0x01, 0x11).
	socket.send(
		connect_settings.method + " " + socket.url.build_request() + " HTTP/1.1\r\n"
		"Host: " + socket.url.build_host() + "\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: AQIDBAUGBwgJCgsMDQ4PEC==\r\n"
		"Sec-WebSocket-Version: 13\r\n" +
		userpwd +
		extra_headers +
		"\r\n");
	http_state = HTTP_START;
	// TODO: Set special error and disconnect callbacks for connection startup.
	socket.read(&Websocket <UserType>::recv_http);
} // }}}

template <class UserType>
Websocket <UserType>::Websocket(int socket_fd, UserType *user, Receiver receiver, RunSettings const &run_settings) : // {{{
	buffer(),
	fragments(),
	keepalive_handle(),
	is_closed(false),
	pong_seen(true),
	current_opcode(uint8_t(-1)),
	receiver(receiver),
	send_mask(false),
	user(user),
	disconnect_cb(),
	error_cb(),
	http_state(HTTP_INACTIVE),
	init_waiter(),
	connect_settings(),
	run_settings(run_settings),
	received_headers(),
	socket(socket_fd, this)
{
	STARTFUNC;
	/* When constructing a Websocket, a connection is made to the
	requested port, and the websocket handshake is performed.  This
	constructor passes any extra arguments to the network.Socket
	constructor (if it is called), in particular "tls" can be used
	to control the use of encryption.  There objects are also
	created by the websockets server.  For that reason, there are
	some arguments that should not be used when calling it
	directly.
	@param port: Host and port to connect to, same format as
		python-network uses, or None for an incoming connection (internally used).
	@param recv: Function to call when a data packet is received
		asynchronously.
	@param method: Connection method to use.
	@param user: Username for authentication.  Only plain text
		authentication is supported; this should only be used
		over a link with TLS encryption.
	@param password: Password for authentication.
	@param extra: Extra headers to pass to the host.
	@param socket: Existing socket to use for connection, or None
		to create a new socket.
	@param mask: Mostly for internal use by the server.  Flag
		whether or not to send and receive masks.  (None, True)
		is the default, which means to accept anything, and
		send masked packets.  Note that the mask that is used
		for sending is always (0,0,0,0), which is effectively
		no mask.  It is sent to follow the protocol.  No real
		mask is sent, because masks give a false sense of
		security and provide no benefit.  The unmasking
		implementation is rather slow.  When communicating
		between two programs using this module, the non-mask is
		detected and the unmasking step is skipped.
	@param websockets: For interal use by the server.  A set to remove the socket from on disconnect.
	@param data: For internal use by the server.  Data to pass through to callback functions.
	@param real_remote: For internal use by the server.  Override detected remote.  Used to have proper remotes behind virtual proxy.
	@param keepalive: Seconds between keepalive pings, or None to disable keepalive pings.
	*/
	// Set up keepalive heartbeat.
	if (run_settings.keepalive != Loop::Duration()) {
		Loop *loop = Loop::get(run_settings.loop);
		keepalive_handle = loop->add_timeout({loop->now() + run_settings.keepalive, run_settings.keepalive, keepalive, this});
	}
	if (DEBUG > 2)
		WL_log("accepted websocket");
	socket.user_data = this;
	socket.set_disconnect_cb(&Websocket <UserType>::disconnect_impl);
	socket.set_error_cb(&Websocket <UserType>::error_impl);
	socket.read(&Websocket <UserType>::inject);
} // }}}

template <class UserType> template <class ServerType>
Websocket <UserType>::Websocket(Socket <ServerType> &&src, UserType *user, Receiver receiver, RunSettings const &run_settings) : // {{{
	buffer(),
	fragments(),
	keepalive_handle(),
	is_closed(false),
	pong_seen(true),
	current_opcode(uint8_t(-1)),
	receiver(receiver),
	send_mask(false),
	user(user),
	disconnect_cb(),
	error_cb(),
	http_state(HTTP_INACTIVE),
	connect_settings(),
	run_settings(run_settings),
	received_headers(),
	socket(std::move(src), this)
{
	STARTFUNC;
	// Set up keepalive heartbeat.
	if (run_settings.keepalive != Loop::Duration()) {
		Loop *loop = Loop::get(run_settings.loop);
		keepalive_handle = loop->add_timeout(Loop::TimeoutRecord {loop->now() + run_settings.keepalive, run_settings.keepalive, reinterpret_cast <Loop::CbBase *>(this), reinterpret_cast <Loop::Cb>(&Websocket <UserType>::keepalive)});
	}
	if (DEBUG > 2)
		WL_log("accepted websocket");
	socket.set_disconnect_cb(&Websocket <UserType>::disconnect_impl);
	socket.set_error_cb(&Websocket <UserType>::error_impl);
	socket.read(&Websocket <UserType>::inject);
} // }}}

template <class UserType>
Websocket <UserType>::Websocket(Websocket <UserType> &&other) : // {{{
	buffer(std::move(other.buffer)),
	fragments(std::move(other.fragments)),
	keepalive_handle(),
	is_closed(other.is_closed),
	pong_seen(other.pong_seen),
	current_opcode(other.current_opcode),
	receiver(other.receiver),
	send_mask(other.send_mask),
	user(other.user),
	disconnect_cb(other.disconnect_cb),
	error_cb(other.error_cb),
	http_state(other.http_state),
	init_waiter(other.init_waiter),
	connect_settings(other.connect_settings),
	run_settings(other.run_settings),
	received_headers(std::move(other.received_headers)),
	socket(std::move(other))
{
	socket.update_user(this);
	other.init_waiter = coroutine::handle_type();
	other.buffer.clear();
	other.fragments.clear();
	other.received_headers.clear();
	WL_log("buffer moved");
	if (!other.is_closed) {
		if (run_settings.keepalive != Loop::Duration()) {
			Loop::get(other.run_settings.loop)->remove_timeout(other.keepalive_handle);
			Loop *loop = Loop::get(run_settings.loop);
			keepalive_handle = loop->add_timeout(Loop::TimeoutRecord(loop->now() + run_settings.keepalive, run_settings.keepalive, this, &Websocket <UserType>::keepalive));
		}
	}
	other.is_closed = true;
	socket.set_disconnect_cb(&Websocket <UserType>::disconnect_impl);
	socket.set_error_cb(&Websocket <UserType>::error_impl);
} // }}}

template <class UserType>
Websocket <UserType> &Websocket <UserType>::operator=(Websocket <UserType> &&other) { // {{{
	STARTFUNC;
	disconnect();
	socket = std::move(other.socket);
	socket.update_user(this);
	buffer = std::move(other.buffer);
	other.buffer.clear();
	fragments = std::move(other.fragments);
	other.fragments.clear();
	is_closed = other.is_closed;
	pong_seen = other.pong_seen;
	current_opcode = other.current_opcode;
	receiver = other.receiver;
	send_mask = other.send_mask;
	user = other.user;
	disconnect_cb = other.disconnect_cb;
	error_cb = other.error_cb;
	http_state = other.http_state;
	init_waiter = other.init_waiter;
	connect_settings = other.connect_settings;
	run_settings = other.run_settings;
	received_headers = std::move(other.received_headers);
	other.received_headers.clear();
	if (!other.is_closed) {
		if (run_settings.keepalive != Loop::Duration()) {
			Loop::get(other.run_settings.loop)->remove_timeout(other.keepalive_handle);
			Loop *loop = Loop::get(run_settings.loop);
			keepalive_handle = loop->add_timeout(Loop::TimeoutRecord(loop->now() + run_settings.keepalive, run_settings.keepalive, this, &Websocket <UserType>::keepalive));
		}
	}
	other.init_waiter = coroutine::handle_type();
	other.is_closed = true;
	socket.set_disconnect_cb(&Websocket <UserType>::disconnect_impl);
	socket.set_error_cb(&Websocket <UserType>::error_impl);
	return *this;
} // }}}

template <class UserType>
bool Websocket <UserType>::keepalive() { // {{{
	STARTFUNC;
	if (!ping())
		WL_log("Warning: no keepalive reply received");
	return true;
} // }}}

template <class UserType>
void Websocket <UserType>::inject(std::string &data) { // {{{
	STARTFUNC;
	// Handle received data. Return bool to use as read callback.
	// Websocket data consists of:
	// 1 byte:
	//	bit 7: 1 for last (or only) fragment; 0 for other fragments.
	//	bit 6-4: extension stuff; must be 0.
	//	bit 3-0: opcode.
	// 1 byte:
	//	bit 7: 1 if masked, 0 otherwise.
	//	bit 6-0: length or 0x7e or 0x7f.
	// If 0x7e:
	// 	2 bytes: length
	// If 0x7f:
	//	8 bytes: length
	// If masked:
	//	4 bytes: mask
	// length bytes: (masked) payload

	//WL_log("received: " + data);
	if (DEBUG > 2)
		WL_log((std::ostringstream() << "received " << data.length() << " bytes: " << WebString(data).dump()).str());
	if (DEBUG > 3) {
		//WL_log(std::format("waiting: ' + ' '.join(['%02x' % x for x in self.websocket_buffer]) + ''.join([chr(x) if 32 <= x < 127 else '.' for x in self.websocket_buffer]))
		//WL_log('data: ' + ' '.join(['%02x' % x for x in data]) + ''.join([chr(x) if 32 <= x < 127 else '.' for x in data]))
	}
	buffer += std::move(data);
	data.clear();
	while (!buffer.empty()) {
		if (buffer[0] & 0x70) {
			// Protocol error.
			WL_log("extension stuff is not supported!");
			is_closed = true;
			socket.close();
			return;
		}
		// Check that entire packet is received. {{{
		if (buffer.size() < 2) {
			// Not enough data for length bytes.
			if (DEBUG > 2)
				WL_log("no length yet");
			return;
		}
		char b = buffer[1];
		bool have_mask = bool(b & 0x80);
		b &= 0x7f;
		if ((have_mask && send_mask) || (!have_mask && !send_mask)) {
			// Protocol error.
			WL_log("mask error have mask:" + std::to_string(have_mask) + "; send mask:" + std::to_string(send_mask));
			is_closed = true;
			socket.close();
			return;
		}
		std::string::size_type pos;
		std::string::size_type len = 0;
		if (b == 0x7f) {
			if (buffer.length() < 10) {
				// Not enough data for length bytes.
				if (DEBUG > 2)
					WL_log("no 10 length yet");
				return;
			}
			for (int i = 0; i < 8; ++i)
				len |= (buffer[2 + i] & 0xff) << (8 * (7 - i));
			pos = 10;
		}
		else if (b == 0x7e) {
			if (buffer.length() < 4) {
				// Not enough data for length bytes.
				if (DEBUG > 2)
					WL_log("no 4 length yet");
				return;
			}
			for (int i = 0; i < 2; ++i)
				len |= (buffer[2 + i] & 0xff) << (8 * (1 - i));
			pos = 4;
		}
		else {
			len = b;
			pos = 2;
		}
		if (buffer.length() < pos + (have_mask ? 4 : 0) + len) {
			// Not enough data for packet.
			if (DEBUG > 2)
				WL_log((std::ostringstream() << "no packet yet; length = " << buffer.length() << "; need " << pos << " + " << (have_mask ? 4 : 0) << " + " << len).str());
			// Long packets should not cause ping timeouts.
			pong_seen = true;
			return;
		}
		// }}}
		std::string header = buffer.substr(0, pos);
		uint8_t opcode = header[0] & 0xf;
		std::string packet;
		uint32_t mask;
		if (have_mask) {
			mask = *(reinterpret_cast <uint32_t *> (&buffer.data()[pos]));
			pos += 4;
		}
		if (have_mask && mask != 0) {
			auto p = pos;
			for (p = pos; p + 3 < buffer.size(); p += 4) {
				auto word = *(reinterpret_cast <uint32_t const *> (&buffer.data()[p])) ^ mask;
				packet += std::string(reinterpret_cast <char const *> (&word), 4);
			}
			uint8_t m[4];
			*reinterpret_cast <uint32_t *>(m) = mask;
			for (int i = 0; p < buffer.size(); ++p, ++i)
				packet += std::string(1, buffer.data()[p] ^ m[i]);
		}
		else {
			packet = buffer.substr(pos, len);
		}
		buffer = buffer.substr(pos + len);
		if (current_opcode == uint8_t(-1))
			current_opcode = opcode;
		else if (opcode != 0) {
			// Protocol error.
			// Exception: pongs are sometimes sent asynchronously.
			// Theoretically the packet can be fragmented, but that should never happen; asynchronous pongs seem to be a protocol violation anyway...
			if (opcode == 10) {
				// Pong.
				pong_seen = true;
			}
			else {
				WL_log("invalid fragment");
				is_closed = true;
				socket.close();
				return;
			}
			continue;
		}
		fragments += packet;
		if ((header[0] & 0x80) != 0x80) {
			// fragment found; not last.
			pong_seen = true;
			if (DEBUG > 2)
				WL_log("fragment recorded");
			continue;
		}
		// Complete frame has been received.
		packet = std::move(fragments);
		fragments.clear();
		opcode = current_opcode;
		current_opcode = uint8_t(-1);
		switch(opcode) {
		case 8:
			// Connection close request.
			disconnect(false);
			return;
		case 9:
			// Ping.
			send(packet, 10);	// Pong
			break;
		case 10:
			// Pong.
			pong_seen = true;
			break;
		case 1:	// Text.
		case 2:	// Binary.
			(user->*receiver)(packet);
			break;
		default:
			WL_log("invalid opcode");
			is_closed = true;
			socket.close();
			return;
		}
	}
} // }}}

template <class UserType>
void Websocket <UserType>::send(std::string const &data, int opcode) {	// Send a WebSocket frame.  {{{
	STARTFUNC;
	/* Send a Websocket frame to the remote end of the connection.
	@param data: Data to send.
	@param opcode: Opcade to send.  0 = fragment, 1 = text packet, 2 = binary packet, 8 = close request, 9 = ping, 10 = pong.
	*/
	if (DEBUG > 3)
		WL_log("websend: " + data);
	assert((opcode >= 0 && opcode <= 2) || (opcode >= 8 && opcode <=10));
	if (is_closed)
		return;
	//if (opcode == 1)
	//	data = data.encode('utf-8')
	uint8_t maskchar;
	std::string maskcode;
	if (send_mask) {
		maskchar = 0x80;
		// Masks are stupid, but the standard requires them.  Don't waste time on encoding (or decoding, if also using this library).
		maskcode = std::string("\0\0\0\0", 4);
	}
	else {
		maskchar = 0;
	}
	std::string len;
	size_t l = data.length();
	if (l < 0x7e) {
		char lenchar = maskchar | l;
		len = std::string(&lenchar, 1);
	}
	else if (l < 1 << 16) {
		char lendata[3] {char(maskchar | 0x7e), char((l >> 8) & 0xff), char(l & 0xff)};
		len = std::string(lendata, 3);
	}
	else {
		char header[9];
		header[0] = maskchar | 0x7f;
		for (int i = 0; i < 8; ++i)
			header[1 + i] = (l >> (8 * (7 - i))) & 0xff;
		len = std::string(header, 9);
	}
	try {
		char o = 0x80 | opcode;
		socket.send(std::string(&o, 1) + len + maskcode + data);
	}
	catch (char const *msg) {
		// Something went wrong; close the socket(in case it wasn't yet).
		WL_log(std::string("closing socket due to problem while sending: ") + msg);
		is_closed = true;
		socket.close();
	}
	if (opcode == 8) {
		is_closed = true;
		socket.close();
	}
} // }}}

template <class UserType>
bool Websocket <UserType>::ping(std::string const &data) { // Send a ping; return False if no pong was seen for previous ping.  Other received packets also count as a pong. {{{
	STARTFUNC;
	/* Send a ping, return if a pong was received since last ping.
	@param data: Data to send with the ping.
	@return True if a pong was received since last ping, False if not.
	*/
	bool ret = pong_seen;
	pong_seen = false;
	send(data, 9);
	return ret;
} // }}}
// }}}
// }}}

// Httpd. {{{
template <class OwnerType>
class Httpd { // {{{
	/* HTTP server.
	This object implements an HTTP server.  It supports GET and
	POST, and of course websockets.
	*/
public:
	class Connection;
	friend class Connection;
	typedef void (OwnerType::*PostCb)(Connection &connection);
	typedef void (OwnerType::*ClosedCb)();
	typedef void (OwnerType::*ErrorCb)(std::string const &message);
	typedef void (OwnerType::*AcceptCb)(Connection &connection);
	OwnerType *owner;				// User data to send with callbacks.
	std::string service;				// Service (name or port number) that the server listens on.
	std::list <Connection> connections;		// Active connections, including non-websockets.
private:
	std::list <std::filesystem::path> htmldirs;	// Directories where static web pages are searched.
	std::list <std::string> proxy;			// Proxy prefixes which are ignored when received.
	PostCb post_cb;					// Callback when a POST request is received.
	AcceptCb accept_cb;				// Websocket accept callback.
	ClosedCb closed_cb;				// Closed callback.
	ErrorCb error_cb;				// Error callback.
	std::map <std::string, std::string> exts;	// Handled extensions; key is extension (including '.'), value is mime type.
	Loop *loop;					// Main loop for registering read events.
	Loop::Duration keepalive;			// Default keepalive for accepted sockets.
	Server <Connection, Httpd <OwnerType> > server;	// Network server which provides the interface.
	virtual char const *authentication(Connection &connection) { (void)&connection; return nullptr; }	// Override to require authentication.
	virtual bool valid_credentials(Connection &connection) { (void)&connection; return true; }	// Override to check credentials.
	virtual bool page(Connection &connection) { (void)&connection; return false; }	// Override and return true to provide dynamic pages.
	void server_closed();
	void server_error(std::string const &message);
	void create_connection(Socket <Connection> *socket);
public:
	Httpd(OwnerType *owner, std::string const &service, std::string const &htmldir = "html", Loop *loop = nullptr, int backlog = 5);
	~Httpd() { STARTFUNC; WL_log("destructing http server " + std::to_string((long)this)); }
	// Move support.
	Httpd(Httpd <OwnerType> &&src);
	Httpd <OwnerType> &operator=(Httpd <OwnerType> &&src);
	void set_post(PostCb callback) { STARTFUNC; post_cb = callback; }
	void set_accept(AcceptCb callback) { STARTFUNC; accept_cb = callback; }
	void set_closed(ClosedCb callback) { STARTFUNC; closed_cb = callback; }
	void set_error(ErrorCb callback) { STARTFUNC; error_cb = callback; }
	void set_default_keepalive(Loop::Duration duration) { STARTFUNC; keepalive = duration; }
	Loop::Duration get_keepalive() const { return keepalive; }
	Loop *get_loop() { return loop; }
}; // }}}

template <class OwnerType>
void Httpd <OwnerType>::server_closed() { // {{{
	WL_log("Server closed " + std::to_string((long)this));
	if (closed_cb != nullptr)
		(owner->*closed_cb)();
} // }}}

template <class OwnerType>
void Httpd <OwnerType>::server_error(std::string const &message) { // {{{
	WL_log("Error received by server " + std::to_string((long)this) + ": " + message);
	if (error_cb != nullptr)
		(owner->*error_cb)(message);
} // }}}

template <class OwnerType>
void Httpd <OwnerType>::create_connection(Socket <Connection> *socket) { // {{{
	WL_log("received new connection " + std::to_string((long)this));
	connections.emplace_back(std::move(socket), this);
} // }}}

template <class OwnerType>
Httpd <OwnerType>::Httpd(OwnerType *owner, std::string const &service, std::string const &htmldir, Loop *loop, int backlog) : // {{{
		owner(owner),
		service(service),
		connections{},
		// The default location for html files is "html"; if it is set to "" by the caller, no html pages will be served.
		htmldirs(htmldir.empty() ? std::list <std::filesystem::path>() : read_data_names(htmldir, {}, true, true)),
		proxy(),
		post_cb(),
		accept_cb(),
		closed_cb(),
		error_cb(),
		exts(),
		loop(Loop::get(loop)),
		keepalive(50s),
		server(service, this, &Httpd <OwnerType>::create_connection, &Httpd <OwnerType>::server_closed, &Httpd <OwnerType>::server_error, loop, backlog)
{
	WL_log("created new http server " + std::to_string((long)this));
	/* Create a webserver.
	Additional arguments are passed to the network.Server.
	@param port: Port to listen on.  Same format as in
		python-network.
	@param htmldirs: Locations of static web pages to
		serve.
	@param proxy: Tuple of virtual proxy prefixes that
		should be ignored if requested.
	*/

	// Automatically add all extensions for which a mime type exists. {{{
	std::ifstream mimetypes("/etc/mime.types");
	if (mimetypes.is_open()) {
		std::set <std::string> duplicate;
		while (true) {
			std::string line;
			std::getline(mimetypes, line, '\n');
			if (!mimetypes)
				break;
			auto parts = split(line);
			if (parts.size() == 0 || parts[0][0] == '#')
				continue;
			for (size_t i = 1; i < parts.size(); ++i) {
				// If this is an existing duplicate, ignore it.
				std::string ext = "." + parts[i];
				if (duplicate.contains(ext))
					continue;

				// If this is a new duplicate, remove it and ignore it.
				auto p = exts.find(ext);
				if (p != exts.end()) {
					duplicate.insert(ext);
					exts.erase(p);
					continue;
				}

				// Otherwise, insert it in the map.
				if (parts[0].substr(0, 5) == "text/" || parts[0] == "application/javascript")
					exts[ext] = parts[0] + ";charset=utf-8";
				else
					exts[ext] = parts[0];
			}
		}
	}
	else {
		// This is probably a Windows system; use some defaults.
		exts = {
			{".html", "text/html;charset=utf-8"},
			{".css", "text/css;charset=utf-8"},
			{".js", "text/javascript;charset=utf-8"},
			{".jpg", "image/jpeg"},
			{".jpeg", "image/jpeg"},
			{".png", "image/png"},
			{".bmp", "image/bmp"},
			{".gif", "image/gif"},
			{".pdf", "application/pdf"},
			{".svg", "image/svg+xml"},
			{".txt", "text/plain;charset=utf-8"}
		};
	} // }}}
} // }}}

template <class OwnerType>
Httpd <OwnerType>::Httpd(Httpd <OwnerType> &&src) : // {{{
		owner(src.owner),
		service(std::move(src.service)),
		connections(std::move(src.connections)),
		htmldirs(std::move(src.htmldirs)),
		proxy(std::move(src.proxy)),
		post_cb(src.post_cb),
		accept_cb(src.accept_cb),
		closed_cb(src.closed_cb),
		error_cb(src.error_cb),
		exts(std::move(src.exts)),
		loop(src.loop),
		keepalive(src.keepalive),
		server(std::move(src.server), this, &Httpd <OwnerType>::create_connection, &Httpd <OwnerType>::server_closed, &Httpd <OwnerType>::server_error)
{
	STARTFUNC;
	WL_log("moving http server " + std::to_string((long)&src) + " to " + std::to_string((long)this));
} // }}}

template <class OwnerType>
Httpd <OwnerType> &Httpd <OwnerType>::operator=(Httpd <OwnerType> &&src) { // {{{
	STARTFUNC;
	WL_log("move-assigning http server " + std::to_string((long)&src) + " to " + std::to_string((long)this));
	owner = std::move(src.owner);
	service = std::move(src.service);
	connections = std::move(src.connections);
	htmldirs = std::move(src.htmldirs);
	proxy = std::move(src.proxy);
	post_cb = src.post_cb;
	accept_cb = src.accept_cb;
	closed_cb = src.closed_cb;
	error_cb = src.error_cb;
	exts = std::move(src.exts);
	loop = src.loop;
	keepalive = src.keepalive;
	server = std::move(src.server);
} // }}}

template <class OwnerType>
class Httpd <OwnerType>::Connection { // {{{
	/* Connection object for an HTTP server.
	This object implements the internals of an HTTP server.  It
	supports GET and POST, and of course websockets.  Don't
	construct these objects directly.
	*/
public:
	// This struct is used to store non-file post data.
	struct PostData {
		std::string value;
		std::map <std::string, std::pair <std::string, std::map <std::string, std::string> > > header;
	};
	// This struct is used to store file post data.
	struct PostFile {
		std::ofstream file;
		std::string mime;
		std::string filename;
		std::map <std::string, std::pair <std::string, std::map <std::string, std::string> > > header;
	};
private:
	friend class Httpd <OwnerType>;
	std::string post_boundary;
	// Example post header field: Content-Type: text/plain; charset=utf-8
	// This maps to {"Content-Type", {"text/plain", {"charset, "utf-8"} } }.
	std::map <std::string, std::pair <std::string, std::map <std::string, std::string> > > post_header;
	std::ofstream post_current_file;
	void reset() { // {{{
		// Clean up state data after completed transaction.
		received_headers.clear();
		method.clear();
		url.clear();
		http_version.clear();
		user.clear();
		password.clear();
		prefix.clear();
	} // }}}
	void ignore_disconnect() {}
	std::map <std::string, std::string> parse_args(std::string const &args) { // {{{
		std::map <std::string, std::string> ret;
		std::string::size_type pos = 0;
		while (pos < args.size()) {
			auto p = args.find('=', pos);
			if (p == std::string::npos) {
				WL_log("ignoring incomplete header argument");
				break;
			}
			std::string key = lower(strip(args.substr(pos, p)));
			pos = p + 1;
			std::string value;
			while (true) {
				p = args.find_first_of("\\\";", pos);

				// Nothing special: use rest of string.
				if (p == std::string::npos) {
					value += args.substr(pos);
					pos = args.size();
					break;
				}

				// Start by adding string up to token.
				value += args.substr(pos, p);

				// Semicolon marks end of value.
				if (args[p] == ';') {
					pos = p + 1;
					break;
				}

				// Backslash escapes whatever follows it.
				if (args[p] == '\\') {
					value += args[++p];
					pos = p + 1;
					continue;
				}

				// Double quote starts protected string.
				assert(args[p] == '"');
				value += args.substr(pos, p);
				pos = p + 1;
				while (true) {
					p = args.find_first_of("\\\"");
					if (p == std::string::npos) {
						WL_log("missing end quote in argument");
						return ret;
					}
					value += args.substr(pos, p);
					// Backslash escapes whatever follows it.
					if (args[p] == '\\') {
						value += args[++p];
						continue;
					}
					// Double quote ends quoted string.
					assert(args[p] == '"');
					pos = p + 1;
					break;
				}
			}
			if (ret.find(key) != ret.end()) {
				WL_log("duplicate key in argument: " + key);
				return ret;
			}
			ret[key] = value;
		}
		return ret;
	} // }}}
	std::string post_decode(std::string &data, bool finish) { // {{{
		// Decode data in POST body, given post_header (with all relevant fields existing).
		std::string &encoding = post_header["content-transfer-encoding"].first;
		if (encoding == "7bit") {
			std::string ret = std::move(data);
			data.clear();
			return ret;
		}
		if (encoding == "quoted-printable") {
			std::string ret;
			std::string::size_type pos = 0;
			while (true) {
				auto p = data.find('=', pos);
				if (p == std::string::npos) {
					ret += data.substr(pos);
					data.clear();
					return ret;
				}
				ret += data.substr(pos, p);
				if (p > data.size() - 3) {
					if (finish) {
						WL_log("invalid quoted printable");
						reply(400, {}, {}, {}, false);
						data.clear();
						return ret;
					}
					data = data.substr(p);
					return ret;
				}
				if (data.substr(p + 1, 2) == "\r\n") {
					ret += "\n";
					pos = p + 3;
					continue;
				}
				char num;
				try {
					num = std::stoi(data.substr(p + 1, 2), nullptr, 16);
				}
				catch (...) {
					WL_log("invalid quoted printable");
					reply(400, {}, {}, {}, false);
					data.clear();
					return ret;
				}
				ret += num;
				pos = p + 3;
			}
		}
		if (encoding == "base64") {
			std::string::size_type pos = 0;
			std::string ret = b64decode(data, pos, true);
			if (pos == std::string::npos) {
				WL_log("invalid base64 data");
				reply(400, {}, {}, {}, false);
				data.clear();
				return ret;
			}
			data = data.substr(pos);
			return ret;
		}
		WL_log("unrecognized Content-Transfer-Encoding in POST: " + encoding);
		return {};
	} // }}}
	void read_post_body(std::string &buffer) { // {{{
		auto p = buffer.find(post_boundary);
		if (p != std::string::npos) {
			auto part = buffer.substr(0, p);
			buffer = buffer.substr(p);
			post_current_file << post_decode(part, true);

			// Store data.
			std::string &name = post_header["content-disposition"].second["name"];
			post_file.emplace(name, PostFile {std::move(post_current_file), post_header["content-type"].first, post_header["content-disposition"].second["filename"], std::move(post_header)});

			// Prepare for next chunk.
			post_current_file.close(); // Just in case.
			post_header.clear();
			socket.read(&Connection::read_post_header);
			return;
		}
		post_current_file << post_decode(buffer, false);
	} // }}}
	void read_post_header(std::string &buffer) { // {{{
		std::string::size_type bs = post_boundary.size();
		if (buffer.size() < bs + 4) {
			// Not enough data yet.
			return;
		}
		if (buffer.substr(bs, 4) == "--\r\n") {
			// Final boundary; finish up.
			buffer = buffer.substr(bs + 4);
			// call POST callback.
			if (httpd->post_cb != nullptr)
				((httpd->owner)->*(httpd->post_cb))(*this);
			// TODO: unlink all POST files (that are still there).
			reset();
			if (socket)
				socket.read(&Connection::read_header);
			return;
		}

		if (buffer.substr(bs, 2) != "\r\n") {
			// Invalid data.
			WL_log("invalid POST header");
			reply(400, {}, {}, {}, false);
			return;
		}

		// Find end of header.
		std::string::size_type eoh = buffer.find("\r\n\r\n", bs);
		if (eoh == std::string::npos) {
			// End of header not yet found.
			return;
		}

		// Header complete; parse it.
		auto headerlines = split(buffer.substr(bs + 2, eoh - (bs + 2)), -1, 0, "\n");

		for (auto ln: headerlines) {
			assert(!ln.empty());
			if (std::string(" \t\r\n\v\f").find(ln[0]) != std::string::npos) {
				WL_log("refusing continuation in POST content");
				reply(400, {}, {}, {}, false);
				return;
			}
			auto equal = ln.find('=');
			if (equal == std::string::npos) {
				WL_log("no = sign in POST header");
			}
			auto key = lower(strip(ln.substr(0, equal)));
			auto value = strip(ln.substr(equal + 1));
			if (post_header.contains(key)) {
				WL_log("duplicate header in POST content: " + key);
				reply(400, {}, {}, {}, false);
				return;
			}
			auto parts = split(value, 1, 0, ";");
			post_header[key] = {parts[0], parts.size() == 2 ? parse_args(parts[1]) : std::map <std::string, std::string>() };
		}

		if (!post_header.contains("content-type"))
			post_header["content-type"] = {"text/plain", { {"charset", "us-ascii"} } };
		if (!post_header.contains("content-transfer-encoding"))
			post_header["content-transfer-encoding"] = {"7bit", {}};
		else
			post_header["content-transfer-encoding"].first = lower(post_header["content-transfer-encoding"].first);
		if (!post_header.contains("content-disposition") || lower(post_header["content-disposition"].first) != "form-data" || !post_header["content-disposition"].second.contains("name")) {
			WL_log("Content-Disposition must be form-data and contain at least a name");
			reply(400, {}, {}, {}, false);
			return;
		}

		// If there is a filename, allow storing file contents in chunks. Otherwise, read and store content here.
		if (post_header["content-disposition"].second.contains("filename")) {
			buffer = buffer.substr(eoh + 4);
			// Prepare output file.
			post_current_file = write_temp_file(post_header["content-disposition"].second["filename"]);
			socket.read(&Connection::read_post_body);
			return;
		}

		// Check if boundary after body is received yet.
		auto eob = buffer.find(post_boundary, eoh + 2);
		if (eob == std::string::npos) {
			// Not received yet.
			return;
		}
		auto part = buffer.substr(eoh + 4, eob - (eoh + 4));
		buffer = buffer.substr(eob);
		auto body = post_decode(part, true);
		// Store data in post member.
		std::string &name = post_header["content-disposition"].second["name"];
		post_data.emplace(name, PostData{std::move(body), std::move(post_header)});
		post_header.clear();	// Start new chunk with empty header.
		socket.read(&Connection::read_header);
	} // }}}
	void read_header(std::string &buffer) { // {{{
		if (DEBUG > 4)
			WL_log("reading header");
		std::string::size_type p = 0;
		std::list <std::string> lines;
		// Read header lines. {{{
		while (true) {
			auto q = buffer.find('\n', p);
			if (q == std::string::npos) {
				// Header is not complete yet.
				return;
			}
			std::string line = buffer.substr(p, q - p);
			p = q + 1;
			if (line.empty() && lines.empty()) {
				// HTTP says we SHOULD allow at least one empty line before the request, so do that and ignore the empty line.
				continue;
			}
			if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
				if (lines.empty()) {
					WL_log("Error: http request starts with continuation");
					socket.close();
					return;
				}
				else if (lines.size() == 1) {
					// A continuation on the request line MUST be rejected (or ignored) according to HTTP.
					WL_log("Error: http request contains a continuation");
					socket.close();
					return;
				}
				else
					lines.back() += " " + strip(line);
				continue;
			}
			line = strip(line);
			if (line.empty())
				break;
			if (DEBUG > 3)
				WL_log("Header line: " + line);
			lines.push_back(line);
		}
		// }}}
		// Header complete; parse it.
		// Parse request. {{{
		buffer = buffer.substr(p);
		auto request = split(lines.front(), 2);
		if (request.size() != 3 || request[1][0] != '/') {
			WL_log("Warning: ignoring invalid request " + lines.front());
			socket.close();
			return;
		}
		method = upper(request[0]);
		std::string path = request[1];
		http_version = request[2];
		for (auto current_prefix: httpd->proxy) {
			if (startswith(path, "/" + current_prefix + "/") || path == "/" + current_prefix) {
				prefix = "/" + current_prefix;
				break;
			}
		}
		std::string noprefix_path = path.substr(prefix.size());
		if (noprefix_path.empty() || noprefix_path[0] != '/')
			noprefix_path = "/" + noprefix_path;
		// }}}
		// Store attributes. {{{
		for (auto line = ++lines.begin(); line != lines.end(); ++line) {
			p = line->find(':');
			if (p == std::string::npos) {
				WL_log("Warning: ignoring http header without : " + *line);
				continue;
			}
			auto key = lower(strip(line->substr(0, p)));
			auto value = strip(line->substr(p + 1));
			received_headers[key] = value;
			if (DEBUG > 2)
				WL_log("Header field: '" + key + "' = '" + value + "'");
		}
		// }}}
		if (!received_headers.contains("host")) {
			WL_log("Error in request: no Host header");
			socket.close();
			return;
		}
		std::string host = received_headers["host"];
		url = URL(host + noprefix_path);
		// Check if authorization is required and provided. {{{
		auto message = httpd->authentication(*this);
		if (message) {
			// Authentication required; check if it was present.
			auto i = received_headers.find("authorization");
			if (i == received_headers.end()) {
				// No authorization requested; reply 401.
				reply(401, {}, {}, {{"WWW-Authenticate", std::string("Basic realm=\"") + message + "\""}}, true);
				return;
			}
			// Authorization requested; check it.
			auto data = split(i->second, 1);
			if (data.size() != 2 || data[0] != "basic") {
				// Invalid authorization.
				reply(400, {}, {}, {}, true);
				return;
			}
			std::string::size_type pos = 0;
			auto pwdata = b64decode(data[1], pos);
			auto p = pwdata.find(':');
			if (p == std::string::npos) {
				// Invalid authorization.
				reply(400, {}, {}, {}, true);
				return;
			}
			user = pwdata.substr(0, p);
			password = pwdata.substr(p + 1);
			if (!httpd->valid_credentials(*this)) {
				reply(401, {}, {}, {{"WWW-Authenticate", std::string("Basic realm=\"") + message + "\""}}, true);
				return;
			}
		}
		// Authorization successful or not needed.
		// }}}

		// Parse request: handle POST. {{{
		if (method == "POST") {
			// Handle POST requests.
			auto p = received_headers.find("content-type");
			if (p == received_headers.end()) {
				WL_log("No Content-Type found in POST request");
				reply(400, {}, {}, {}, true);
				return;
			}
			auto ct = split(lower(p->second), 1, 0, ";");
			if (ct.size() != 2 || lower(strip(ct[0])) != "multipart/form-data") {
				WL_log("Wrong Content-Type found in POST request (must be multipart/form-data)");
				reply(400, {}, {}, {}, true);
				return;
			}
			// Parse content-type arguments.
			auto args = parse_args(ct[1]);
			// Compute boundary.
			auto b = args.find("boundary");
			if (b == args.end()) {
				WL_log("POST request has no boundary");
				reply(400, {}, {}, {}, false);
				return;
			}

			post_boundary = "\r\n--" + args["boundary"];
			// Start boundary: boundary + "\r\n".
			// End boundary: boundary + "--\r\n".

			// Prepare body for POST boundary search.
			buffer = "\r\n" + buffer;

			// Set read callback to POST handler.
			socket.read(&Connection::read_post_header);
			return;
		} // }}}

		// Handle request. Options:
		// - Create a websocket.
		// - Serve a dynamic page.
		// - Serve a static page.
		auto c = received_headers.find("connection");
		auto u = received_headers.find("upgrade");
		if (c != received_headers.end() && u != received_headers.end() && lower(u->second) == "websocket") {
			// This is probably a websocket. "connection" must include "upgrade".
			auto cc = split(lower(c->second), -1, 0, ",");
			for (auto cci: cc) {
				if (strip(cci) == "upgrade") {
					// This is a websocket.
					auto k = received_headers.find("sec-websocket-key");
					if (method != "GET" || k == received_headers.end()) {
						reply(400, {}, {}, {}, true);
						return;
					}
					std::string key = b64encode(sha1(k->second + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
					reply(101, {}, {}, {{"Sec-WebSocket-Accept", key}, {"Connection", "Upgrade"}, {"Upgrade", "WebSocket"}}, false);
					((httpd->owner)->*(httpd->accept_cb))(*this);
					return;
				}
			}
			// Not a websocket after all.
			WL_log("upgrade: websocket header found, but no connection: upgrade");
		}
		if (DEBUG > 3)
			WL_log("Not a websocket");

		// Attempt to serve a dynamic page.
		if (httpd->page(*this)) {
			// Page has been handled.
			return;
		}

		// This was not a dynamic page; attempt to serve a static page.
		if (httpd->htmldirs.empty()) {
			WL_log("no htmldirs found; returning 501 NOT IMPLEMENTED");
			reply(501, {}, {}, {}, true);
			return;
		}
		// Clean up path: remove initial /; reject a path with .. in it.
		auto decoded_path = URL::decode(url.path);
		if (decoded_path.empty() || decoded_path[0] != '/' || decoded_path.find("/../") != std::string::npos) {
			reply(400, {}, {}, {}, true);
			return;
		}
		decoded_path = decoded_path.substr(1);	// Remove initial '/'.
		for (auto &dir: httpd->htmldirs) {
			std::filesystem::path path = (decoded_path.empty() ? dir : dir / decoded_path);
			if (!std::filesystem::exists(path))
				continue;
			if (!std::filesystem::is_directory(path)) {
				// This is a regular file which exists. Detect mime type.
				auto ext = path.extension();
				if (!httpd->exts.contains(ext)) {
					WL_log(std::string("extension ") + std::string(ext) + " not supported; returning 501 NOT IMPLEMENTED");
					reply(501, {}, {}, {}, true);
					return;
				}
				reply_file(path, httpd->exts[ext]);
				return;
			}
			// Look for index.*
			for (auto &ext: httpd->exts) {
				auto p = path / ("index" + ext.first);
				if (std::filesystem::exists(p)) {
					reply_file(p, ext.second);
					return;
				}
			}
			// Not found; try next htmldirs location.
		}
		// Not found in any location.
		reply(404, {}, {}, {}, true);
		return;
	} // }}}
public:
	Httpd <OwnerType> *httpd;
	Socket <Connection> socket;
	std::map <std::string, std::string> received_headers;
	std::string method;
	URL url;
	std::string http_version;
	std::string user;
	std::string password;
	std::string prefix;
	std::map <std::string, PostData> post_data;
	std::map <std::string, PostFile> post_file;
	void reply(int code, std::string const &message = {}, std::string const &content_type = {}, std::map <std::string, std::string> const &sent_headers = {}, bool close = false) { // Send HTTP status code and headers, and optionally a message.  {{{
		/* Reply to a request for a document.
		There are three ways to call this function:
		* With a message and content_type.  This will serve the data as a normal page.
		* With a code that is not 101, and no message or content_type.  This will send an error.
		* With a code that is 101, and no message or content_type.  This will open a websocket.
		*/
		assert(http_response.contains(code));
		char const *response = http_response[code];
		//WL_log('Debug: sending reply %d %s for %s\n' % (code, httpcodes[code], connection.address.path))
		socket.send((std::ostringstream() << "HTTP/1.1 " << code << " " << response << "\r\n").str());
		std::string the_content_type;
		std::string the_message;
		if (message.empty() && code != 101) {
			assert(content_type.empty());
			the_content_type = "text/html;charset=utf-8";
			the_message = (std::ostringstream() << "<!DOCTYPE html><html><head><meta charset='utf-8'/><title>" << code << ": " << response << "</title></head><body><h1>" << code << ": " << response << "</h1></body></html>").str();
		}
		else {
			the_content_type = content_type;
			the_message = message;
		}
		if (close && !sent_headers.contains("Connection")) {
			socket.send("Connection:close\r\n");
		}
		if (!the_content_type.empty()) {
			socket.send("Content-Type:" + the_content_type + "\r\n");
			socket.send("Content-Length:" + (std::ostringstream() << the_message.size()).str() + "\r\n");
		}
		else {
			assert(code == 101);
			assert(the_message.empty());
		}
		for (auto h: sent_headers)
			socket.send(h.first + ":" + h.second + "\r\n");
		socket.send("\r\n" + the_message);
		if (close)
			socket.close();
	} // }}}
	void reply_file(std::filesystem::path const &path, std::string const &mime) { // {{{
		// This should be called when a request is received (from read_header() or the server's overloaded page() function.
		// It replies 200 OK and serves the file (which must exist) to the client.
		// This reads the entire file into memory first, so it cannot be used for streamed content.
		std::string content;
		std::ifstream file(path, std::ios::binary);
		if (!file.good()) {
			// Service Unavailable.
			reply(503, {}, {}, {}, true);
			return;
		}
		while (true) {
			if (!file.good()) {
				// Internal server error.
				reply(500, {}, {}, {}, true);
				return;
			}
			char part[4096];
			file.read(part, sizeof(part));
			content += std::string(part, file.gcount());
			if (file.eof())
				break;
		}
		// Send OK.
		reply(200, content, mime, {{"Content-Length", std::to_string(content.size())}}, false);

		// Prepare connection for next request.
		reset();
	} // }}}
	Connection(Socket <Connection> *src, Httpd <OwnerType> *httpd) : // {{{
			post_boundary(),
			post_header(),
			httpd(httpd),
			socket(std::move(*src), this),
			received_headers{},
			method{},
			url{},
			http_version{},
			user{},
			password{},
			prefix{},
			post_data{},
			post_file{}
	{
		socket.set_disconnect_cb(&Connection::ignore_disconnect);
		socket.read(&Connection::read_header);
		if (DEBUG > 2)
			WL_log((std::ostringstream() << "new connection from " << socket.url.host << ":" << socket.url.service).str());
	} // }}}
}; // }}}
// }}}

// RPC. {{{
template <class UserType>
class RPC { // {{{
	/* Remote Procedure Call over Websocket.
	This class manages a communication object, and on the other end of the
	connection a similar object should exist.  When calling a member of
	this class, the request is sent to the remote object and the function
	is called there.  The return value is sent back and returned to the
	caller.  Exceptions are also propagated.  Instead of calling the
	method, the item operator can be used, or the event member:
	obj.remote_function(...) calls the function and waits for the return
	value; obj.remote_function[...] or obj.remote_function.event(...) will
	return immediately and ignore the return value.

	If no communication object is given in the constructor, any calls that
	the remote end attempts will fail.
	*/
public:
	typedef void (UserType::*BgReply)(std::shared_ptr <WebObject>);
	typedef coroutine (UserType::*Published)(Args args, KwArgs kwargs);
	typedef coroutine (UserType::*PublishedFallback)(std::string const &target, Args args, KwArgs kwargs);
	typedef void (UserType::*DisconnectCb)();
	typedef void (UserType::*ErrorCb)(std::string const &message);
	struct Call {
		std::shared_ptr <WebObject> code;
		std::string target;
		Args args;
		KwArgs kwargs;
		UserType *user;
	};
private:
	DisconnectCb disconnect_cb;
	ErrorCb error_cb;
	Loop::IdleHandle activation_handle;
	bool activated;
	UserType *user;

	// Members for handling calls to remote.
	int reply_index;	// Index that was passed with last command. Auto-increments on each call.
	std::map <int, BgReply> expecting_reply_bg;	// Data for pending bgcalls, by reply_index.
	std::map <int, coroutine::handle_type> expecting_reply_fg;	// Data for pending fgcalls, by reply_index.

	// Members for handling calls from remote.
	std::list <Call> delayed_calls;	// Pending received calls, to be made when socket is activated.
	struct CalledData { // {{{
		std::list <CalledData>::iterator iterator;
		RPC <UserType> *rpc;
		int id;
		void called_return(std::shared_ptr <WebObject> ret);
	}; // }}}
	std::list <CalledData> called_data;

	int get_index();
	bool activate();
	void recv(std::string const &frame);
	void send(std::string const &code, std::shared_ptr <WebObject> object);
	void called(std::shared_ptr <WebObject> id, std::string const &target, std::shared_ptr <WebVector> args, std::shared_ptr <WebMap> kwargs);
	void disconnect_handler() { if (disconnect_cb) (user->*disconnect_cb)(); }
	void error_handler(std::string const &message) { if (error_cb) (user->*error_cb)(message); }
public:
	Websocket <RPC <UserType> > websocket;

	void set_disconnect_cb(DisconnectCb cb) { disconnect_cb = cb; }
	void set_error_cb(ErrorCb cb) { error_cb = cb; }
	void disconnect() { websocket.disconnect(); }

	// Empty constructor, for moving a connected object into.
	RPC() : disconnect_cb(nullptr), error_cb(nullptr), activation_handle(Loop::get()->invalid_idle()), activated(false), user(nullptr), reply_index(0), expecting_reply_bg{}, expecting_reply_fg{}, delayed_calls{}, called_data{}, websocket{} {}
	// Constructor to connect to host.
	RPC(std::string const &address, UserType *user = nullptr, Websocket <RPC <UserType> >::ConnectSettings const &connect_settings = {}, Websocket <RPC <UserType> >::RunSettings const &run_settings = {.loop = nullptr, .keepalive = 50s});
	~RPC() { // {{{
		STARTFUNC;
		if (activation_handle != Loop::get()->invalid_idle())
			Loop::get(websocket.run_settings.loop)->remove_idle(activation_handle);
		activation_handle = Loop::get()->invalid_idle();
	} // }}}

	RPC(RPC <UserType> &&other) : disconnect_cb(other.disconnect_cb), error_cb(other.error_cb), activation_handle(Loop::get()->invalid_idle()), activated(other.activated), user(other.user), reply_index(other.reply_index), expecting_reply_bg(std::move(other.expecting_reply_bg)), expecting_reply_fg(std::move(other.expecting_reply_fg)), delayed_calls(std::move(other.delayed_calls)), called_data(std::move(other.called_data)), websocket(std::move(other.websocket)) { // {{{
		STARTFUNC;
		websocket.update_user(this);
		if (other.activation_handle != Loop::get()->invalid_idle()) {
			Webloop::Loop::get(websocket.run_settings.loop)->remove_idle(other.activation_handle);
			other.activation_handle = Loop::get()->invalid_idle();
			activation_handle = Loop::get(websocket.run_settings.loop)->add_idle(Loop::IdleRecord(this, &RPC <UserType>::activate));
		}
	} // }}}

	RPC <UserType> &operator=(RPC <UserType> &&other) { // {{{
		STARTFUNC;
		if (activation_handle != Loop::get()->invalid_idle()) {
			Loop::get(websocket.run_settings.loop)->remove_idle(activation_handle);
			activation_handle = Loop::get()->invalid_idle();
		}
		disconnect_cb = other.disconnect_cb;
		error_cb = other.error_cb;
		activated = other.activated;
		user = other.user;
		reply_index = other.reply_index;
		expecting_reply_bg = std::move(other.expecting_reply_bg);
		expecting_reply_fg = std::move(other.expecting_reply_fg);
		delayed_calls = std::move(other.delayed_calls);
		called_data = std::move(other.called_data);
		websocket = std::move(other.websocket);
		websocket.update_user(this);
		if (other.activation_handle != Loop::get()->invalid_idle()) {
			Webloop::Loop::get(websocket.run_settings.loop)->remove_idle(other.activation_handle);
			other.activation_handle = Loop::get()->invalid_idle();
			activation_handle = Loop::get(websocket.run_settings.loop)->add_idle(Loop::IdleRecord(this, &RPC <UserType>::activate));
		}
		return *this;
	} // }}}

	void update_user(UserType *new_user) { user = new_user; }

	// Constructor for use by accepted sockets through Httpd.
	template <class ConnectionType> explicit RPC(ConnectionType &connection, UserType *user);

	// RPC calls.
	void bgcall(std::string const &target, std::shared_ptr <WebVector> args = {}, std::shared_ptr <WebMap> kwargs = {}, BgReply reply = nullptr);
	coroutine fgcall(std::string const &target, std::shared_ptr <WebVector> args = {}, std::shared_ptr <WebMap> kwargs = {});

	// For debugging.
	void print_expecting(std::string const &msg) {
		//*
		WL_log("expecting for " + msg + " of " + websocket.get_name() + ":");
		for (auto fgid: expecting_reply_fg)
			WL_log("fg: " + std::to_string(fgid.first));
		for (auto bgid: expecting_reply_bg)
			WL_log("bg: " + std::to_string(bgid.first));
		WL_log("end of list");
		//*/
	}
}; // }}}

// RPC internals. {{{
template <class UserType>
int RPC <UserType>::get_index() { // {{{
	STARTFUNC;
	WL_log("this: " + std::to_string((long)this));
	++reply_index;
	//print_expecting("creating reply index");
	while (expecting_reply_bg.contains(reply_index) || expecting_reply_fg.contains(reply_index) || reply_index == 0)
		++reply_index;
	return reply_index;
} // }}}

template <class UserType>
bool RPC <UserType>::activate() { // {{{
	STARTFUNC;
	/* Internal use only.  Activate the websocket; send initial frames.
	@return None.
	*/
	activation_handle = Loop::get()->invalid_idle();
	while (!delayed_calls.empty()) {
		auto calls = std::move(delayed_calls);
		delayed_calls.clear();
		for (auto this_call: calls)
			called(this_call.code, this_call.target, this_call.args, this_call.kwargs);
	}
	activated = true;
	return false;
} // }}}

template <class UserType>
void RPC <UserType>::recv(std::string const &frame) { // {{{
	STARTFUNC;
	/* Receive a websocket packet.
	@param frame: The packet.
	@return None.
	*/
	if (DEBUG > 2)
		WL_log("frame: " + frame);
	auto data = WebObject::load(frame);
	if (DEBUG > 1)
		WL_log("packet received: " + data->print());
	if (data->get_type() != WebObject::VECTOR) {
		// Don't send errors back for unknown things, to lower risk of loops.
		WL_log("error: frame is not a WebVector");
		return;
	}
	auto vdata = data->as_vector();
	auto length = vdata->size();
	if (length < 1 || (*vdata)[0]->get_type() != WebObject::STRING) {
		// Don't send errors back for unknown things, to lower risk of loops.
		WL_log("error: frame does not start with a packet type string");
		return;
	}
	std::string const &ptype = *(*vdata)[0]->as_string();

	if (ptype == "error") { // string:"error", int:id, string:message {{{
		// Returning error on error is a looping risk, so any errors here are only logged to the user, not sent over the network.
		if (DEBUG > 0)
			WL_log("error frame received");
		if (length != 2 && length != 3) {
			WL_log("not exactly 1 or 2 arguments received with error");
			return;
		}
		auto payload = (*vdata)[length - 1];
		if (payload->get_type() != WebObject::STRING) {
			WL_log("error payload is not a string");
			return;
		}

		// Remove request from queue if an id was provided.
		int id = 0;
		if (length == 3) {
			WL_log("error received");
			print_expecting("error received");
			auto idobj = (*vdata)[1];
			if (idobj->get_type() != WebObject::INT) {
				WL_log("error id is not int");
				return;
			}
			id = *idobj->as_int();
			auto p = expecting_reply_bg.find(id);
			if (p != expecting_reply_bg.end())
				expecting_reply_bg.erase(p);
			else {
				auto q = expecting_reply_fg.find(id);
				if (q != expecting_reply_fg.end())
					expecting_reply_fg.erase(q);
				else
					WL_log("warning: error reply for unknown id");
			}
		}

		// Call error handler or throw exception.
		std::string const &msg = *payload->as_string();
		if (error_cb != nullptr)
			(user->*error_cb)(msg);
		else
			throw msg;
		return;
	} // }}}

	if (ptype == "return") { // string:"return", int:id, WebObject:value {{{
		if (DEBUG > 2)
			WL_log("return received");
		//print_expecting("return received");
		if (length != 2) {
			WL_log("not exactly 1 argument received with return");
			return;
		}
		auto argobj = (*vdata)[1];
		if (argobj->get_type() != WebObject::VECTOR) {
			WL_log("return argument is not vector");
			return;
		}
		auto arg = argobj->as_vector();
		if (arg->size() != 2) {
			WL_log("return argument is not length 2");
			return;
		}
		auto idobj = (*arg)[0];
		auto payload = (*arg)[1];
		if (idobj->get_type() != WebObject::INT) {
			WL_log("return id is not int");
			return;
		}
		int id = *idobj->as_int();

		auto p = expecting_reply_bg.find(id);
		if (p == expecting_reply_bg.end()) {
			auto q = expecting_reply_fg.find(id);
			if (q == expecting_reply_fg.end()) {
				WL_log("invalid return id received: " + std::to_string(id));
				print_expecting("invalid return id");
				return;
			}
			// This is the return call of a fgcall().
			auto handle = q->second;
			expecting_reply_fg.erase(q);
			coroutine::activate(&handle, payload);
			return;
		}

		// This is the return call of a bgcall().
		auto target = p->second;
		expecting_reply_bg.erase(p);
		(user->*target)(payload);
		return;
	} // }}}

	if (ptype == "call") { // string:"call", int:id, string:target, WebVector:args, WebMap:kwargs {{{
		if (length != 2 || (*vdata)[1]->get_type() != WebObject::VECTOR) {
			WL_log("call did not get only a vector argument");
			return;
		}
		auto arg = (*vdata)[1]->as_vector();
		if (arg->size() != 4) {
			WL_log("call argument did not have exactly 4 elements");
			return;
		}
		auto idobj = (*arg)[0];
		auto targetobj = (*arg)[1];
		auto argsobj = (*arg)[2];
		auto kwargsobj = (*arg)[3];
		if (targetobj->get_type() != WebObject::STRING) {
			WL_log("call target is not string");
			return;
		}
		if (argsobj->get_type() != WebObject::VECTOR) {
			WL_log("call args is not vector");
			return;
		}
		if (kwargsobj->get_type() != WebObject::MAP) {
			WL_log("call kwargs is not map");
			return;
		}
		std::string target = *targetobj->as_string();
		auto args = std::dynamic_pointer_cast <WebVector> (argsobj);
		auto kwargs = std::dynamic_pointer_cast <WebMap> (kwargsobj);
		auto idtype = idobj->get_type();
		if (idtype != WebObject::INT && idtype != WebObject::NONE) {
			WL_log("call id is not int or none");
			return;
		}
		if (activated) {
			try {
				called(idobj, target, args, kwargs);
			}
			catch (...) {
				WL_log("error: remote call failed");
				send("error", WebVector::create(idobj, WebString::create("remote call failed")));
			}
		}
		else
			delayed_calls.push_back(Call(idobj, target, args, kwargs, user));
		return;
	} // }}}

	WL_log("error: invalid RPC command");
} // }}}

template <class UserType>
void RPC <UserType>::send(std::string const &code, std::shared_ptr <WebObject> object) { // {{{
	STARTFUNC;
	/* Send an RPC packet.
	@param type: The packet type.
		One of "return", "error", "call".
	@param object: The data to send.
		Return value, error message, or function arguments.
	*/
	if (DEBUG > 1)
		WL_log((std::ostringstream() << "sending: " << " " << object->print()).str());
	auto obj = WebVector::create(WebString::create(code), object);
	websocket.send(obj->dump());
} // }}}

template <class UserType>
void RPC <UserType>::CalledData::called_return(std::shared_ptr <WebObject> ret) { // {{{
	STARTFUNC;
	// "this" is invalidated by the erase() call, so do that last.
	rpc->send("return", WebVector::create(WebInt::create(id), ret));
	rpc->called_data.erase(iterator);
} // }}}

template <class UserType>
void RPC <UserType>::called(std::shared_ptr <WebObject> id, std::string const &target, std::shared_ptr <WebVector> args, std::shared_ptr <WebMap> kwargs) { // {{{
	STARTFUNC;
	/* Make local function call at remote request.
	The local function may be a generator, in which case the call
	will return when it finishes.  Yielded values are ignored.
	@param reply: Return code, or None for event.
	@param member: Requested function name.
	@param a: Arguments.
	@param ka: Keyword arguments.
	@return None.
	*/

	// Try to call target coroutine.
	auto pub = user->published;
	if (!pub)
		throw "no targets defined; unable to call";
	auto co = pub->find(target);
	if (co == pub->end() && user->published_fallback == nullptr) {
		// Fail.
		throw "trying to call unregistered target";
	}
	auto c = (co == pub->end() ? (user->*user->published_fallback)(target, args, kwargs) : (user->*co->second)(args, kwargs));
	if (id->get_type() != WebObject::NONE) {
		called_data.emplace_back(called_data.end(), this, int(*id->as_int()));
		called_data.back().iterator = --called_data.end();
		c.set_cb(&called_data.back(), &CalledData::called_return);
	}
	c();	// Start coroutine.
	return;
} // }}}

template <class UserType>
RPC <UserType>::RPC(std::string const &address, UserType *user, Websocket <RPC <UserType> >::ConnectSettings const &connect_settings, Websocket <RPC <UserType> >::RunSettings const &run_settings) : // {{{
		disconnect_cb(),
		error_cb(),
		activation_handle(Loop::get()->invalid_idle()),
		activated(false),
		user(user),
		reply_index(0),
		expecting_reply_bg(),
		expecting_reply_fg(),
		delayed_calls{},
		called_data(),
		websocket(address, connect_settings, this, &RPC <UserType>::recv, run_settings)
{
	STARTFUNC;
	/* Create a new RPC object.  Extra parameters are passed to the
	Websocket constructor, which passes its extra parameters to the
	network.Socket constructor.
	@param port: Host and port to connect to, same format as
		python-network uses.
	@param recv: Function (or class) that receives this object as
		an argument and returns a communication object.
	*/
	// Note: not thread-safe.
	websocket.set_disconnect_cb(&RPC <UserType>::disconnect_handler);
	websocket.set_error_cb(&RPC <UserType>::error_handler);
	activation_handle = Loop::get(run_settings.loop)->add_idle(Loop::IdleRecord(this, &RPC <UserType>::activate));
} // }}}

template <class UserType> template <class ConnectionType>
RPC <UserType>::RPC(ConnectionType &connection, UserType *user) : // {{{
		disconnect_cb(),
		error_cb(),
		activation_handle(Loop::get()->invalid_idle()),
		activated(true),
		user(user),
		reply_index(0),
		expecting_reply_bg(),
		expecting_reply_fg(),
		delayed_calls{},
		called_data(),
		websocket(std::move(connection.socket), this, &RPC <UserType>::recv, {connection.httpd->get_loop(), connection.httpd->get_keepalive()})
{
	STARTFUNC;
	websocket.set_disconnect_cb(&RPC <UserType>::disconnect_handler);
	websocket.set_error_cb(&RPC <UserType>::error_handler);
} // }}}

template <class UserType>
void RPC <UserType>::bgcall( // {{{
		std::string const &target,
		std::shared_ptr <WebVector> args,
		std::shared_ptr <WebMap> kwargs,
		BgReply reply
) {
	STARTFUNC;
	//typedef void (*Reply)(std::shared_ptr <WebObject>, void *self_ptr);
	if (!args)
		args = WebVector::create();
	if (!kwargs)
		kwargs = WebMap::create();
	int index;
	if (reply) {
		index = get_index();
		expecting_reply_bg[index] = reply;
	}
	else
		index = 0;
	if (DEBUG > 3)
		WL_log("sending bg call");
	//print_expecting("send bg call");
	send("call", WebVector::create(index == 0 ? std::dynamic_pointer_cast <WebObject> (WebNone::create()) : std::dynamic_pointer_cast <WebObject> (WebInt::create(index)), WebString::create(target), args, kwargs));
} // }}}

template <class UserType>
coroutine RPC <UserType>::fgcall(std::string const &target, std::shared_ptr <WebVector> args, std::shared_ptr <WebMap> kwargs) { // {{{
	STARTFUNC;
	if (!args)
		args = WebVector::create();
	if (!kwargs)
		kwargs = WebMap::create();
	WL_log("this: " + std::to_string((long)this));
	int index = get_index();
	expecting_reply_fg[index] = GetHandle();
	if (DEBUG > 4)
		WL_log("sending fg call");
	//print_expecting("send fg call");
	send("call", WebVector::create(WebInt::create(index), WebString::create(target), args, kwargs));
	auto ret = Yield(WebNone::create());
	if (DEBUG > 4)
		WL_log("fgcall returns " + (ret ? ret->print() : "nothing"));
	co_return ret;
} // }}}
// }}}
// }}}

} 

#endif

// vim: set fileencoding=utf-8 foldmethod=marker :
