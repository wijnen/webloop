#include <webloop/loop.hh>
#include <webloop/network.hh>
#include <cstdint>
#include <cstdio>

/*
OUTPUT:
int: 0.
END OF OUTPUT.
*/

static Webloop::Loop *loop;

class Socket : public Webloop::Socket <Socket> {
public:
	// Low level events. Usually handled internally.
	void raw_read();

	// Low level events, useful for applications.
	void connected();
	void disconnected();
	void write_done();

	// High level events.
	//void read(uint8_t *data, size_t size);
	void read_line(std::string const &line);
};

class Socket2 : public Webloop::Socket <Socket2> {
public:
	void connected();
	void disconnected();
	void read(std::string &buffer);
};

static Socket2 socket2;

void Socket::connected()
{
	printf("Connected.\n");
}

void Socket::raw_read()
{
	printf("Raw read ready.\n");
	handle_read_lines (&Socket::read_line);
	send("Writing {}.\n", 9944);
}

void Socket::write_done()
{
	printf("Write done.\n");
}

void Socket::read_line(std::string const &line)
{
	printf("Read line: %s\n", line.c_str());
	transfer_to(socket2);
	printf("Reading 2\n");
}

void Socket::disconnected()
{
	printf("Socket disconnected. THIS SHOULD NOT HAPPEN!\n");
}

void Socket2::connected()
{
	printf("Socket2 conected. THIS SHOULD NOT HAPPEN!\n");
}

void Socket2::read(std::string &buffer)
{
	printf("Read2 done: %s\n", buffer.c_str());
	buffer.clear();
	close();
}

void Socket2::disconnected()
{
	printf("Disconnected.\n");
	loop->stop();
}

int main(int /*argc*/, char ** /*argv*/)
{
	// TODO: bind cbs to Socket; copy to SocketBase on transfer or open.
	loop = Webloop::Loop::get();
	Socket socket;
	socket.handle_connected(&Socket::connected);
	socket.handle_disconnected(&Socket::disconnected);
	socket2.handle_connected(&Socket2::connected);
	socket2.handle_disconnected(&Socket2::disconnected);
	socket2.handle_read(&Socket2::read);
	socket.open(Webloop::URL("localhost:4433"));
	socket.handle_raw_read(&Socket::raw_read);
	loop->run();
	return 0;
}
