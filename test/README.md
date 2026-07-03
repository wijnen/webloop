#Tests for Webloop
This directory contains programs for testing the webloop library.

The programs are intended both for regression testing, and as examples for
using the library.

##Components
	- url.cc
		- tools
	- tools.cc
		- webobject
	- loop.cc
		- tools
	- fhs.cc
		- tools
	- webobject.cc
		- tools
		- network
	- network.cc
		- tools
		- loop
		- url
		- webobject
	- websocketd.cc
		- network
		- webobject
		- url
		- tools
		- loop
		- fhs

##Programs
The following tests are implemented here:
  - url parsing.
  - tools: strip, split, b64, sha1, logging.
  - loop: run with io, idle and timeout records.
  - fhs: argument parsing, path finding.
  - webobject: create all objects and handle them.
  - network server.
  - network client.
  - websocket server.
  - websocket client.
