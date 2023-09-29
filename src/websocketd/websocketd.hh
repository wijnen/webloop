/* {{{ Copyright 2013-2023 Bas Wijnen <wijnen@debian.org>
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or(at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# }}} */

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
// }}}

// Debug level, set from DEBUG environment variable.
// * 0: No debugging (default).
// * 1: Tracebacks on errors.
// * 2: Incoming and outgoing RPC packets.
// * 3: Incomplete packet information.
// * 4: All incoming and outgoing data.
// * 5: Non-websocket data.
//DEBUG = 0 if os.getenv('NODEBUG') else int(os.getenv('DEBUG', 1))

std::string strip(std::string const &src, std::string const &chars = " \t\v\f") { // {{{
	auto first = src.find_first_not_of(chars);
	if (first == std::string::npos)
		return std::string();
	auto last = src.find_last_not_of(chars);
	return src.substr(first, last - first + 1);
} // }}}

class Websocket { // {{{
public:
	typedef void (*Receiver)(std::string const &data, void *user_data);
private:
	// Main class implementing the websocket protocol.
	Socket socket;
	std::string buffer;
	std::string fragments;
	Loop::TimeoutHandle keepalive_handle;
	bool is_closed;
	bool pong_seen;	// true if a pong was seen since last ping command.
	uint8_t current_opcode;
	Receiver receiver;
public:
	struct Settings {
		std::string method;				// default: GET
		std::string user;				// login name; if both this and password are empty: don't send them.
		std::string password;				// login password
		bool send_mask;					// whether masks are used to sent data.
		Loop *loop;					// Main loop to use; usually nullptr, so the default is used.
		Loop::Duration keepalive;			// keepalive timer.
		std::map <std::string, std::string> headers;	// extra headers.
		void *user_data;				// passed through to callback functions.
	};
	Settings settings;
	std::map <std::string, std::string> headers;
	static void disconnect(void *self_ptr) { // {{{
		Websocket *self = reinterpret_cast <Websocket *>(self_ptr);
		if (self->is_closed)
			return;
		self->is_closed = true;
		Loop::get()->remove_timeout(self->keepalive_handle);
	} // }}}
	Websocket(std::string const &address, Receiver receiver, Settings const &settings) : // {{{
		socket(address, this),
		buffer(),
		fragments(),
		keepalive_handle(),
		is_closed(true),
		pong_seen(true),
		receiver(receiver),
		settings(settings),
		headers()
	{
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
		for (auto e: settings.headers)
			extra_headers += e.first + ": " + e.second + "\r\n";
		std::string userpwd;
		if (!settings.user.empty() || !settings.password.empty())
			userpwd = settings.user + ":" + password + "\r\n";
		std::string url = socket.extra;
		if (url.empty())
			url = "/";
		else if (url[0] != '/')
			url = "/" + url;
		std::string host = socket.hostname;
		if (socket.service != socket.protocol)
			host += ":" + socket.service;
		// Sec-Websocket-Key is not random, because that has no
		// value. The example value from the RFC is used.
		// Differently put: it uses a special random generator
		// which always returns range(0x01, 0x11).
		socket.send(
			settings.method + " " + url + " HTTP/1.1\r\n"
			"Host: " + host + "\r\n"
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
			log(std::format("Unexpected reply: {}", hdrdataa));
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
			if (line.empty())
				break;
			std::string::size_type sep = line.find(":");
			if (sep == std::string::npos) {
				log("invalid header line");
				throw "invalid header line";
			}
			std::string key = line.substr(0, sep);
			std::string value = line.substr(sep + 1);
			headers[strip(key)] = strip(value);
		}
		socket.read(read, nullptr, settings.loop);
		disconnect_cb(this);
		// Set up keepalive heartbeat.
		if (settings.keepalive) {
			Loop *loop = Loop::get(settings.loop);
			keepalive_handle = loop->add_timeout(loop->now() + settings.keepalive, settings.keepalive, keepalive);
		}
		if (!hdrdata.empty())
			read(hdrdata, this)
		if (DEBUG > 2)
			log("opened websocket");
	} // }}}
	static bool keepalive(void *self_ptr) { // {{{
		Websocket *self = reinterpret_cast <Websocket *>(self_ptr);
		if (!self->ping())
			log("Warning: no keepalive reply received");
		return true;
	} // }}}
	static bool read(std::string &data, void *self_ptr) { // {{{
		Websocket *self = reinterpret_cast <Websocket *>(self_ptr);
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
			log(std::format("received {} bytes", data.length()));
		if (DEBUG > 3) {
			//log(std::format("waiting: ' + ' '.join(['%02x' % x for x in self.websocket_buffer]) + ''.join([chr(x) if 32 <= x < 127 else '.' for x in self.websocket_buffer]))
			//log('data: ' + ' '.join(['%02x' % x for x in data]) + ''.join([chr(x) if 32 <= x < 127 else '.' for x in data]))
		}
		self->buffer += data;
		while (!self->buffer.empty()) {
			if (self->buffer[0] & 0x70) {
				// Protocol error.
				log(std::format("extension stuff {:x}, not supported!", self->buffer[0]));
				self->is_closed = true;
				self->socket.close();
				return false;
			}
			// Check that entire packet is received. {{{
			if (self->buffer.size() < 2) {
				// Not enough data for length bytes.
				if (DEBUG > 2)
					log("no length yet");
				return true;
			}
			char b = self->buffer[1];
			bool have_mask = bool(b & 0x80);
			b &= 0x7f;
			if ((have_mask && self->settings.send_mask) || (!have_mask && !self->settings.send_mask)) {
				// Protocol error.
				log("mask error");
				self->is_closed = true;
				self->socket.close();
				return false;
			}
			std::string::size_type pos;
			std::string::size_type len = 0;
			if (b == 0x7f) {
				if (self->buffer.length() < 10) {
					// Not enough data for length bytes.
					if (DEBUG > 2)
						log("no 10 length yet");
					return true;
				}
				for (int i = 0; i < 8; ++i)
					len |= self->buffer[2 + i] << (8 * (7 - i));
				pos = 10;
			}
			else if (b == 0x7e) {
				if (self->buffer.length() < 4) {
					// Not enough data for length bytes.
					if (DEBUG > 2)
						log("no 4 length yet");
					return true;
				}
				for (int i = 0; i < 2; ++i)
					len |= self->buffer[2 + i] << (8 * (1 - i));
				pos = 4;
			}
			else {
				len = b;
				pos = 2;
			}
			if (self->buffer.length() < pos + (have_mask ? 4 : 0) + len) {
				// Not enough data for packet.
				if (DEBUG > 2)
					log("no packet yet");
				// Long packets should not cause ping timeouts.
				pong_seen = true;
				return true;
			}
			// }}}
			std::string header = self->buffer.substr(0, pos);
			uint8_t opcode = header[0] & 0xf;
			std::string packet;
			if (have_mask) {
				uint32_t mask = *(reinterpret_cast <uint32_t *> (&self->buffer.data()[pos]));
				pos += 4;
				auto p = pos;
				for (p = pos; p + 3 < self->buffer.size(); p += 4)
					packet += std::string(*(reinterpret_cast <uint32_t const *> (&self->buffer.data()[p])) ^ mask, 4);
				uint8_t m[4];
				*reinterpret_cast <uint32_t *>(m) = mask;
				for (int i = 0; p < self->buffer.size(); ++p, ++i)
					packet += std::string(self->buffer.data()[p] ^ m[i], 1);
			}
			else {
				packet = self->buffer.substr(pos, len);
			}
			self->buffer = self->buffer.substr(pos + len);
			if (current_opcode < 0)
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
					self->is_closed = true;
					self->socket.close();
					return false;
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
			current_opcode = -1;
			switch(opcode) {
			case 8:
				// Connection close request.
				close();
				return false;
			case 9:
				// Ping.
				send(data, 10);	// Pong
				break;
			case 10:
				// Pong.
				pong_seen = true;
				break;
			case 1:	// Text.
			case 2:	// Binary.
				receiver(data, self->settings.user_data);
				break;
			default:
				log("invalid opcode");
				self->is_closed = true;
				self->socket.close();
				return false;
			}
		}
		return true;
	} // }}}
	void send(std::string const &data, int opcode = 1) {	// Send a WebSocket frame.  {{{
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
		if (self->settings.send_mask) {
			maskchar = 0x80;
			// Masks are stupid, but the standard requires them.  Don't waste time on encoding (or decoding, if also using this module).
			maskcode = std::string("\0\0\0\0", 4);
		}
		else {
			maskchar = 0
		}
		std::string len;
		size_t l = data.length();
		if (l < 0x7e)
			len = std::string(maskchar | l, 1);
		else if (l < 1 << 16)
			len = std::string((char[3]){maskchar | 0x7e, (l >> 8) & 0xff, l & 0xff}, 3);
		else {
			char header[9];
			header[0] = maskchar | 0x7f;
			for (int i = 0; i < 8; ++i)
				header[1 + i] = (l >> (8 * (7 - i))) & 0xff;
			len = std::string(header, 9);
		}
		try {
			self.socket.send(bytes((0x80 | opcode,)) + len + mask + data)
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
	bool ping(std::string const &data = std::string()) { // Send a ping; return False if no pong was seen for previous ping.  Other received packets also count as a pong. {{{
		/* Send a ping, return if a pong was received since last ping.
		@param data: Data to send with the ping.
		@return True if a pong was received since last ping, False if not.
		*/
		bool ret = pong_seen;
		pong_seen = false;
		send(data, 9);
		return ret;
	} // }}}
	void close() {	// Close a WebSocket.  (Use self.socket.close for other connections.)  {{{
		/* Send close request, and close the connection.
		@return None.
		*/
		send(std::string(), 8);
		is_closed = true;
		socket.close();
	} // }}}
}; // }}}

// Set of inactive websockets, and idle handle for _activate_all.
_activation = [set(), None]

struct ObjectBase {};
typedef std::vector <std::shared_ptr <ObjectBase> > Args;
typedef std::map <std::string, std::shared_ptr <ObjectBase> > KwArgs;
typedef std::shared_ptr <ObjectBase> (*ReplyCb)(Args args, KwArgs kwargs);
void call(ReplyCb reply, std::string const &target, Args args, KwArgs kwargs) { // {{{
	/* Make a call to a function or generator.
	If target is a generator, the call will return when it finishes.
	Yielded values are ignored.  Extra arguments are passed to target.
	@param reply: Function to call with return value when it is ready, or
		None.
	@param target: Function or generator to call.
	@param a: Arguments that are passed to target.
	@param ka: Keyword arguments that are passed to target.
	@return None.
	*/
	ret = target(*a, **ka)
	if type(ret) is not RPC._generatortype:
		if reply is not None:
			reply(ret)
		return
	# Target is a generator.
	def wake(arg = None):
		try:
			return ret.send(arg)
		except StopIteration as result:
			if reply is not None:
				reply(result.value)
	# Start the generator.
	wake()
	# Send it its wakeup function.
	wake(wake)
} // }}}

class RPC(Websocket): # {{{
	'''Remote Procedure Call over Websocket.
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
	'''
	_generatortype = type((lambda: (yield))())
	_index = 0
	_calls = {}
	@classmethod
	def _get_index(cls): # {{{
		while cls._index in cls._calls:
			cls._index += 1
		# Put a limit on the _index values.
		if cls._index >= 1 << 31:
			cls._index = 0
			while cls._index in cls._calls:
				cls._index += 1
		return cls._index
	# }}}
	def __init__(self, port, recv = None, error = None, *a, **ka): # {{{
		'''Create a new RPC object.  Extra parameters are passed to the
		Websocket constructor, which passes its extra parameters to the
		network.Socket constructor.
		@param port: Host and port to connect to, same format as
			python-network uses.
		@param recv: Function (or class) that receives this object as
			an argument and returns a communication object.
		'''
		_activation[0].add(self)
		if _activation[1] is None:
			_activation[1] = add_idle(_activate_all)
		self._delayed_calls = []
		## Groups are used to do selective broadcast() events.
		self.groups = set()
		Websocket.__init__(self, port, recv = RPC._recv, *a, **ka)
		self._error = error
		self._target = recv(self) if recv is not None else None
	# }}}
	def __call__(self): # {{{
		'''Internal use only.  Do not call.  Activate the websocket; send initial frames.
		@return None.
		'''
		if self._delayed_calls is None:
			return
		calls = self._delayed_calls
		self._delayed_calls = None
		for call in calls:
			if not hasattr(self._target, call[1]) or not callable(getattr(self._target, call[1])):
				self._send('error', 'invalid delayed call frame %s' % repr(call))
			else:
				self._call(call[0], call[1], call[2], call[3])
	# }}}
	class _wrapper: # {{{
		def __init__(self, base, attr): # {{{
			self.base = base
			self.attr = attr
		# }}}
		def __call__(self, *a, **ka): # {{{
			my_id = RPC._get_index()
			self.base._send('call', (my_id, self.attr, a, ka))
			my_call = [None]
			RPC._calls[my_id] = lambda x: my_call.__setitem__(0, (x,))	# Make it a tuple so it cannot be None.
			while my_call[0] is None:
				data = self.base._websocket_read(self.base.socket.recv(), True)
				while data is not None:
					self.base._recv(data)
					data = self.base._websocket_read(b'')
			del RPC._calls[my_id]
			return my_call[0][0]
		# }}}
		def __getitem__(self, *a, **ka): # {{{
			self.base._send('call', (None, self.attr, a, ka))
		# }}}
		def bg(self, reply, *a, **ka): # {{{
			my_id = RPC._get_index()
			self.base._send('call', (my_id, self.attr, a, ka))
			RPC._calls[my_id] = lambda x: self.do_reply(reply, my_id, x)
		# }}}
		def wait(self, wake, *a, **ka): # {{{
			self.bg(wake, *a, **ka)
			return (yield)
		# }}}
		def do_reply(self, reply, my_id, ret): # {{{
			del RPC._calls[my_id]
			reply(ret)
		# }}}
		# alternate names. {{{
		def call(self, *a, **ka):
			self.__call__(*a, **ka)
		def event(self, *a, **ka):
			self.__getitem__(*a, **ka)
		# }}}
	# }}}
	def _send(self, type, object): # {{{
		'''Send an RPC packet.
		@param type: The packet type.
			One of "return", "error", "call".
		@param object: The data to send.
			Return value, error message, or function arguments.
		'''
		if DEBUG > 1:
			log('sending:' + repr(type) + repr(object))
		Websocket._websocket_send(self, json.dumps((type, object)))
	# }}}
	def _parse_frame(self, frame): # {{{
		'''Decode an RPC packet.
		@param frame: The packet.
		@return (type, object) or (None, error_message).
		'''
		try:
			# Don't choke on Chrome's junk at the end of packets.
			data = json.JSONDecoder().raw_decode(frame)[0]
		except ValueError:
			log('non-json frame: %s' % repr(frame))
			return (None, 'non-json frame')
		if type(data) is not list or len(data) != 2 or not isinstance(data[0], str):
			log('invalid frame %s' % repr(data))
			return (None, 'invalid frame')
		if data[0] == 'call':
			if not isinstance(data[1], list):
				log('invalid call frame (no list) %s' % repr(data))
				return (None, 'invalid frame')
			if len(data[1]) != 4:
				log('invalid call frame (list length is not 4) %s' % repr(data))
				return (None, 'invalid frame')
			if (data[1][0] is not None and not isinstance(data[1][0], int)):
				log('invalid call frame (invalid id) %s' % repr(data))
				return (None, 'invalid frame')
			if not isinstance(data[1][1], str):
				log('invalid call frame (no string target) %s' % repr(data))
				return (None, 'invalid frame')
			if not isinstance(data[1][2], list):
				log('invalid call frame (no list args) %s' % repr(data))
				return (None, 'invalid frame')
			if not isinstance(data[1][3], dict):
				log('invalid call frame (no dict kwargs) %s' % repr(data))
				return (None, 'invalid frame')
			if self._delayed_calls is None and (not hasattr(self._target, data[1][1]) or not callable(getattr(self._target, data[1][1]))):
				log('invalid call frame (no callable) %s' % repr(data))
				return (None, 'invalid frame')
		elif data[0] not in ('error', 'return'):
			log('invalid frame type %s' % repr(data))
			return (None, 'invalid frame')
		return data
	# }}}
	def _recv(self, frame): # {{{
		'''Receive a websocket packet.
		@param frame: The packet.
		@return None.
		'''
		data = self._parse_frame(frame)
		if DEBUG > 1:
			log('packet received: %s' % repr(data))
		if data[0] is None:
			self._send('error', data[1])
			return
		elif data[0] == 'error':
			if DEBUG > 0:
				traceback.print_stack()
			if self._error is not None:
				self._error(data[1])
			else:
				raise ValueError(data[1])
		elif data[0] == 'event':
			# Do nothing with this; the packet is already logged if DEBUG > 1.
			return
		elif data[0] == 'return':
			assert data[1][0] in RPC._calls
			RPC._calls[data[1][0]] (data[1][1])
			return
		elif data[0] == 'call':
			try:
				if self._delayed_calls is not None:
					self._delayed_calls.append(data[1])
				else:
					self._call(data[1][0], data[1][1], data[1][2], data[1][3])
			except:
				traceback.print_exc()
				log('error: %s' % str(sys.exc_info()[1]))
				self._send('error', traceback.format_exc())
		else:
			self._send('error', 'invalid RPC command')
	# }}}
	def _call(self, reply, member, a, ka): # {{{
		'''Make local function call at remote request.
		The local function may be a generator, in which case the call
		will return when it finishes.  Yielded values are ignored.
		@param reply: Return code, or None for event.
		@param member: Requested function name.
		@param a: Arguments.
		@param ka: Keyword arguments.
		@return None.
		'''
		call((lambda ret: self._send('return', (reply, ret))) if reply is not None else None, getattr(self._target, member), *a, **ka)
	# }}}
	def __getattr__(self, attr): # {{{
		'''Select member to call on remote communication object.
		@param attr: Member name.
		@return Function which calls the remote object when invoked.
		'''
		if attr.startswith('_'):
			raise AttributeError('invalid RPC function name %s' % attr)
		return RPC._wrapper(self, attr)
	# }}}
# }}}

def _activate_all(): # {{{
	'''Internal function to activate all inactive RPC websockets.
	@return False, so this can be registered as an idle task.
	'''
	if _activation[0] is not None:
		for s in _activation[0]:
			s()
	_activation[0].clear()
	_activation[1] = None
	return False
# }}}

class _Httpd_connection:	# {{{
	'''Connection object for an HTTP server.
	This object implements the internals of an HTTP server.  It
	supports GET and POST, and of course websockets.  Don't
	construct these objects directly.
	'''
	def __init__(self, server, socket, websocket = Websocket, proxy = (), error = None): # {{{
		'''Constructor for internal use.  This should not be
			called directly.
		@param server: Server object for which this connection
			is handled.
		@param socket: Newly accepted socket.
		@param httpdirs: Locations of static web pages to
			serve.
		@param websocket: Websocket class from which to create
			objects when a websocket is requested.
		@param proxy: Tuple of virtual proxy prefixes that
			should be ignored if requested.
		'''
		self.server = server
		self.socket = socket
		self.websocket = websocket
		self.proxy = (proxy,) if isinstance(proxy, str) else proxy
		self.error = error
		self.headers = {}
		self.address = None
		self.socket.disconnect_cb(lambda socket, data: b'')	# Ignore disconnect until it is a WebSocket.
		self.socket.readlines(self._line)
		#log('Debug: new connection from %s\n' % repr(self.socket.remote))
	# }}}
	def _line(self, l):	# {{{
		if DEBUG > 4:
			log('Debug: Received line: %s' % l)
		if self.address is not None:
			if not l.strip():
				self._handle_headers()
				return
			try:
				key, value = l.split(':', 1)
			except ValueError:
				log('Invalid header line: %s' % l)
				return
			self.headers[key.lower()] = value.strip()
			return
		else:
			try:
				self.method, url, self.standard = l.split()
				for prefix in self.proxy:
					if url.startswith('/' + prefix + '/') or url == '/' + prefix:
						self.prefix = '/' + prefix
						break
				else:
					self.prefix = ''
				address = urlparse(url)
				path = address.path[len(self.prefix):] or '/'
				self.url = path + url[len(address.path):]
				self.address = urlparse(self.url)
				self.query = parse_qs(self.address.query)
			except:
				traceback.print_exc()
				self.server.reply(self, 400, close = True)
			return
	# }}}
	def _handle_headers(self):	# {{{
		if DEBUG > 4:
			log('Debug: handling headers')
		is_websocket = 'connection' in self.headers and 'upgrade' in self.headers and 'upgrade' in self.headers['connection'].lower() and 'websocket' in self.headers['upgrade'].lower()
		self.data = {}
		self.data['url'] = self.url
		self.data['address'] = self.address
		self.data['query'] = self.query
		self.data['headers'] = self.headers
		msg = self.server.auth_message(self, is_websocket) if callable(self.server.auth_message) else self.server.auth_message
		if msg:
			if 'authorization' not in self.headers:
				self.server.reply(self, 401, headers = {'WWW-Authenticate': 'Basic realm="%s"' % msg.replace('\n', ' ').replace('\r', ' ').replace('"', "'")}, close = True)
				return
			else:
				auth = self.headers['authorization'].split(None, 1)
				if auth[0].lower() != 'basic':
					self.server.reply(self, 400, close = True)
					return
				pwdata = base64.b64decode(auth[1].encode('utf-8')).decode('utf-8', 'replace').split(':', 1)
				if len(pwdata) != 2:
					self.server.reply(self, 400, close = True)
					return
				self.data['user'] = pwdata[0]
				self.data['password'] = pwdata[1]
				if not self.server.authenticate(self):
					self.server.reply(self, 401, headers = {'WWW-Authenticate': 'Basic realm="%s"' % msg.replace('\n', ' ').replace('\r', ' ').replace('"', "'")}, close = True)
					return
		if not is_websocket:
			if DEBUG > 4:
				log('Debug: not a websocket')
			self.body = self.socket.unread()
			if self.method.upper() == 'POST':
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
			else:
				try:
					if not self.server.page(self):
						self.socket.close()
				except:
					if DEBUG > 0:
						traceback.print_exc()
					log('exception: %s\n' % repr(sys.exc_info()[1]))
					try:
						self.server.reply(self, 500, close = True)
					except:
						self.socket.close()
			return
		# Websocket.
		if self.method.upper() != 'GET' or 'sec-websocket-key' not in self.headers:
			if DEBUG > 2:
				log('Debug: invalid websocket')
			self.server.reply(self, 400, close = True)
			return
		newkey = base64.b64encode(hashlib.sha1(self.headers['sec-websocket-key'].strip().encode('utf-8') + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest()).decode('utf-8')
		headers = {'Sec-WebSocket-Accept': newkey, 'Connection': 'Upgrade', 'Upgrade': 'WebSocket'}
		self.server.reply(self, 101, None, None, headers, close = False)
		self.websocket(None, recv = self.server.recv, socket = self.socket, error = self.error, mask = (None, False), websockets = self.server.websockets, data = self.data, real_remote = self.headers.get('x-forwarded-for'))
	# }}}
	def _parse_headers(self, message): # {{{
		lines = []
		pos = 0
		while True:
			p = message.index(b'\r\n', pos)
			ln = message[pos:p].decode('utf-8', 'replace')
			pos = p + 2
			if ln == '':
				break
			if ln[0] in ' \t':
				if len(lines) == 0:
					log('header starts with continuation')
				else:
					lines[-1] += ln
			else:
				lines.append(ln)
		ret = {}
		for ln in lines:
			if ':' not in ln:
				log('ignoring header line without ":": %s' % ln)
				continue
			key, value = [x.strip() for x in ln.split(':', 1)]
			if key.lower() in ret:
				log('duplicate key in header: %s' % key)
			ret[key.lower()] = value
		return ret, message[pos:]
	# }}}
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
	def _base64_decoder(self, data, final):	# {{{
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
# }}}
class Httpd: # {{{
	'''HTTP server.
	This object implements an HTTP server.  It supports GET and
	POST, and of course websockets.
	'''
	def __init__(self, port, recv = None, httpdirs = None, server = None, proxy = (), http_connection = _Httpd_connection, websocket = Websocket, error = None, *a, **ka): # {{{
		'''Create a webserver.
		Additional arguments are passed to the network.Server.
		@param port: Port to listen on.  Same format as in
			python-network.
		@param recv: Communication object class for new
			websockets.
		@param httpdirs: Locations of static web pages to
			serve.
		@param server: Server to use, or None to start a new
			server.
		@param proxy: Tuple of virtual proxy prefixes that
			should be ignored if requested.
		'''
		## Communication object for new websockets.
		self.recv = recv
		self._http_connection = http_connection
		## Sequence of directories that that are searched to serve.
		# This can be used to make a list of files that are available to the web server.
		self.httpdirs = httpdirs
		self._proxy = proxy
		self._websocket = websocket
		self._error = error if error is not None else lambda msg: print(msg)
		## Extensions which are handled from httpdirs.
		# More items can be added by the user program.
		self.exts = {}
		# Automatically add all extensions for which a mime type exists.
		try:
			exts = {}
			with open('/etc/mime.types') as f:
				for ln in f:
					if ln.strip() == '' or ln.startswith('#'):
						continue
					items = ln.split()
					for ext in items[1:]:
						if ext not in exts:
							exts[ext] = items[0]
						else:
							# Multiple registration: don't choose one.
							exts[ext] = False
		except FileNotFoundError:
			# This is probably a Windows system; use some defaults.
			exts = { 'html': 'text/html', 'css': 'text/css', 'js': 'text/javascript', 'jpg': 'image/jpeg', 'jpeg': 'image/jpeg', 'png': 'image/png', 'bmp': 'image/bmp', 'gif': 'image/gif', 'pdf': 'application/pdf', 'svg': 'image/svg+xml', 'txt': 'text/plain'}
		for ext in exts:
			if exts[ext] is not False:
				if exts[ext].startswith('text/') or exts[ext] == 'application/javascript':
					self.handle_ext(ext, exts[ext] + ';charset=utf-8')
				else:
					self.handle_ext(ext, exts[ext])
		## Currently connected websocket connections.
		self.websockets = set()
		if server is None:
			## network.Server object.
			self.server = network.Server(port, self, *a, **ka)
		else:
			self.server = server
	# }}}
	def __call__(self, socket): # {{{
		'''Add socket to list of accepted connections.
		Primarily useful for adding standard input and standard
		output as a fake socket.
		@param socket: Socket to add.
		@return New connection object.
		'''
		return self._http_connection(self, socket, proxy = self._proxy, websocket = self._websocket, error = self._error)
	# }}}
	def handle_ext(self, ext, mime): # {{{
		'''Add file extension to handle successfully.
		Files with this extension in httpdirs are served to
		callers with a 200 Ok code and the given mime type.
		This is a convenience function for adding the item to
		exts.
		@param ext: The extension to serve.
		@param mime: The mime type to use.
		@return None.
		'''
		self.exts[ext] = lambda socket, message: self.reply(socket, 200, message, mime)
	# }}}
	# Authentication. {{{
	## Authentication message.  See authenticate() for details.
	auth_message = None
	def authenticate(self, connection): # {{{
		'''Handle user authentication.
		To use authentication, set auth_message to a static message
		or define it as a method which returns a message.  The method
		is called with two arguments, connection and is_websocket.
		If it is or returns a True value (when cast to bool),
		authenticate will be called, which should return a bool.  If
		it returns False, the connection will be rejected without
		notifying the program.

		connection.data is a dict which contains the items 'user' and
		'password', set to their given values.  This dict may be
		changed by authenticate and is passed to the websocket.
		Apart from filling the initial contents, this module does not
		touch it.  Note that connection.data is empty when
		auth_message is called.  'user' and 'password' will be
		overwritten before authenticate is called, but other items
		can be added at will.

		***********************
		NOTE REGARDING SECURITY
		***********************
		The module uses plain text authentication.  Anyone capable of
		seeing the data can read the usernames and passwords.
		Therefore, if you want authentication, you will also want to
		use TLS to encrypt the connection.
		@param connection: The connection to authenticate.
			Especially connection.data['user'] and
			connection.data['password'] are of interest.
		@return True if the authentication succeeds, False if
			it does not.
		'''
		return True
	# }}}
	# }}}
	# The following function can be called by the overloaded page function.
	def reply(self, connection, code, message = None, content_type = None, headers = None, close = False):	# Send HTTP status code and headers, and optionally a message.  {{{
		'''Reply to a request for a document.
		There are three ways to call this function:
		* With a message and content_type.  This will serve the
		  data as a normal page.
		* With a code that is not 101, and no message or
		  content_type.  This will send an error.
		* With a code that is 101, and no message or
		  content_type.  This will open a websocket.
		@param connection: Requesting connection.
		@param code: HTTP response code to send.  Use 200 for a
			valid page.
		@param message: Data to send (as bytes), or None for an
			error message just showing the response code
			and its meaning.
		@param content_type: Content-Type of the message, or
			None if message is None.
		@param headers: Headers to send in addition to
			Content-Type and Content-Length, or None for no
			extra headers.
		@param close: True if the connection should be closed after
			this reply.
		@return None.
		'''
		assert code in httpcodes
		#log('Debug: sending reply %d %s for %s\n' % (code, httpcodes[code], connection.address.path))
		connection.socket.send(('HTTP/1.1 %d %s\r\n' % (code, httpcodes[code])).encode('utf-8'))
		if headers is None:
			headers = {}
		if message is None and code != 101:
			assert content_type is None
			content_type = 'text/html; charset=utf-8'
			message = ('<!DOCTYPE html><html><head><title>%d: %s</title></head><body><h1>%d: %s</h1></body></html>' % (code, httpcodes[code], code, httpcodes[code])).encode('utf-8')
		if close and 'Connection' not in headers:
			headers['Connection'] = 'close'
		if content_type is not None:
			headers['Content-Type'] = content_type
			headers['Content-Length'] = '%d' % len(message)
		else:
			assert code == 101
			message = b''
		connection.socket.send((''.join(['%s: %s\r\n' % (x, headers[x]) for x in headers]) + '\r\n').encode('utf-8') + message)
		if close:
			connection.socket.close()
	# }}}
	# If httpdirs is not given, or special handling is desired, this can be overloaded.
	def page(self, connection, path = None):	# A non-WebSocket page was requested.  Use connection.address, connection.method, connection.query, connection.headers and connection.body (which should be empty) to find out more.  {{{
		'''Serve a non-websocket page.
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
		'''
		if self.httpdirs is None:
			self.reply(connection, 501)
			return
		if path is None:
			path = connection.address.path
		if path == '/':
			address = 'index'
		else:
			address = '/' + unquote(path) + '/'
			while '/../' in address:
				# Don't handle this; just ignore it.
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
	# }}}
	def post(self, connection):	# A non-WebSocket page was requested with POST.  Same as page() above, plus connection.post, which is a dict of name:(headers, sent_filename, local_filename).  When done, the local files are unlinked; remove the items from the dict to prevent this.  The default is to return an error (so POST cannot be used to retrieve static pages!) {{{
		'''Handle POST request.
		This function responds with an error by default.  It
		must be overridden to handle POST requests.

		@param connection: Same as for page(), plus connection.post, which is a 2-tuple.
			The first element is a dict of name:['value', ...] for fields without a file.
			The second element is a dict of name:[(local_filename, remote_filename), ...] for fields with a file.
			When done, the local files are unlinked; remove the items from the dict to prevent this.
		@return True to keep connection open after this request, False to close it.
		'''
		log('Warning: ignoring POST request.')
		self.reply(connection, 501)
		return False
	# }}}
# }}}
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
def fgloop(*a, **ka): # {{{
	'''Activate all websockets and start the main loop.
	See the documentation for python-network for details.
	@return See python-network documentation.
	'''
	_activate_all()
	return network.fgloop(*a, **ka)
# }}}
def iteration(*a, **ka): # {{{
	'''Activate all websockets and run one loop iteration.
	See the documentation for python-network for details.
	@return See python-network documentation.
	'''
	_activate_all()
	return network.iteration(*a, **ka)
# }}}

// vim: set fileencoding=utf-8 foldmethod=marker :
