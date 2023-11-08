// This module implements fhs directory support in C++.
// vim: set fileencoding=utf-8 foldmethod=marker :

/* {{{ Copyright 2013-2019 Bas Wijnen <wijnen@debian.org>
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
// }}}*/

#ifndef _FHS_HH
#define _FHS_HH

// File documentation. {{{
/**@mainpage
This module makes it easy to find files in the locations that are defined for
them by the FHS.  Some locations are not defined there.  This module chooses a
location for those.

It also defines a configuration file format which is used automatically when
initializing this module.
*/

/**@file
This module makes it easy to find files in the locations that are defined for
them by the FHS.  Some locations are not defined there.  This module chooses a
location for those.

It also defines a configuration file format which is used automatically when
initializing this module.
*/

/**@package fhs Module for using paths as described in the FHS.
This module makes it easy to find files in the locations that are defined for
them by the FHS.  Some locations are not defined there.  This module chooses a
location for those.

It also defines a configuration file format which is used automatically when
initializing this module.
*/
// }}}

/* Paths and how they are handled by this module: {{{
// /etc			configfile
// /run			runtimefile
// /tmp			tempfile
// /usr/lib/package	datafile
// /usr/local		datafile
// /usr/share/package	datafile
// /var/cache		cachefile
// /var/games		datafile
// /var/lib/package	datafile
// /var/lock		lockfile
// /var/log		logfile
// /var/spool		spoolfile
// /var/tmp		tempfile?

// /home			(xdgbasedir)
// /root			(xdgbasedir)
// /bin			-
// /boot			-
// /dev			-
// /lib			-
// /lib<qual>		-
// /media		-
// /mnt			-
// /opt			-
// /sbin			-
// /srv			-
// /usr/bin		-
// /usr/include		-
// /usr/libexec		-
// /usr/lib<qual>	-
// /usr/sbin		-
// /usr/src		-
// /var/lib		-
// /var/opt		-
// /var/run		-

// FHS: http://www.linuxbase.org/betaspecs/fhs/fhs.html
// XDG basedir: http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html

// So: configfile, runtimefile, tempfile, datafile, cachefile, lockfile, logfile, spoolfile
// }}}*/

// Includes. {{{
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <list>
#include <filesystem>
#include <algorithm>
#include "tools.hh"
#include "url.hh"
// }}}

namespace Webloop {

extern std::vector <std::string> arguments;

// Options. {{{
/** Base struct for option definitions. Derived types must define a value member containing the computed value, and default_value and default_noarg constructor attributes. */
struct OptionBase { // {{{
	// All registered options.
	static std::list <OptionBase *> all_options;

	// Set at option parse time.
	bool is_default;	/// true if the default value is used, false if it was passed by the user.

	// Set at option define time.
	std::string name;	/// Name of the option.
	std::string help;	/// Help text for the option.
	bool must_have_parameter;	/// False for optional parameters; true otherwise.
	bool can_have_parameter;	/// False for bool parameters; true otherwise.
	bool multiple;	/// Whether this option may be specified multiple times.
	char shortopt;	/// Short option name.

	// Constructor.
	OptionBase(std::string const &name, std::string const &help, bool must_have_parameter, bool can_have_parameter, bool multiple, char shortopt, bool store);

	// Implementation of the actual option parsing.
	virtual void parse(std::string const &param) = 0;	// Parse option parameter.
	virtual void noparse() = 0;	// Handle option without parameter.
	enum PrintType { PRINT_STORE, PRINT_VALUE, PRINT_DEFAULT, PRINT_DEFAULT_NOARG };
	virtual std::string print(PrintType type) const = 0;	// Serialize a value.
}; // }}}

template <typename T> struct Option: public OptionBase { // {{{
	T value;
	T const default_value;
	T const default_noarg;
	bool has_parameter;	/// true if a parameter was specified, false if default value was used or no parameter was specified.

	// Constructors.
	Option(std::string const &name, std::string const &help, char shortopt = 0)
		: OptionBase(name, help, true, true, false, shortopt, true), value(), default_value(), default_noarg(), has_parameter(false) {}
	Option(std::string const &name, std::string const &help, char shortopt, T const &default_value)
		: OptionBase(name, help, true, true, false, shortopt, true), value(default_value), default_value(default_value), default_noarg(), has_parameter(false) {}
	Option(std::string const &name, std::string const &help, char shortopt, T const &default_value, T const &default_noarg)
		: OptionBase(name, help, false, true, false, shortopt, true), value(default_value), default_value(default_value), default_noarg(default_noarg), has_parameter(false) {}

	// Copy constructor does not register the option.
	Option(Option const &other)
		: OptionBase(other.name, other.help, other.must_have_parameter, other.can_have_parameter, other.multiple, other.args, false), value(other.value), default_value(other.default_value), default_noarg(other.default_noarg), has_parameter(other.has_parameter) {}

	// Generic parse function.
	void parse(std::string const &param) override {
		std::istringstream p(param);
		p >> value;
		if (std::find_if_not(param.begin() + p.tellg(), param.end(), [](char c) { return std::isspace(c); }) != param.end())
			throw "junk found after option value";
	}

	// Generic noparse function.
	void noparse() override {
		value = default_noarg;
	}

	// Generic print function.
	std::string print(PrintType type) const override {
		switch (type) {
		case PRINT_STORE:
			return (std::ostringstream() << name << "=" << value << "\n").str();
		case PRINT_VALUE:
			return (std::ostringstream() << value).str();
		case PRINT_DEFAULT:
			return (std::ostringstream() << default_value).str();
		case PRINT_DEFAULT_NOARG:
			return (std::ostringstream() << default_noarg).str();
		default:
			throw "invalid print type";
		}
	}
}; // }}}

typedef Option<int> IntOption;
typedef Option<std::string> StringOption;
typedef Option<bool> BoolOption;
typedef Option<double> DoubleOption;

template <typename T> struct MultiOption: public OptionBase { // {{{
	std::list <T> value;
	T const default_noarg;
	std::list <bool> has_parameter;	/// true if a parameter was specified, false if not; has one element for each element in value.

	// Constructors.
	MultiOption(std::string const &name, std::string const &help, char shortopt = 0)
		: OptionBase(name, help, true, true, true, shortopt, true), value(), default_noarg(), has_parameter() {}
	MultiOption(std::string const &name, std::string const &help, char shortopt, T const &default_noarg)
		: OptionBase(name, help, false, true, true, shortopt, true), value(), default_noarg(default_noarg), has_parameter() {}

	// Copy constructor does not register the option.
	MultiOption(MultiOption const &other)
		: OptionBase(other.name, other.help, other.must_have_parameter, other.can_have_parameter, other.args, false), value(other.value), default_noarg(other.default_noarg), has_parameter(other.has_parameter) {}

	// Generic parse function.
	void parse(std::string const &param) override {
		auto parts = split(param, -1, 0, ";");
		for (auto part: parts) {
			std::istringstream p(URL::decode(part));
			T single_value;
			p >> single_value;
			if (std::find_if_not(param.begin() + p.tellg(), param.end(), [](char c) { return std::isspace(c); }) != param.end())
				throw "junk found after option value";
			value.push_back(single_value);
		}
	}

	// Generic noparse function.
	void noparse() override {
		has_parameter.push_back(false);
		value.push_back(default_noarg);
	}

	// Generic print function.
	std::string print(PrintType type) const override {
		std::string ret;
		switch (type) {
		case PRINT_STORE:
		case PRINT_VALUE:
		{
			std::ostringstream s;
			for (auto &i: value) {
				std::string v = (std::ostringstream() << i).str();
				if (type == PRINT_STORE)
					s << name << "=";
				s << URL::encode(v);
				if (type == PRINT_STORE)
					s << "\n";
			}
			return s.str();
		}
		case PRINT_DEFAULT:
			return {};
		case PRINT_DEFAULT_NOARG:
			return (std::ostringstream() << default_noarg).str();
		default:
			throw "invalid print type";
		}
	}
}; // }}}

typedef MultiOption<int> IntMultiOption;
typedef MultiOption<std::string> StringMultiOption;
typedef MultiOption<bool> BoolMultiOption;
typedef MultiOption<double> DoubleMultiOption;
// }}}

// Globals. {{{
enum InitState { UNINITIALIZED, INITIALIZING, INITIALIZED };
/// Flag that is set to true when init() is called.
extern InitState initialized;
/// Flag that is set during init() if --system was specified, or the application set this directly; that should be done before calling init().
/// It should be set to true or false; it's defined as an int to detect it not having been set, to disable the --system option if it was forced to false.
extern int is_system;
/// Flag that is set before calling init() by the application if this is a game (makes it use /usr/games instead of /usr/bin).
extern bool is_game;
/// Default program name; can be overridden from functions that use it. Default value is set in init().
extern std::string pname;
/// Current user's home directory.
extern std::filesystem::path HOME;
// Callback type for atinit().
typedef void (*generic_cb)(void *arg);
// }}}

void init(char **argv, std::string const &help = std::string(), std::string const &version = std::string(), std::string const &contact = std::string(), std::string const &packagename = std::string());
void atinit(generic_cb target, void *data);

// Configuration files. {{{
/// XDG home directory.
extern std::filesystem::path XDG_CONFIG_HOME;
/// XDG config directory search path.
extern std::list <std::filesystem::path> XDG_CONFIG_DIRS;

std::filesystem::path write_config_name(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string(), bool dir = false);
std::ofstream write_config_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path write_config_dir(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string());
std::list <std::filesystem::path> read_config_names(std::string const &name = std::string(), std::string const &packagename = std::string(), bool dir = false, bool multiple = true);
std::ifstream read_config_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path read_config_dir(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_config_file(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_config_dir(std::string const &name = std::string(), std::string const &packagename = std::string());

/*
// Module commandline argument handling. {{{
def module_info(module_name, desc, version, contact): // {{{
	///Register information about a module.
	This should be called by modules that use python-fhs to get their own commandline arguments.
	@param module_name: The name of the calling module.
	@param desc: Module description in --version output.
	@param version: Version number in --version output.
	@param contact: Contact information in --version output.
	//
	assert(initialized != INITIALIZED);
	if module_name in _module_info:
		print('Warning: duplicate registration of information for module %s' % module_name, file = sys.stderr)
		return
	_module_info[module_name] = {'desc': desc, 'version': version, 'contact': contact}
	_module_config[module_name] = set()
// }}}
// }}}
*/
// }}}

// Runtime files. {{{
/// XDG runtime directory.
extern std::filesystem::path XDG_RUNTIME_DIR;
std::filesystem::path write_runtime_name(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string(), bool dir = false);
std::ofstream write_runtime_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path write_runtime_dir(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string());
std::list <std::filesystem::path> read_runtime_names(std::string const &name = std::string(), std::string const &packagename = std::string(), bool dir = false, bool multiple = true);
std::ifstream read_runtime_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path read_runtime_dir(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_runtime_file(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_runtime_dir(std::string const &name = std::string(), std::string const &packagename = std::string());
// }}}

// Data files. {{{
/// XDG data directory.
extern std::filesystem::path XDG_DATA_HOME;
/// XDG data directory search path.
extern std::list <std::filesystem::path> XDG_DATA_DIRS;

std::filesystem::path write_data_name(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string(), bool dir = false);
std::ofstream write_data_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path write_data_dir(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string());
std::list <std::filesystem::path> read_data_names(std::string const &name = std::string(), std::string const &packagename = std::string(), bool dir = false, bool multiple = true);
std::ifstream read_data_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path read_data_dir(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_data_file(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_data_dir(std::string const &name = std::string(), std::string const &packagename = std::string());
// }}}

// Cache files. {{{
/// XDG cache directory.
extern std::filesystem::path XDG_CACHE_HOME;

std::filesystem::path write_cache_name(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string(), bool dir = false);
std::ofstream write_cache_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path write_cache_dir(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string());
std::list <std::filesystem::path> read_cache_names(std::string const &name = std::string(), std::string const &packagename = std::string(), bool dir = false, bool multiple = true);
std::ifstream read_cache_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path read_cache_dir(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_cache_file(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_cache_dir(std::string const &name = std::string(), std::string const &packagename = std::string());
// }}}

// Spool files. {{{
std::filesystem::path write_spool_name(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string(), bool dir = false);
std::ofstream write_spool_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path write_spool_dir(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string());
std::list <std::filesystem::path> read_spool_names(std::string const &name = std::string(), std::string const &packagename = std::string(), bool dir = false, bool multiple = true);
std::ifstream read_spool_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path read_spool_dir(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_spool_file(std::string const &name = std::string(), std::string const &packagename = std::string());
void remove_spool_dir(std::string const &name = std::string(), std::string const &packagename = std::string());
// }}}

// Log files. {{{
std::filesystem::path write_log_name(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string(), bool dir = false);
std::ofstream write_log_file(std::string const &name = std::string(), std::string const &packagename = std::string());
std::filesystem::path write_log_dir(std::string const &name = std::string(), bool create = true, std::string const &packagename = std::string());
// }}}

// Temporary files. {{{
std::ofstream write_temp_file(std::string const &name = std::string());
std::filesystem::path write_temp_dir(std::string const &name = std::string());
// }}}

// TODO: Support lock files.
#if 0

// Locks. {{{
def lock(name = None, info = '', packagename = None):
	/**Acquire a lock.
	@todo locks are currently not implemented.
	*/
	assert(initialized != UNINITIALIZED);
	// TODO: Support lock()

def unlock(name = None, packagename = None):
	/**Release a lock.
	@todo locks are currently not implemented.
	*/
	assert(initialized != UNINITIALIZED);
	// TODO: Support unlock()
// }}}
#endif

// Overrides. {{{
// Override for default value of double options.
template <> Option <double>::Option(std::string const &name, std::string const &help, char shortopt);
template <> MultiOption <double>::MultiOption(std::string const &name, std::string const &help, char shortopt);

// Override constructor defaults for BoolOption.
template <> Option <bool>::Option(std::string const &name, std::string const &help, char shortopt);
template <> Option <bool>::Option(std::string const &name, std::string const &help, char shortopt, bool const &default_value);
template <> Option <bool>::Option(std::string const &name, std::string const &help, char shortopt, bool const &default_value, bool const &default_noarg);
template <> MultiOption <bool>::MultiOption(std::string const &name, std::string const &help, char shortopt);
template <> MultiOption <bool>::MultiOption(std::string const &name, std::string const &help, char shortopt, bool const &default_noarg);

// Override parse and print functions for strings.
template <> void Option <std::string>::parse(std::string const &param);
template <> void MultiOption <std::string>::parse(std::string const &param);
template <> std::string Option <std::string>::print(PrintType type) const;
template <> std::string MultiOption <std::string>::print(PrintType type) const;
// }}}

}

#endif
