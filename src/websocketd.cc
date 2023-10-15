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

#include "websocketd.hh"

/*
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
*/

// vim: set fileencoding=utf-8 foldmethod=marker :
