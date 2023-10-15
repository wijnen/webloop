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

/*
Some thoughts about this code; probably obsolete when you read them.

An http server can be started for several reasons. example use cases:
A - control of heater: server own opentherm connection; clients are single shot "get state" or "set target temperature".
B - game: clients have a session. Reconnect should be possible, perhaps with timeout.

a Server is started in both cases. For A, requests are handled directly. If websockets are used, they carry no state, but are only used for broadcasts.
For B, each user has its own session object. The session contains a Socket, which may be closed (in which case the user can reconnect).

Currently, a Socket is owned by the Server. This is wrong?

What happens when the Socket disconnects?
- Socket is unregistered from loop.
- Server removes it from its list, if that exists.
- Owner should handle this event. Until it does, the Socket should be valid in a disconnected state. (This also allows a move constructor, which is good.)

*/

/* Documentation. {{{
'''@mainpage
This module can be used to create websockets servers and clients.  A websocket
client is an HTTP connection which uses the headers to initiate a protocol
change.  The server is a web server which serves web pages, and also responds
to the protocol change headers that clients can use to set up a websocket.

Note that the server is not optimized for high traffic.  If you need that, use
something like Apache to handle all the other content and set up a virtual
proxy to this server just for the websocket.

In addition to implementing the protocol, this module contains a simple system
to use websockets for making remote procedure calls (RPC).  This system allows
the called procedures to be generators, so they can yield control to the main
program and continue running when they need to.  This system can also be used
locally by using call().
'''

'''@file
This module can be used to create websockets servers and clients.  A websocket
client is an HTTP connection which uses the headers to initiate a protocol
change.  The server is a web server which serves web pages, and also responds
to the protocol change headers that clients can use to set up a websocket.

Note that the server is not optimized for high traffic.  If you need that, use
something like Apache to handle all the other content and set up a virtual
proxy to this server just for the websocket.

In addition to implementing the protocol, this module contains a simple system
to use websockets for making remote procedure calls (RPC).  This system allows
the called procedures to be generators, so they can yield control to the main
program and continue running when they need to.  This system can also be used
locally by using call().
'''

'''@package websocketd Client WebSockets and webserver with WebSockets support
This module can be used to create websockets servers and clients.  A websocket
client is an HTTP connection which uses the headers to initiate a protocol
change.  The server is a web server which serves web pages, and also responds
to the protocol change headers that clients can use to set up a websocket.

Note that the server is not optimized for high traffic.  If you need that, use
something like Apache to handle all the other content and set up a virtual
proxy to this server just for the websocket.

In addition to implementing the protocol, this module contains a simple system
to use websockets for making remote procedure calls (RPC).  This system allows
the called procedures to be generators, so they can yield control to the main
program and continue running when they need to.  This system can also be used
locally by using call().
'''
# }}} */

// includes.  {{{
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>
#include "network.hh"
#include "webobject.hh"
#include "coroutine.hh"
#include "url.hh"
#include "tools.hh"
#include "loop.hh"
// }}}

template <class UserType>
class Websocket { // {{{
public:
	typedef void (UserType::*Receiver)(std::string const &data);
private:
	// Main class implementing the websocket protocol.
	Socket <Websocket <UserType> > socket;
	std::string buffer;
	std::string fragments;
	Loop::TimeoutHandle keepalive_handle;
	bool is_closed;
	bool pong_seen;	// true if a pong was seen since last ping command.
	uint8_t current_opcode;
	Receiver receiver;
	bool send_mask;		// whether masks are used to sent data (true for client, false for server).
	UserType *user;				// callback functions are called on this object.
	void inject(std::string &data);	// Make the class handle incoming data.
public:
	// Settings for connecting (ignored for accepted websockets).
	struct ConnectSettings {
		std::string method;				// default: GET
		std::string user;				// login name; if both this and password are empty: don't send them.
		std::string password;				// login password
		std::map <std::string, std::string> sent_headers;	// extra headers, sent to server.
	};
	// Settings used for all websockets (both connected and accepted).
	struct RunSettings {
		Loop *loop;					// Main loop to use; usually nullptr, so the default is used.
		Loop::Duration keepalive;			// keepalive timer.
	};
	ConnectSettings connect_settings;
	RunSettings run_settings;
	std::map <std::string, std::string> received_headers;	// Headers that are received from server. (Only for accepted websockets.)
	void disconnect();
	Websocket() {}
	Websocket(std::string const &address, ConnectSettings const &connect_settings = {}, UserType *user = nullptr, Receiver receiver = nullptr, RunSettings const &run_settings = {});
	Websocket(int socket_fd, UserType *user = nullptr, Receiver receiver = {}, RunSettings const &run_settings = {});
	template <class ServerType>
	Websocket(Socket <ServerType> &&src, UserType *user = nullptr, Receiver receiver = nullptr, RunSettings const &run_settings = {});
	// TODO: move constructor and assignment.

	bool keepalive();
	void send(std::string const &data, int opcode = 1); // Send a WebSocket frame.
	bool ping(std::string const &data = std::string()); // Send a ping; return False if no pong was seen for previous ping.  Other received packets also count as a pong.
	void close(); // Close a WebSocket.  (Use socket.close for other connections.)
}; // }}}

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
	typedef std::shared_ptr <WebVector> Args;
	typedef std::shared_ptr <WebMap> KwArgs;
	typedef coroutine (UserType::*Published)(Args args, KwArgs kwargs);
	struct Call {
		int code;
		std::string target;
		Args args;
		KwArgs kwargs;
		UserType *user;
	};
	typedef void (UserType::*ErrorCb)(std::string const &message);
	struct BgReplyData {
		UserType *caller;
		BgReply reply;
	};
private:
	Websocket <RPC <UserType> > websocket;
	ErrorCb error;
	Loop::IdleHandle activation_handle;
	bool activated;
	UserType *user;

	// Members for handling calls to remote.
	int reply_index;	// Index that was passed with last command. Auto-increments on each call.
	std::map <int, BgReplyData> expecting_reply_bg;	// Data for pending bgcalls, by reply_index.
	std::map <int, coroutine::handle_type> expecting_reply_fg;	// Data for pending fgcalls, by reply_index.

	// Members for handling calls from remote.
	std::map <std::string, Published> published;	// Available targets.
	std::list <Call> delayed_calls;	// Pending received calls, to be made when socket is activated.
	struct CalledData {
		std::list <CalledData>::iterator iterator;
		RPC <UserType> *rpc;
		int id;
		void called_return(std::shared_ptr <WebObject> ret);
	};
	std::list <CalledData> called_data;

	int get_index();
	bool activate();
	void recv(std::string const &frame);
	void send(std::string const &code, std::shared_ptr <WebObject> object);
	void called(int id, std::string const &target, std::shared_ptr <WebVector> args, std::shared_ptr <WebMap> kwargs);
public:
	RPC(std::string const &address, std::map <std::string, Published> const &published = {}, ErrorCb error = {}, UserType *user = nullptr, Websocket <RPC <UserType> >::ConnectSettings const &connect_settings = {.method = "GET", .user = {}, .password = {}, .sent_headers = {}}, Websocket <RPC <UserType> >::RunSettings const &run_settings = {.loop = nullptr, .keepalive = 50s});
	void bgcall(std::string const &target, std::shared_ptr <WebVector> args, std::shared_ptr <WebMap> kwargs, BgReply reply = nullptr);
	coroutine fgcall(std::string const &target, std::shared_ptr <WebVector> args, std::shared_ptr <WebMap> kwargs);
}; // }}}

template <class UserType>
class Httpd { // {{{
	/* HTTP server.
	This object implements an HTTP server.  It supports GET and
	POST, and of course websockets.
	*/
public:
	class Connection;
private:
	friend class Connection;
	std::vector <std::string> httpdirs;		// Directories where static web pages are searched.
	std::vector <std::string> proxy;		// Proxy prefixes which are ignored when received.
	typedef void (UserType::*ClosedCb)();
	typedef RPC <UserType>::ErrorCb ErrorCb;
	ClosedCb closed_cb;				// Closed callback.
	ErrorCb error_cb;				// Error callback.
	UserType *user;					// User data to send with callbacks.
	std::map <std::string, std::string> exts;	// Handled extensions; value is mime type.
	Loop *loop;					// Main loop for registering read events.
	Server <Connection, Httpd <UserType> > server;	// Network server which provides the interface.
	std::list <Connection> connections;		// Active connections, including non-websockets.
	std::list <UserType> websockets;		// Active websockets.
	void new_connection(Socket <UserType> &&remote);
	virtual char const *authentication(Connection &connection) { (void)&connection; return nullptr; }	// Override to require authentication.
	virtual bool valid_credentials(Connection &connection) { (void)&connection; return true; }	// Override to check credentials.
	virtual bool page(Connection &connection) { (void)&connection; return false; }	// Override and return true to provide dynamic pages.
	void server_closed() { // {{{
		log("Server closed");
		if (closed_cb)
			(user->*closed_cb)();
	} // }}}
	void server_error(std::string const &message) { // {{{
		log("Error received by server: " + message);
		if (error_cb)
			(user->*error_cb)(message);
	} // }}}
	void create_connection(Socket <Connection> *socket) { // {{{
		connections.emplace_back(std::move(socket), this);
	} // }}}
public:
	Httpd(std::string const &service, std::vector <std::string> const &httpdirs = {}, std::vector <std::string> const &proxy = {}, Loop *loop = nullptr, int backlog = 5) : // {{{
			httpdirs(httpdirs),
			proxy(proxy),
			closed_cb(),
			error_cb(),
			user(nullptr),
			exts(),
			loop(Loop::get(loop)),
			server(service, this, &Httpd <UserType>::create_connection, &Httpd <UserType>::server_closed, &Httpd <UserType>::server_error, loop, backlog),
			connections{},
			websockets{}
	{
		/* Create a webserver.
		Additional arguments are passed to the network.Server.
		@param port: Port to listen on.  Same format as in
			python-network.
		@param httpdirs: Locations of static web pages to
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
					if (duplicate.contains(parts[i]))
						continue;

					// If this is a new duplicate, remove it and ignore it.
					auto p = exts.find(parts[i]);
					if (p != exts.end()) {
						duplicate.insert(parts[i]);
						exts.erase(p);
						continue;
					}

					// Otherwise, insert it in the map.
					if (parts[0].substr(0, 5) == "text/" || parts[0] == "application/javascript")
						exts[parts[i]] = parts[0] + ";charset=utf-8";
					else
						exts[parts[i]] = parts[0];
				}
			}
		}
		else {
			// This is probably a Windows system; use some defaults.
			exts = {
				{"html", "text/html;charset=utf-8"},
				{"css", "text/css;charset=utf-8"},
				{"js", "text/javascript;charset=utf-8"},
				{"jpg", "image/jpeg"},
				{"jpeg", "image/jpeg"},
				{"png", "image/png"},
				{"bmp", "image/bmp"},
				{"gif", "image/gif"},
				{"pdf", "application/pdf"},
				{"svg", "image/svg+xml"},
				{"txt", "text/plain;charset=utf-8"}
			};
		} // }}}
	} // }}}
	/*def page(self, connection, path = None):	// A non-WebSocket page was requested.  Use connection.address, connection.method, connection.query, connection.headers and connection.body (which should be empty) to find out more.  {{{
		/ * Serve a non-websocket page.
		Overload this function for custom behavior.  Call this
		function from the overloaded function if you want the
		default functionality in some cases.
		@param connection: The connection that requests the
			page.  Attributes of interest are
			connection.address, connection.method,
			connection.query, connection.headers and
			connection.body (which should be empty).
		@param path: The requested file.
		@return True to keep the connection open after this
			request, False to close it.
		* /
		if self.httpdirs is None:
			self.reply(connection, 501)
			return
		if path is None:
			path = connection.address.path
		if path == '/':
			address = "index"
		else:
			address = '/' + unquote(path) + '/'
			while '/../' in address:
				// Don't handle this; just ignore it.
				pos = address.index('/../')
				address = address[:pos] + address[pos + 3:]
			address = address[1:-1]
		if '.' in address.rsplit('/', 1)[-1]:
			base, ext = address.rsplit('.', 1)
			base = base.strip('/')
			if ext not in self.exts and None not in self.exts:
				log('not serving unknown extension %s' % ext)
				self.reply(connection, 404)
				return
			for d in self.httpdirs:
				filename = os.path.join(d, base + os.extsep + ext)
				if os.path.exists(filename):
					break
			else:
				log('file %s not found in %s' % (base + os.extsep + ext, ', '.join(self.httpdirs)))
				self.reply(connection, 404)
				return
		else:
			base = address.strip('/')
			for ext in self.exts:
				for d in self.httpdirs:
					filename = os.path.join(d, base if ext is None else base + os.extsep + ext)
					if os.path.exists(filename):
						break
				else:
					continue
				break
			else:
				log('no file %s (with supported extension) found in %s' % (base, ', '.join(self.httpdirs)))
				self.reply(connection, 404)
				return
		return self.exts[ext](connection, open(filename, 'rb').read())
	// }}} */
}; // }}}

template <class UserType>
class Httpd <UserType>::Connection { // {{{
	/* Connection object for an HTTP server.
	This object implements the internals of an HTTP server.  It
	supports GET and POST, and of course websockets.  Don't
	construct these objects directly.
	*/
	friend class Httpd <UserType>;
	static std::map <int, char const *> http_response;	// TODO: this is duplicated if the template is used multiple times; it shouldn't be.
	Httpd <UserType> *httpd;
	Socket <Connection> socket;
public:
	std::map <std::string, std::string> received_headers;
	std::string method;
	URL url;
	std::string http_version;
	std::string user;
	std::string password;
	std::string prefix;
	void ignore_disconnect() {}
	void read_header(std::string &buffer) { // {{{
		log("reading header");
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
					log("Error: http request starts with continuation");
					socket.close();
					return;
				}
				else if (lines.size() == 1) {
					// A continuation on the request line MUST be rejected (or ignored) according to HTTP.
					log("Error: http request contains a continuation");
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
			lines.push_back(line);
		}
		// }}}
		// Header complete; parse it.
		// Parse request. {{{
		buffer = buffer.substr(p);
		auto request = split(lines.front(), 2);
		if (request.size() != 3 || request[1][0] != '/') {
			log("Warning: ignoring invalid request " + lines.front());
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
				log("Warning: ignoring http header without : " + *line);
				continue;
			}
			auto key = lower(strip(line->substr(0, p)));
			auto value = strip(line->substr(p + 1));
			received_headers[key] = value;
		}
		// }}}
		if (!received_headers.contains("host")) {
			log("Error in request: no Host header");
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
			auto pwdata = data[1]; // TODO:b64decode(data[1]);
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

		// Parse request: handle POST. TODO {{{
		if (method == "POST") {
			// TODO
			/*if self.method.upper() == 'POST':
				if 'content-type' not in self.headers or self.headers['content-type'].lower().split(';')[0].strip() != 'multipart/form-data':
					log('Invalid Content-Type for POST; must be multipart/form-data (not %s)\n' % (self.headers['content-type'] if 'content-type' in self.headers else 'undefined'))
					self.server.reply(self, 500, close = True)
					return
				args = self._parse_args(self.headers['content-type'])[1]
				if 'boundary' not in args:
					log('Invalid Content-Type for POST: missing boundary in %s\n' % (self.headers['content-type'] if 'content-type' in self.headers else 'undefined'))
					self.server.reply(self, 500, close = True)
					return
				self.boundary = b'\r\n' + b'--' + args['boundary'].encode('utf-8') + b'\r\n'
				self.endboundary = b'\r\n' + b'--' + args['boundary'].encode('utf-8') + b'--\r\n'
				self.post_state = None
				self.post = [{}, {}]
				self.socket.read(self._post)
				self._post(b'')
				log('Warning: ignoring POST request.')
				self.reply(connection, 501)
				return False
			 */
		} // }}}

		// Handle request. Options:
		// - Serve a static page.
		// - Serve a dynamic page.
		// - Create a websocket.
		auto c = received_headers.find("connection");
		auto u = received_headers.find("upgrade");
		if (c != received_headers.end() && u != received_headers.end() && lower(c->second) == "upgrade" && lower(u->second) == "websocket") {
			// This is a websocket.
			auto k = received_headers.find("sec-websocket-key");
			if (method != "GET" || k == received_headers.end()) {
				reply(400, {}, {}, {}, true);
				return;
			}
			// TODO.
			// newkey = base64.b64encode(hashlib.sha1(self.headers['sec-websocket-key'].strip().encode('utf-8') + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest()).decode('utf-8')
			// headers = {'Sec-WebSocket-Accept': newkey, 'Connection': 'Upgrade', 'Upgrade': 'WebSocket'}
			// self.server.reply(self, 101, None, None, headers, close = False)
			reply(101, {}, {}, {}, false);
			httpd->websockets.emplace_back(std::move(socket), httpd);
			return;
		}
		// Attempt to serve a dynamic page.
		if (!httpd->page(*this)) {
			// This was not a dynamic page; attempt to serve a static page.
			// TODO.
			socket.close();
			return;
		}
	} // }}}
	bool connection_error() { // {{{
		// TODO
		return false;
	} // }}}
	Connection(Socket <Connection> *src, Httpd *httpd) : // {{{
			httpd(httpd),
			socket(std::move(*src), this),
			received_headers{},
			method{},
			url{},
			http_version{},
			prefix{}
	{
		socket.set_disconnect_cb(&Connection::ignore_disconnect);
		socket.read(&Connection::read_header);
		if (DEBUG > 2)
			log((std::ostringstream() << "new connection from " << socket.url.host << ":" << socket.url.service).str());
	} // }}}
	void reply(int code, std::string const &message = {}, std::string const &content_type = {}, std::map <std::string, std::string> const &sent_headers = {}, bool close = false) { // Send HTTP status code and headers, and optionally a message.  {{{
		/* Reply to a request for a document.
		There are three ways to call this function:
		* With a message and content_type.  This will serve the data as a normal page.
		* With a code that is not 101, and no message or content_type.  This will send an error.
		* With a code that is 101, and no message or content_type.  This will open a websocket.
		*/
		assert(http_response.contains(code));
		char const *response = http_response[code];
		//log('Debug: sending reply %d %s for %s\n' % (code, httpcodes[code], connection.address.path))
		socket.send((std::ostringstream() << "HTTP/1.1 " << code << " " << response << "\n").str());
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
		/* POST internals (removed for now)
	def _parse_args(self, header): # {{{
		if ';' not in header:
			return (header.strip(), {})
		pos = header.index(';') + 1
		main = header[:pos].strip()
		ret = {}
		while pos < len(header):
			if '=' not in header[pos:]:
				if header[pos:].strip() != '':
					log('header argument %s does not have a value' % header[pos:].strip())
				return main, ret
			p = header.index('=', pos)
			key = header[pos:p].strip().lower()
			pos = p + 1
			value = ''
			quoted = False
			while True:
				first = (len(header), None)
				if not quoted and ';' in header[pos:]:
					s = header.index(';', pos)
					if s < first[0]:
						first = (s, ';')
				if '"' in header[pos:]:
					q = header.index('"', pos)
					if q < first[0]:
						first = (q, '"')
				if '\\' in header[pos:]:
					b = header.index('\\', pos)
					if b < first[0]:
						first = (b, '\\')
				value += header[pos:first[0]]
				pos = first[0] + 1
				if first[1] == ';' or first[1] is None:
					break
				if first[1] == '\\':
					value += header[pos]
					pos += 1
					continue
				quoted = not quoted
			ret[key] = value
		return main, ret
	# }}}
	def _post(self, data):	# {{{
		#log('post body %s data %s' % (repr(self.body), repr(data)))
		self.body += data
		if self.post_state is None:
			# Waiting for first boundary.
			if self.boundary not in b'\r\n' + self.body:
				if self.endboundary in b'\r\n' + self.body:
					self._finish_post()
				return
			self.body = b'\r\n' + self.body
			self.body = self.body[self.body.index(self.boundary) + len(self.boundary):]
			self.post_state = 0
			# Fall through.
		a = 20
		while True:
			if self.post_state == 0:
				# Reading part headers.
				if b'\r\n\r\n' not in self.body:
					return
				headers, self.body = self._parse_headers(self.body)
				self.post_state = 1
				if 'content-type' not in headers:
					post_type = ('text/plain', {'charset': 'us-ascii'})
				else:
					post_type = self._parse_args(headers['content-type'])
				if 'content-transfer-encoding' not in headers:
					self.post_encoding = '7bit'
				else:
					self.post_encoding = self._parse_args(headers['content-transfer-encoding'])[0].lower()
				# Handle decoding of the data.
				if self.post_encoding == 'base64':
					self._post_decoder = self._base64_decoder
				elif self.post_encoding == 'quoted-printable':
					self._post_decoder = self._quopri_decoder
				else:
					self._post_decoder = lambda x, final: (x, b'')
				if 'content-disposition' in headers:
					args = self._parse_args(headers['content-disposition'])[1]
					if 'name' in args:
						self.post_name = args['name']
					else:
						self.post_name = None
					if 'filename' in args:
						fd, self.post_file = tempfile.mkstemp()
						self.post_handle = os.fdopen(fd, 'wb')
						if self.post_name not in self.post[1]:
							self.post[1][self.post_name] = []
						self.post[1][self.post_name].append((self.post_file, args['filename'], headers, post_type))
					else:
						self.post_handle = None
				else:
					self.post_name = None
				if self.post_handle is None:
					self.post[0][self.post_name] = [b'', headers, post_type]
				# Fall through.
			if self.post_state == 1:
				# Reading part body.
				if self.endboundary in self.body:
					p = self.body.index(self.endboundary)
				else:
					p = None
				if self.boundary in self.body and (p is None or self.body.index(self.boundary) < p):
					self.post_state = 0
					rest = self.body[self.body.index(self.boundary) + len(self.boundary):]
					self.body = self.body[:self.body.index(self.boundary)]
				elif p is not None:
					self.body = self.body[:p]
					self.post_state = None
				else:
					if len(self.body) <= len(self.boundary):
						break
					rest = self.body[-len(self.boundary):]
					self.body = self.body[:-len(rest)]
				decoded, self.body = self._post_decoder(self.body, self.post_state != 1)
				if self.post_handle is not None:
					self.post_handle.write(decoded)
					if self.post_state != 1:
						self.post_handle.close()
				else:
					self.post[0][self.post_name][0] += decoded
					if self.post_state != 1:
						if self.post[0][self.post_name][2][0] == 'text/plain':
							self.post[0][self.post_name][0] = self.post[0][self.post_name][0].decode(self.post[0][self.post_name][2][1].get('charset', 'utf-8'), 'replace')
				if self.post_state is None:
					self._finish_post()
					return
				self.body += rest
	# }}}
	def _finish_post(self):	# {{{
		if not self.server.post(self):
			self.socket.close()
		for f in self.post[1]:
			for g in self.post[1][f]:
				os.remove(g[0])
		del self.post
	# }}}
	def _base64_decoder(self, data, final):	// {{{
		ret = b''
		pos = 0
		table = b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/='
		current = []
		while len(data) >= pos + 4 - len(current):
			c = data[pos]
			pos += 1
			if c not in table:
				if c not in b'\r\n':
					log('ignoring invalid character %s in base64 string' % c)
				continue
			current.append(table.index(c))
			if len(current) == 4:
				# decode
				ret += bytes((current[0] << 2 | current[1] >> 4,))
				if current[2] != 65:
					ret += bytes((((current[1] << 4) & 0xf0) | current[2] >> 2,))
				if current[3] != 65:
					ret += bytes((((current[2] << 6) & 0xc0) | current[3],))
		return (ret, data[pos:])
	# }}}
	def _quopri_decoder(self, data, final):	# {{{
		ret = b''
		pos = 0
		while b'=' in data[pos:-2]:
			p = data.index(b'=', pos)
			ret += data[:p]
			if data[p + 1:p + 3] == b'\r\n':
				ret += b'\n'
				pos = p + 3
				continue
			if any(x not in b'0123456789ABCDEFabcdef' for x in data[p + 1:p + 3]):
				log('invalid escaped sequence in quoted printable: %s' % data[p:p + 3].encode('utf-8', 'replace'))
				pos = p + 1
				continue
			ret += bytes((int(data[p + 1:p + 3], 16),))
			pos = p + 3
		if final:
			ret += data[pos:]
			pos = len(data)
		elif len(pos) >= 2:
			ret += data[pos:-2]
			pos = len(data) - 2
		return (ret, data[pos:])
	# }}}
		  */
}; // }}}

// Websocket internals. {{{
template <class UserType>
void Websocket <UserType>::disconnect() { // {{{
	STARTFUNC;
	if (is_closed)
		return;
	is_closed = true;
	Loop::get()->remove_timeout(keepalive_handle);
} // }}}

template <class UserType>
Websocket <UserType>::Websocket(std::string const &address, ConnectSettings const &connect_settings, UserType *user, Receiver receiver, RunSettings const &run_settings) : // {{{
	socket(address, this),
	buffer(),
	fragments(),
	keepalive_handle(),
	is_closed(true),
	pong_seen(true),
	current_opcode(uint8_t(-1)),
	receiver(receiver),
	send_mask(true),
	user(user),
	connect_settings(connect_settings),
	run_settings(run_settings),
	received_headers()
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
	std::string hdrdata;
	std::string::size_type pos;
	while (true) {
		pos = hdrdata.find("\n");
		if (pos != std::string::npos)
			break;
		std::string r = socket.recv();	// FIXME: this should not be a blocking call.
		if (r.empty())
			throw "EOF while reading reply";
		hdrdata += r;
	}
	std::istringstream firstline(hdrdata.substr(0, pos));
	std::string namecode;
	int numcode;
	firstline >> namecode >> numcode;
	if (numcode != 101) {
		log("Unexpected reply: " + hdrdata);
		throw "wrong reply code";
	}
	hdrdata = hdrdata.substr(pos + 1);
	while (true) {
		pos = hdrdata.find("\n");
		if (pos == std::string::npos) {
			std::string r = socket.recv();	// FIXME: this should not be a blocking call.
			if (r.empty())
				throw "EOF while reading reply";
			hdrdata += r;
			continue;
		}
		std::string line = hdrdata.substr(0, pos);
		hdrdata = hdrdata.substr(pos + 1);
		if (strip(line).empty())
			break;
		if (DEBUG > 2)
			log("Header: " + line);
		std::string::size_type sep = line.find(":");
		if (sep == std::string::npos) {
			log("invalid header line");
			throw "invalid header line";
		}
		std::string key = line.substr(0, sep);
		std::string value = line.substr(sep + 1);
		received_headers[strip(key)] = strip(value);
	}
	is_closed = false;
	socket.read(&Websocket <UserType>::inject);
	//disconnect_cb(this); // TODO: allow disconnect callbacks.
	// Set up keepalive heartbeat.
	if (run_settings.keepalive != Loop::Duration()) {
		Loop *loop = Loop::get(run_settings.loop);
		keepalive_handle = loop->add_timeout(Loop::TimeoutRecord(loop->now() + run_settings.keepalive, run_settings.keepalive, this, &Websocket <UserType>::keepalive));
	}
	if (!hdrdata.empty())
		inject(hdrdata);
	if (DEBUG > 2)
		log("opened websocket");
} // }}}

template <class UserType>
Websocket <UserType>::Websocket(int socket_fd, UserType *user, Receiver receiver, RunSettings const &run_settings) : // {{{
	socket(socket_fd, this),
	buffer(),
	fragments(),
	keepalive_handle(),
	is_closed(false),
	pong_seen(true),
	current_opcode(uint8_t(-1)),
	receiver(receiver),
	send_mask(false),
	user(user),
	connect_settings(),
	run_settings(run_settings),
	received_headers()
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
	socket.read(&Websocket <UserType>::inject);
	//disconnect_cb(this); // TODO: allow disconnect cb
	// Set up keepalive heartbeat.
	if (run_settings.keepalive != Loop::Duration()) {
		Loop *loop = Loop::get(run_settings.loop);
		keepalive_handle = loop->add_timeout({loop->now() + run_settings.keepalive, run_settings.keepalive, keepalive, this});
	}
	if (DEBUG > 2)
		log("accepted websocket");
	socket.user_data = this;
	socket.read(&Websocket <UserType>::inject);
} // }}}

template <class UserType> template <class ServerType>
Websocket <UserType>::Websocket(Socket <ServerType> &&src, UserType *user, Receiver receiver, RunSettings const &run_settings) : // {{{
	socket(std::move(src), this),
	buffer(),
	fragments(),
	keepalive_handle(),
	is_closed(false),
	pong_seen(true),
	current_opcode(uint8_t(-1)),
	receiver(receiver),
	send_mask(false),
	user(user),
	connect_settings(),
	run_settings(run_settings),
	received_headers()
{
	STARTFUNC;
	socket.read(&Websocket <UserType>::inject);
	//disconnect_cb(this); // TODO: allow disconnect cb
	// Set up keepalive heartbeat.
	if (run_settings.keepalive != Loop::Duration()) {
		Loop *loop = Loop::get(run_settings.loop);
		keepalive_handle = loop->add_timeout(Loop::TimeoutRecord {loop->now() + run_settings.keepalive, run_settings.keepalive, reinterpret_cast <Loop::CbBase *>(this), reinterpret_cast <Loop::Cb>(&Websocket <UserType>::keepalive)});
	}
	if (DEBUG > 2)
		log("accepted websocket");
	socket.read(&Websocket <UserType>::inject);
} // }}}

template <class UserType>
bool Websocket <UserType>::keepalive() { // {{{
	STARTFUNC;
	if (!ping())
		log("Warning: no keepalive reply received");
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

	//log("received: " + data);
	if (DEBUG > 2)
		log((std::ostringstream() << "received " << data.length() << " bytes: " << WebString(data).dump()).str());
	if (DEBUG > 3) {
		//log(std::format("waiting: ' + ' '.join(['%02x' % x for x in self.websocket_buffer]) + ''.join([chr(x) if 32 <= x < 127 else '.' for x in self.websocket_buffer]))
		//log('data: ' + ' '.join(['%02x' % x for x in data]) + ''.join([chr(x) if 32 <= x < 127 else '.' for x in data]))
	}
	buffer += data;
	while (!buffer.empty()) {
		if (buffer[0] & 0x70) {
			// Protocol error.
			log("extension stuff is not supported!");
			is_closed = true;
			socket.close();
			return;
		}
		// Check that entire packet is received. {{{
		if (buffer.size() < 2) {
			// Not enough data for length bytes.
			if (DEBUG > 2)
				log("no length yet");
			return;
		}
		char b = buffer[1];
		bool have_mask = bool(b & 0x80);
		b &= 0x7f;
		if ((have_mask && send_mask) || (!have_mask && !send_mask)) {
			// Protocol error.
			log("mask error have mask:" + std::to_string(have_mask) + "; send mask:" + std::to_string(send_mask));
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
					log("no 10 length yet");
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
					log("no 4 length yet");
				return;
			}
			for (int i = 0; i < 2; ++i)
				len |= (buffer[2 + i] & 0xff) << (8 * (1 - i));
			pos = 4;
		}
		else {
			len = b;
			pos = 2;
			std::cerr << "packet length is " << len << std::endl;
		}
		if (buffer.length() < pos + (have_mask ? 4 : 0) + len) {
			// Not enough data for packet.
			if (DEBUG > 2)
				log((std::ostringstream() << "no packet yet; length = " << buffer.length() << "; need " << pos << " + " << (have_mask ? 4 : 0) << " + " << len).str());
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
				packet += std::string(buffer.data()[p] ^ m[i], 1);
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
				log("invalid fragment");
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
				log("fragment recorded");
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
			close();
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
			log("invalid opcode");
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
		log("websend: " + data);
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
		log(std::string("closing socket due to problem while sending: ") + msg);
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

template <class UserType>
void Websocket <UserType>::close() {	// Close a WebSocket.  (Use self.socket.close for other connections.)  {{{
	STARTFUNC;
	/* Send close request, and close the connection.
	@return None.
	*/
	send(std::string(), 8);
	is_closed = true;
	socket.close();
} // }}}
// }}}

// RPC internals. {{{
template <class UserType>
int RPC <UserType>::get_index() { // {{{
	STARTFUNC;
	++reply_index;
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
	while (!delayed_calls.empty()) {
		auto calls = std::move(delayed_calls);
		delayed_calls.clear();
		for (auto this_call: calls) {
			if (published.contains(this_call.target))
				called(this_call.code, this_call.target, this_call.args, this_call.kwargs);
			else
				log("error: invalid delayed call frame");
		}
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
		log("frame: " + frame);
	auto data = WebObject::load(frame);
	if (DEBUG > 1)
		log("packet received: " + data->print());
	if (data->get_type() != WebObject::VECTOR) {
		// Don't send errors back for unknown things, to lower risk of loops.
		log("error: frame is not a WebVector");
		return;
	}
	auto vdata = data->as_vector();
	auto length = vdata->size();
	if (length < 1 || (*vdata)[0]->get_type() != WebObject::STRING) {
		// Don't send errors back for unknown things, to lower risk of loops.
		log("error: frame does not start with a packet type string");
		return;
	}
	std::string const &ptype = *(*vdata)[0]->as_string();

	if (ptype == "error") { // string:"error", int:id, string:message {{{
		if (DEBUG > 0)
			log("error frame received");
		// Returning error on error is a looping risk, so don't do it.
		if (length != 3) {
			log("not exactly 2 argument received with error");
			return;
		}
		auto idobj = (*vdata)[1];
		auto payload = (*vdata)[2];
		if (idobj->get_type() != WebObject::INT) {
			log("error id is not int");
			return;
		}
		if (payload->get_type() != WebObject::STRING) {
			log("error payload is not a string");
			return;
		}
		int id = *idobj->as_int();

		// Remove request from queue.
		auto p = expecting_reply_bg.find(id);
		if (p != expecting_reply_bg.end())
			expecting_reply_bg.erase(p);
		else {
			auto q = expecting_reply_fg.find(id);
			if (q != expecting_reply_fg.end())
				expecting_reply_fg.erase(q);
			else
				log("warning: error reply for unknown id");
		}

		// Call error handler or throw exception.
		std::string const &msg = *payload->as_string();
		if (error)
			(user->*error)(msg);
		else
			throw msg;
		return;
	} // }}}

	if (ptype == "return") { // string:"return", int:id, WebObject:value {{{
		if (length != 2) {
			log("not exactly 1 argument received with return");
			return;
		}
		auto argobj = (*vdata)[1];
		if (argobj->get_type() != WebObject::VECTOR) {
			log("return argument is not vector");
			return;
		}
		auto arg = argobj->as_vector();
		if (arg->size() != 2) {
			log("return argument is not length 2");
			return;
		}
		auto idobj = (*arg)[0];
		auto payload = (*arg)[1];
		if (idobj->get_type() != WebObject::INT) {
			log("return id is not int");
			return;
		}
		int id = *idobj->as_int();

		auto p = expecting_reply_bg.find(id);
		if (p == expecting_reply_bg.end()) {
			auto q = expecting_reply_fg.find(id);
			if (q == expecting_reply_fg.end()) {
				log("invalid return id received");
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
		(user->*target.reply)(payload);
		return;
	} // }}}

	if (ptype == "call") { // string:"call", int:id, string:target, WebVector:args, WebMap:kwargs {{{
		if (length != 5) {
			log("not exactly 4 argument received with call");
			return;
		}
		auto idobj = (*vdata)[1];
		auto targetobj = (*vdata)[2];
		auto argsobj = (*vdata)[3];
		auto kwargsobj = (*vdata)[4];
		if (idobj->get_type() != WebObject::INT) {
			log("call id is not int");
			return;
		}
		if (targetobj->get_type() != WebObject::STRING) {
			log("call target is not string");
			return;
		}
		if (argsobj->get_type() != WebObject::VECTOR) {
			log("call args is not vector");
			return;
		}
		if (kwargsobj->get_type() != WebObject::MAP) {
			log("call kwargs is not map");
			return;
		}
		int id = *idobj->as_int();
		std::string target = *targetobj->as_string();
		auto args = std::dynamic_pointer_cast <WebVector> (argsobj);
		auto kwargs = std::dynamic_pointer_cast <WebMap> (kwargsobj);
		if (activated) {
			try {
				called(id, target, args, kwargs);
			}
			catch (...) {
				log("error: remote call failed");
				send("error", WebVector::create(WebInt::create(id), WebString::create("remote call failed")));
			}
		}
		else
			delayed_calls.push_back(Call(id, target, args, kwargs, user));
		return;
	} // }}}

	log("error: invalid RPC command");
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
		log((std::ostringstream() << "sending: " << " " << object->print()).str());
	auto obj = WebVector::create(WebString::create(code), object);
	websocket.send(obj->dump());
} // }}}

template <class UserType>
void RPC <UserType>::CalledData::called_return(std::shared_ptr <WebObject> ret) { // {{{
	STARTFUNC;
	rpc->called_data.erase(iterator);
	rpc->send("return", WebVector::create(WebInt::create(id), ret));
} // }}}

template <class UserType>
void RPC <UserType>::called(int id, std::string const &target, std::shared_ptr <WebVector> args, std::shared_ptr <WebMap> kwargs) { // {{{
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
	auto co = published.find(target);
	if (co != published.end()) {
		auto c = (user->*co->second)(args, kwargs);
		if (id != 0) {
			called_data.emplace_back(called_data.end(), this, id);
			called_data.back().iterator = --called_data.end();
			c.set_cb(&called_data.back(), &CalledData::called_return);
		}
		c();	// Start coroutine.
		return;
	}

	// Fail.
	throw "trying to call unregistered target";
} // }}}

template <class UserType>
RPC <UserType>::RPC(std::string const &address, std::map <std::string, Published> const &published, ErrorCb error, UserType *user, Websocket <RPC <UserType> >::ConnectSettings const &connect_settings, Websocket <RPC <UserType> >::RunSettings const &run_settings) : // {{{
		websocket(address, connect_settings, this, &RPC <UserType>::recv, run_settings),
		error(error),
		activation_handle(),
		activated(false),
		user(user),
		reply_index(0),
		expecting_reply_bg(),
		expecting_reply_fg(),
		published(published),
		delayed_calls{},
		called_data()
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
	activation_handle = Loop::get(run_settings.loop)->add_idle(Loop::IdleRecord(this, &RPC <UserType>::activate));
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
		expecting_reply_bg[index] = {this, reply};
	}
	else
		index = 0;
	send("call", WebVector::create(WebInt::create(index), WebString::create(target), args, kwargs));
} // }}}

template <class UserType>
coroutine RPC <UserType>::fgcall(std::string const &target, std::shared_ptr <WebVector> args, std::shared_ptr <WebMap> kwargs) { // {{{
	STARTFUNC;
	if (!args)
		args = WebVector::create();
	if (!kwargs)
		kwargs = WebMap::create();
	int index = get_index();
	expecting_reply_fg[index] = GetHandle();
	send("call", WebVector::create(WebInt::create(index), WebString::create(target), args, kwargs));
	auto ret = Yield(WebNone::create());
	if (DEBUG > 4)
		log("fgcall returns " + (ret ? ret->print() : "nothing"));
	co_return ret;
} // }}}
// }}}

template <class UserType>
void Httpd <UserType>::new_connection(Socket <UserType> &&remote) { // {{{
	STARTFUNC;
	connections.emplace_back(std::move(remote), this);
} // }}}

template <class UserType>
std::map <int, char const *> Httpd <UserType>::Connection::http_response = {
#include "http.hh"
};


/*
class RPChttpd(Httpd): # {{{
	'''Http server which serves websockets that implement RPC.
	'''
	class _Broadcast: # {{{
		def __init__(self, server, group = None):
			self.server = server
			self.group = group
		def __getitem__(self, item):
			return RPChttpd._Broadcast(self.server, item)
		def __getattr__(self, key):
			if key.startswith('_'):
				raise AttributeError('invalid member name')
			def impl(*a, **ka):
				for c in self.server.websockets.copy():
					if self.group is None or self.group in c.groups:
						getattr(c, key).event(*a, **ka)
			return impl
	# }}}
	def __init__(self, port, target, *a, **ka): # {{{
		'''Start a new RPC HTTP server.
		Extra arguments are passed to the Httpd constructor,
		which passes its extra arguments to network.Server.
		@param port: Port to listen on.  Same format as in
			python-network.
		@param target: Communication object class.  A new
			object is created for every connection.  Its
			constructor is called with the newly created
			RPC as an argument.
		@param log: If set, debugging is enabled and logging is
			sent to this file.  If it is a directory, a log
			file with the current date and time as filename
			will be used.
		'''
		## Function to send an event to some or all connected
		# clients.
		# To send to some clients, add an identifier to all
		# clients in a group, and use that identifier in the
		# item operator, like so:
		# @code{.py}
		# connection0.groups.clear()
		# connection1.groups.add('foo')
		# connection2.groups.add('foo')
		# server.broadcast.bar(42)	# This is sent to all clients.
		# server.broadcast['foo'].bar(42)	# This is only sent to clients in group 'foo'.
		# @endcode
		self.broadcast = RPChttpd._Broadcast(self)
		if 'log' in ka:
			name = ka.pop('log')
			if name:
				global DEBUG
				if DEBUG < 2:
					DEBUG = 2
				if os.path.isdir(name):
					n = os.path.join(name, time.strftime('%F %T%z'))
					old = n
					i = 0
					while os.path.exists(n):
						i += 1
						n = '%s.%d' % (old, i)
				else:
					n = name
				try:
					f = open(n, 'a')
					if n != name:
						sys.stderr.write('Logging to %s\n' % n)
				except IOError:
					fd, n = tempfile.mkstemp(prefix = os.path.basename(n) + '-' + time.strftime('%F %T%z') + '-', text = True)
					sys.stderr.write('Opening file %s failed, using tempfile instead: %s\n' % (name, n))
					f = os.fdopen(fd, 'a')
				stderr_fd = sys.stderr.fileno()
				os.close(stderr_fd)
				os.dup2(f.fileno(), stderr_fd)
				log('Start logging to %s, commandline = %s' % (n, repr(sys.argv)))
		Httpd.__init__(self, port, target, websocket = RPC, *a, **ka)
	# }}}
# }}}
*/

// vim: set fileencoding=utf-8 foldmethod=marker :
