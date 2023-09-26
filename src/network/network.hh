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

// {{{ Includes.
//modulename = 'network'
//fhs.module_info(modulename, 'Networking made easy', '0.4', 'Bas Wijnen <wijnen@debian.org>')
//fhs.module_option(modulename, 'tls', 'default tls hostname for server sockets. The code may ignore this option. Set to - to request that tls is disabled on the server. If left empty, detects hostname.', default = '')
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

// Logging. {{{
log_output = sys.stderr
log_date = False

def set_log_output(file): // {{{
	'''Change target for log().
	By default, log() sends its output to standard error.  This function is
	used to change the target.
	@param file: The new file to write log output to.
	@return None.
	'''
	global log_output, log_date
	log_output = file
	log_date = True
// }}}

def log(*message, filename = None, line = None, funcname = None, depth = 0): // {{{
	'''Log a message.
	Write a message to log (default standard error, can be changed with
	set_log_output()).  A timestamp is added before the message and a
	newline is added to it.
	@param message: The message to log. Multiple arguments are logged on separate lines. Newlines in arguments cause the message to be split, so they should not contain a closing newline.
	@param filename: Override filename to report.
	@param line: Override line number to report.
	@param funcname: Override function name to report.
	@param depth: How deep to enter into the call stack for function info.
	@return None.
	'''
	t = time.strftime('%F %T' if log_date else '%T')
	source = inspect.currentframe().f_back
	for d in range(depth):
		source = source.f_back
	code = source.f_code
	if filename is None:
		filename = os.path.basename(code.co_filename)
	if funcname is None:
		funcname = code.co_name
	if line is None:
		line = source.f_lineno
	for msg in message:
		log_output.write(''.join(['%s %s:%s:%d:\t%s\n' % (t, filename, funcname, line, m) for m in str(msg).split('\n')]))
	log_output.flush()
// }}}
// }}}

// Main loop. {{{
struct Item { // {{{
	typedef bool (*Cb)(void *data);
	void *data;
	int fd;
	short events;
	Cb read;
	Cb write;
	Cb error;
	int handle;
}; // }}}

struct PollItems { // {{{
	struct pollfd *data;
	std::vector <Item *> items;
	int num;
	int capacity;
	int min_capacity;
	std::set <int> empty_items();	// Empty items that have lower index than num - 1.
	PollItems(int initial_capacity = 32) : data(new struct pollfd[initial_capacity]), num(0), capacity(initial_capacity), min_capacity(initial_capacity) {}
	int add(Item *item) { // {{{
		// Add an fd; return index.
		int ret;
		if (!empty_items.empty()) {
			// There is space at a removed index.
			ret = *empty_items.begin();
			empty_items.erase(empty_items.begin());
		}
		else {
			if (num == capacity) {
				// There is no space; extend capacity.
				capacity *= 8;
				struct pollfd *new_data = new struct_pollfd[capacity];
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
		return ret;
	} // }}}
	void remove(int index) { // {{{
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
				struct pollfd *new_data = new struct_pollfd[capacity];
				for (int i = 0; i < num; ++i)
					new_data[i] = data[i];
				delete[] data;
				data = new_data;
			}
			return;
		}
		data[index].fd = -1;
		empty_items.add(index);
	} // }}}
}; // }}}

class Loop { // {{{
	// Types. {{{
	typedef std::chrono::time_point <std::chrono::steady_clock> Time;
	typedef std::chrono::duration <std::chrono::steady_clock> Duration;
	typedef bool (*Cb)(void *data);
	struct IdleRecord {
		Cb cb;
		void *data;
	};
	struct TimeoutRecord {
		Time time;
		Duration interval;	// 0 for single shot.
		Cb cb;
		void *data;
	};
	// }}}

	bool running;
	bool aborting;
	std::list <IdleRecord> idle;
	PollItems items;
	std::set <TimeoutRecord> timeouts;

	Time now() { return std::chrono::steady_clock::now(); }
	int handle_timeouts() { // {{{
		Time current = now();
		while (!aborting && !timeouts.empty() && timeouts.begin()->time <= now) {
			TimeoutRecord rec = *timeouts.begin();
			timeouts.erase(timeouts.begin());
			bool keep = rec.cb(rec.data);
			if (keep && rec.interval > 0) {
				while (rec.time <= current)
					rec.time += rec.interval;
				add_timeout(rec.time, rec.cb, rec.data);
			}
		}
		if (timeouts.empty())
			return -1;
		return (timeouts.begin()->time - now) / 1ms;
	} // }}}
	void iteration(block = false) { // {{{
		// Do a single iteration of the main loop.
		// @return None.
		int t = handle_timeouts()
		if (!block)
			t = 0
		int nfds = poll(items.data, items.num, t);
		for (int i = 0; !aborting && i < items.num; ++i) {
			if (items.data[i].fd < 0)
				continue;
			short ev = items.data[i].revents;
			if (ev == 0)
				continue;
			if (ev & (POLLERR | POLLNVAL)) {
				if (!items.items[i].error || !items.items[i].error(items.items[i].data))
					items.remove(i);
			}
			else {
				if (ev & (POLLIN | POLLPRI)) {
					if (!items.items[i].read || !items.items[i].read(items.items[i].data)) {
						items.remove(i);
						continue;
					}
				}
				if (ev & POLLOUT) {
					if (!items.items[i].write || !items.items[i].write(items.items[i].data))
						items.remove(i);
				}
			}
		}
		handle_timeouts()
	// }}}
	void run() { // {{{
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
				if (!i->cb(i->data))
					remove_idle(i)
				if (!running)
					break;
			}
		}
		running = false;
		aborting = false;
	}
	// }}}
	void stop(bool force = false) { // {{{
		// Stop a running loop.
		// @return None.
		assert(running);
		running = false;
		if (force)
			aborting = true;
	} // }}}

	int add_io(Item &item) { // {{{
		return items.add(&item);
	} // }}}
	std::set <TimeoutRecord>::iterator add_timeout(TimeoutRecord &&timeout) { // {{{
		return timeouts.add(timeout);
	} // }}}
	std::list <IdleRecord> add_idle(IdleRecord &&record): // {{{
		return idle.push_back(record);
	} // }}}

	void remove_io(handle) { // {{{
		items.erase(handle);
	} // }}}
	void remove_timeout(std::set <TimeoutRecord>::iterator handle) { // {{{
		timeouts.erase(handle);
	} // }}}
	void remove_idle(std::list <IdleRecord>::iterator handle) { // {{{
		idle.erase(handle);
	} // }}}
}; // }}}
Loop *default_loop;
// }}}

// Network sockets. {{{
/* Windows does not allow this, so it will be non portable; probably not worth it, so just omit it I suppose.
class _Fake: // {{{
	'''File wrapper which can be used in place of a network.Socket.
	This class allows files (specifically sys.stdin and sys.stdout) to be
	used as a base for Socket.  Don't call this class directly, use
	wrap() instead.
	'''
	def __init__(self, i, o = None):
		'''Create a fake socket object.
		@param i: input file.
		@param o: output file.
		'''
		self._i = i
		self._o = o if o is not None else i
	def close(self):
		'''Close the fake socket object.
		@return None.'''
		pass
	def sendall(self, data):
		'''Send data to fake socket object.
		@return None.'''
		while len(data) > 0:
			fd = self._o if isinstance(self._o, int) else self._o.fileno()
			ret = os.write(fd, data)
			if ret >= 0:
				data = data[ret:]
				continue
			log('network.py: Failed to write data')
			traceback.print_exc()
	def recv(self, maxsize):
		'''Receive data from fake socket object.
		@return Received data.'''
		//log('recv fake')
		return os.read(self._i.fileno(), maxsize)
	def fileno(self):
		'''Return file descriptor for select (only for reading).
		@return The file descriptor.'''
		// For reading.
		return self._i.fileno()
// }}}

def wrap(i, o = None): // {{{
	'''Wrap two files into a fake socket.
	This function wraps an input and an output file (which may be the same)
	into a Socket object.
	@param i: input file.
	@param o: output file.
	'''
	return Socket(_Fake(i, o))
// }}}*/

typedef void (*ReadCb)(std::string &buffer);
class Socket { // {{{
	// Connection object.
	int fd;
	size_t maxsize;	// For read operations.
	std::string (*disconnect_cb)(std::string const &pending, void *data);
	void *disconnect_data;
	Item read_item;
	ReadCb read_cb;
	std::string buffer;
	static bool read_impl(void *data) { // {{{
		Socket *self = reinterpret_cast <Socket *>(data);
		self->buffer += self->recv();
		if (!read_cb)
			return false;
		self->read_cb(buffer);
		return true;
	} // }}}
public:
	// Read only address components; these are filled from address in the constructor.
	// <protocol>://<hostname>:<service>[?/#]<extra>
	std::string protocol;
	std::string hostname;
	std::string service;
	std::string extra;

	Socket(std::string const &address) { // {{{
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
			hostname = address.substr(p, q);
			if (address[q] == ':') {
				p = q + 1;
				q = address.find_first_of("/?#", p);
				if (q == std::string::npos)
					service = address.substr(p);
				else {
					service = address.substr(p, q);
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
			connect(fd, &addr, sizeof(addr));
		}
		else {
			struct addrinfo addr_hint;
			addr_hint.ai_family = 0;
			addr_hint.ai_socktype = SOCK_STREAM;
			addr_hint.ai_protocol = IPPROTO_TCP;
			struct addrinfo *addr;
			int code = getaddrinfo(hostname.c_str(), service.c_str(), &addr_hint, &addr);
			if (code != 0) {
				std::cerr << "unable to open socket: " << gai_strerror(code) << std::endl;
				throw "unable to open socket";
			}
			fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
			connect(fd, addr->ai_addr, addr->ai_addrlen);
		}
	} // }}}
	std::string close() { // {{{
		//'Close the network connection.
		// @return The data that was remaining in the line buffer, if any.
		if (fd < 0)
			return "";
		std::string pending = unread();
		::close(fd);
		fd = -1;
		if (disconnect_cb)
			return disconnect_cb(pending, disconnect_data);
		return pending;
	} // }}}
	void send(std::string const &data) { // {{{
		/* Send data over the network.
		Send data over the network.  Block until all data is in the buffer.
		@param data: data to write.  This should be of type bytes.
		@return None.
		*/
		if (fd < 0)
			return;
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
	void sendline(std::string const &data) { // {{{
		// Send a line of text.
		// Identical to send(), but data is a str and a newline is added.
		// @param data: line to send.  A newline is added.  This should be
		//	 of type str.  The data is sent as utf-8.
		send(data + "\n");
	} // }}}
	std::string recv() { // {{{
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
			std::string ret = close();
			if (!disconnect_cb)
				throw "network connection closed";
			return ret;
		}
		return std::string(buffer, num);
	} // }}}
	std::string rawread(Item::Cb cb, Item::Cb error = nullptr) { // {{{
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
		std::string ret = unread();
		read_item = Item {data, fd, POLLIN | POLLPRI, cb, nullptr, error};
		read_item.poll_handle = add_io(read_item);
		return ret;
	} // }}}
	void read(ReadCb callback, Item::Cb error = Item::Cb(), size_t maxsize = 4096) { // {{{
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
		std::string first = self.unread();
		mymaxsize = maxsize;
		read_cb = callback;
		rawread(read_impl, error);
		if (!first.empty())
			read_cb(first);
	} // }}}
	std::string unread(self) { // {{{
		/* Cancel a read() or rawread() callback.
		Cancel any read callback.
		@return Bytes left in the line buffer, if any.  The line buffer
			is cleared.
		*/
		if (read_item.handle >= 0) {
			remove_io(read_item.handle);
			read_item.handle = -1;
		}
		std::string ret = std::move(buffer);
		buffer.clear();
		return ret;
	} // }}}
#if 0
	def readlines(self, callback, error = None, maxsize = 4096): // {{{
		'''Buffer incoming data until a line is received, then call a function.
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
		'''
		if self.socket is None:
			return
		self._linebuffer = self.unread()
		self._maxsize = maxsize
		self._callback = (callback, True)
		self._event = add_read(self.socket, self._line_cb, error)
	// }}}
	def _line_cb(self): // {{{
		self._linebuffer += self.recv(self._maxsize)
		while b'\n' in self._linebuffer and self._event:
			assert self._callback[1] is not None	// Going directly from readlines() to rawread() is not allowed.
			if self._callback[1]:
				line, self._linebuffer = self._linebuffer.split(b'\n', 1)
				line = makestr(line)
				self._callback[0] (line)
			else:
				data = makestr(self._linebuffer)
				self._linebuffer = b''
				self._callback[0](data)
		return True
	// }}}
#endif
}; // }}}

#if 0
class Server: // {{{
	'''Listen on a network port and accept connections.'''
	def __init__(self, port, obj, address = '', backlog = 5, tls = False, disconnect_cb = None):
		'''Start a server.
		@param port: Port to listen on.  Can be a unix domain socket,
			or a numerical port, or a service name.
		@param obj: Object to create when a new connection is
			accepted.  The new object gets the nex Socket
			as parameter.  This can be a function instead
			of an object.
		@param address: Address to listen on.  If empty, listen
			on all IPv4 and IPv6 addresses.  If IPv6 is not
			supported, set this to "0.0.0.0" to listen only
			on IPv4.
		@param backlog: Number of connections that are accepted
			by the kernel while waiting for the program to
			handle them.
		@param tls: Whether TLS encryption should be enabled.
			If False or "-", it is disabled.  If True, it
			is enabled with the default hostname.  If None
			or "", it is enabled if possible.  If a str, it
			is enabled with that string used as hostname.
			New keys are generated if they are not
			available.  If you are serving to the internet,
			it is a good idea to get them signed by a
			certificate authority.  They are in
			~/.local/share/network/.
		@param disconnect_cb: Function which is called when a
			socket loses its connection.  It receives the
			socket and any data that was remaining in the
			buffer as an argument.
		'''
		self._obj = obj
		/// Port that is listened on. (read only)
		self.port = ''
		/// Whether the server listens for IPv6. (read only)
		self.ipv6 = False
		self._socket = None
		/// False or the hostname for which the TLS keys are used. (read only)
		self.tls = tls
		/// Currently active connections for this server. (read only set, but elements may be changed)
		self.connections = set()
		/// Disconnect handler, to be used for new sockets.
		self.disconnect_cb = disconnect_cb
		if isinstance(port, str) and '/' in port:
			// Unix socket.
			// TLS is ignored for these sockets.
			self.tls = False
			self._socket = socket.socket(socket.AF_UNIX)
			self._socket.bind(port)
			self.port = port
			self._socket.listen(backlog)
		else:
			self._tls_init()
			port = lookup(port)
			self._socket = socket.socket()
			self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
			self._socket.bind((address, port))
			self._socket.listen(backlog)
			if address == '':
				self._socket6 = socket.socket(socket.AF_INET6)
				self._socket6.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
				self._socket6.bind(('::1', port))
				self._socket6.listen(backlog)
				self.ipv6 = True
			self.port = port
		self._event = add_read(self._socket, lambda: self._cb(False), lambda: self._cb(False))
		if self.ipv6:
			self._event = add_read(self._socket6, lambda: self._cb(True), lambda: self._cb(True))
	def _cb(self, is_ipv6):
		//log('Accepted connection from %s; possibly attempting to set up encryption' % repr(new_socket))
		if self.tls:
			assert have_ssl
			try:
				with self._tls_context.wrap_socket(self._socket6 if is_ipv6 else self._socket, server_side = True) as ssock:
					new_socket = ssock.accept()
			except ssl.SSLError as e:
				log('Rejecting (non-TLS?) connection for %s: %s' % (repr(new_socket[1]), str(e)))
				try:
					new_socket[0].shutdown(socket.SHUT_RDWR)
				except:
					// Ignore errors here.
					pass
				return True
			except socket.error as e:
				log('Rejecting connection for %s: %s' % (repr(new_socket[1]), str(e)))
				try:
					new_socket[0].shutdown(socket.SHUT_RDWR)
				except:
					// Don't care about errors on shutdown.
					pass
				return True
			//log('Accepted TLS connection from %s' % repr(new_socket[1]))
		else:
			new_socket = (self._socket6 if is_ipv6 else self._socket).accept()
		s = Socket(new_socket[0], remote = new_socket[1], disconnect_cb = self.disconnect_cb, connections = self.connections)
		self._obj(s)
		return True
	def close(self):
		'''Stop the server.
		@return None.
		'''
		self._socket.close()
		self._socket = None
		if self.ipv6:
			self._socket6.close()
			self._socket6 = None
		if isinstance(self.port, str) and '/' in self.port:
			os.remove(self.port)
		self.port = ''
	def __del__(self):
		'''Stop the server.
		@return None.
		'''
		if self._socket is not None:
			self.close()
	def _tls_init(self):
		// Set up members for using tls, if requested.
		if self.tls in (False, '-'):
			self.tls = False
			return
		if self.tls in (None, True, ''):
			self.tls = fhs.module_get_config('network')['tls']
		if self.tls == '':
			self.tls = socket.getfqdn()
		elif self.tls == '-':
			self.tls = False
			return
		// Use tls.
		fc = fhs.read_data(os.path.join('certs', self.tls + os.extsep + 'pem'), opened = False, packagename = 'network')
		fk = fhs.read_data(os.path.join('private', self.tls + os.extsep + 'key'), opened = False, packagename = 'network')
		if fc is None or fk is None:
			// Create new self-signed certificate.
			certfile = fhs.write_data(os.path.join('certs', self.tls + os.extsep + 'pem'), opened = False, packagename = 'network')
			csrfile = fhs.write_data(os.path.join('csr', self.tls + os.extsep + 'csr'), opened = False, packagename = 'network')
			for p in (certfile, csrfile):
				path = os.path.dirname(p)
				if not os.path.exists(path):
					os.makedirs(path)
			keyfile = fhs.write_data(os.path.join('private', self.tls + os.extsep + 'key'), opened = False, packagename = 'network')
			path = os.path.dirname(keyfile)
			if not os.path.exists(path):
				os.makedirs(path, 0o700)
			os.system('openssl req -x509 -nodes -days 3650 -newkey rsa:4096 -subj "/CN=%s" -keyout "%s" -out "%s"' % (self.tls, keyfile, certfile))
			os.system('openssl req -subj "/CN=%s" -new -key "%s" -out "%s"' % (self.tls, keyfile, csrfile))
			fc = fhs.read_data(os.path.join('certs', self.tls + os.extsep + 'pem'), opened = False, packagename = 'network')
			fk = fhs.read_data(os.path.join('private', self.tls + os.extsep + 'key'), opened = False, packagename = 'network')
		self._tls_cert = fc
		self._tls_key = fk
		self._tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
		self._tls_context.load_cert_chain(self._tls_cert, self._tls_key)
// }}}
#endif
// }}}
