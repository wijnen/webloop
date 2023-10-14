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
	Websocket(Socket <Websocket <UserType> > &&src, UserType *user = nullptr, Receiver receiver = nullptr, RunSettings const &run_settings = {});
	// TODO: move constructor and assignment.

	bool keepalive();
	void read(std::string &data);
	void send(std::string const &data, int opcode = 1); // Send a WebSocket frame.
	bool ping(std::string const &data = std::string()); // Send a ping; return False if no pong was seen for previous ping.  Other received packets also count as a pong.
	void close(); // Close a WebSocket.  (Use socket.close for other connections.)
}; // }}}

template <class UserType>
class RPC : public Websocket <UserType> { // {{{
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
	ErrorCb error;
	int reply_index;
	std::map <int, BgReplyData> expecting_reply_bg;
	std::map <int, coroutine::handle_type> expecting_reply_fg;
	Loop::IdleHandle activation_handle;
	std::list <RPC *> activation_queue;
	std::list <Call> delayed_calls;
	bool activated;
	std::map <std::string, Published> published;
	std::set <std::map <std::string, std::shared_ptr <RPC> > > groups;
	UserType *user;
	int get_index();
	bool activate_all();
	void activate();
	void recv(std::string const &frame);
	void send(std::string const &code, std::shared_ptr <WebObject> object);
	struct IdSelf { // {{{
		int id;
		RPC *self;
	}; // }}}
	void call_return(std::shared_ptr <WebObject> ret, void *idselfptr);
	void called(int id, std::string const &target, std::shared_ptr <WebVector> args, std::shared_ptr <WebMap> kwargs);
	void fgcb(std::shared_ptr <WebObject> ret);
public:
	RPC(std::string const &address, std::map <std::string, Published> const &published = {}, ErrorCb error = {}, UserType *user = nullptr, Websocket <UserType>::ConnectSettings const &connect_settings = {.method = "GET", .user = {}, .password = {}, .sent_headers = {}}, Websocket <UserType>::RunSettings const &run_settings = {.loop = nullptr, .keepalive = 50s});
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
	typedef RPC <UserType>::ErrorCb ErrorCb;
	ErrorCb error;					// Error callback.
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
	void server_error(std::string const &message) { // {{{
		log("Error received by server: " + message);
		if (error)
			(user->*error)("Error received by server: " + message);
	} // }}}
	Connection *create_connection(Socket <Connection> &&socket) { // {{{
		connections.emplace_back(std::move(socket), server);
		return &connections.back();
	} // }}}
public:
	Httpd(std::string const &service, std::vector <std::string> const &httpdirs = {}, std::vector <std::string> const &proxy = {}, ErrorCb error = nullptr, Loop *loop = nullptr, int backlog = 5) : // {{{
			httpdirs(httpdirs),
			proxy(proxy),
			error(error),
			user(nullptr),
			exts(),
			loop(Loop::get(loop)),
			server(service, this, &Httpd <UserType>::create_connection, &Httpd <UserType>::server_error, loop, backlog),
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
	std::string ignore_disconnect(std::string const &pending) { (void)&pending; return {}; }
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
			httpd->websockets.emplace_back(std::move(socket), this);
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
	Connection(Socket <Connection> &&src, Httpd *server) : // {{{
			httpd(server),
			socket(std::move(src)),
			received_headers{},
			method{},
			url{},
			http_version{},
			prefix{}
	{
		socket.disconnect_cb = &Connection::ignore_disconnect;
		socket.user = this;
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
