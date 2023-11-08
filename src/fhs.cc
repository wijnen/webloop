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

#include "webloop/fhs.hh"
#include <set>
#include <ranges>
#include <cassert>
#include <cstring>

namespace Webloop {

// Globals. {{{
std::vector <std::string> arguments;
/// Flag that is set to true when init() is called.
InitState initialized = UNINITIALIZED;
/// Flag that is set during init() if --system was specified, or the application set this directly; that should be done before calling init().
/// It should be set to true or false; it's defined as an int to detect it not having been set, to disable the --system option if it was forced to false.
int is_system = -1;
/// Flag that is set before calling init() by the application if this is a game (makes it use /usr/games instead of /usr/bin).
bool is_game = false;
/// Default program name; can be overridden from functions that use it. Default value is set in init().
std::string pname;
/// Current user's home directory.
std::filesystem::path HOME
	= []() -> std::filesystem::path {
		char const *home = std::getenv("HOME");
		if (home)
			return home;
		return std::filesystem::path();
	}();
// Internal variables. This is a singleton class anyway, so make them globals to keep them out of the header file.
static std::filesystem::path _base;
struct AtinitRecord {
	generic_cb cb;
	void *data;
};
static std::list <AtinitRecord> _atinit;
struct Info {
	std::string help;
	std::string version;
	std::string contact;
};
static Info _info;
static std::filesystem::path _temp_file_dir;
// }}}

std::list <OptionBase *> OptionBase::all_options;

OptionBase::OptionBase(std::string const &name, std::string const &help, bool must_have_parameter, bool can_have_parameter, bool multiple, char shortopt, bool store) // {{{
		: name(name), help(help), must_have_parameter(must_have_parameter), can_have_parameter(can_have_parameter), multiple(multiple), shortopt(shortopt)
{
	if (store) {
		if (initialized == UNINITIALIZED)
			all_options.push_back(this);
		else
			all_options.push_front(this);
	}
} // }}}

// Option overrides. {{{
// Override for default value of double options.
template <>
Option <double>::Option(std::string const &name, std::string const &help, char shortopt)
		: OptionBase(name, help, true, true, false, shortopt, true), value(NAN), default_value(NAN), default_noarg(0), has_parameter(false) {}

template <>
MultiOption <double>::MultiOption(std::string const &name, std::string const &help, char shortopt)
		: OptionBase(name, help, true, true, true, shortopt, true), value(), default_noarg(0), has_parameter() {}

// Override constructor defaults for BoolOption.
template <>
Option <bool>::Option(std::string const &name, std::string const &help, char shortopt)
		: OptionBase(name, help, false, false, false, shortopt, true), value(false), default_value(false), default_noarg(true), has_parameter(false) {}
template <>
Option <bool>::Option(std::string const &name, std::string const &help, char shortopt, bool const &default_value)
		: OptionBase(name, help, false, false, false, shortopt, true), value(default_value), default_value(default_value), default_noarg(!default_value), has_parameter(false) {}
template <>
Option <bool>::Option(std::string const &name, std::string const &help, char shortopt, bool const &default_value, bool const &default_noarg)
		: OptionBase(name, help, false, false, false, shortopt, true), value(default_value), default_value(default_value), default_noarg(default_noarg), has_parameter(false) {}
template <>
MultiOption <bool>::MultiOption(std::string const &name, std::string const &help, char shortopt)
		: OptionBase(name, help, false, false, true, shortopt, true), value(), default_noarg(true), has_parameter() {}
template <>
MultiOption <bool>::MultiOption(std::string const &name, std::string const &help, char shortopt, bool const &default_noarg)
		: OptionBase(name, help, false, false, true, shortopt, true), value(), default_noarg(default_noarg), has_parameter() {}

// Override parse and print functions for strings.
static std::string parse_string(std::string const &src) { // {{{
	std::ostringstream ret;
	auto p = src.begin();
	while (p != src.end()) {
		if (*p == '\\') {
			++p;
			auto n = std::find(p, src.end(), ';');
			if (n == src.end()) {
				// Ignore invalid escape.
				break;
			}
			std::string sub = src.substr(p - src.begin(), n - p);
			ret << char(atoi(sub.c_str()));
			p = n + 1;
		}
		else {
			if (*p >= 32 && *p < 127)
				ret << *p;
			// Anything else (in particular newlines) is silently ignored.
			++p;
		}
	}
	return ret.str();
} // }}}

template <> void Option <std::string>::parse(std::string const &param) { has_parameter = true; value = parse_string(param); }
template <> void MultiOption <std::string>::parse(std::string const &param) { // {{{
	has_parameter.push_back(true);
	value.push_back(parse_string(param));
} // }}}

template <> std::string Option <std::string>::print(PrintType type) const { // {{{
	std::ostringstream ret;
	std::string target;
	switch (type) {
	case PRINT_STORE:
		target = name + '=';
		// Fall through.
	case PRINT_VALUE:
		target += value;
		break;
	case PRINT_DEFAULT:
		target = default_value;
		break;
	case PRINT_DEFAULT_NOARG:
		target = default_noarg;
		break;
	}
	for (auto x: target) {
		if (x < 32 || x >= 127 || x == '\\')
			ret << "\\" << std::hex << x << ";";
		else
			ret << x;
	}
	if (type == PRINT_STORE)
		return ret.str() + "\n";
	else
		return ret.str();
} // }}}

static std::string print_string(std::string const &target) {
	std::ostringstream ret;
	for (auto x: target) {
		if (x < 32 || x >= 127 || x == '\\')
			ret << "\\" << std::hex << x << ";";
		else
			ret << x;
	}
	return ret.str();
}

template <> std::string MultiOption <std::string>::print(PrintType type) const { // {{{
	switch (type) {
	case PRINT_STORE:
	{
		std::string ret;
		auto v = value.begin();
		auto p = has_parameter.begin();
		for (; v != value.end(); ++v, ++p) {
			if (!*p)
				ret += name + "\n";
			else
				ret += name + "=" + print_string(*v) + "\n";
		}
		return ret;
	}
	case PRINT_VALUE:
		break;
	case PRINT_DEFAULT:
		return {};
	case PRINT_DEFAULT_NOARG:
		return print_string(default_noarg);
	}
	std::ostringstream ret;
	std::string sep = "";
	for (auto &part: value) {
		ret << sep << print_string(part);
		sep = "\t";
	}
	return ret.str();
} // }}}
// }}}

void clean_temps() { // {{{
	if (_temp_file_dir != std::filesystem::path()) {
		std::filesystem::remove_all(_temp_file_dir);
		_temp_file_dir = std::filesystem::path();
	}
} // }}}

static std::filesystem::path write_name( // {{{
		std::filesystem::path const &home,
		std::filesystem::path const &system_path,
		std::string const &default_ext,
		std::string const &name,
		bool create,
		std::string const &packagename,
		bool dir
) {
	/**Open a config file for writing.  The file is not truncated if it exists.
	@param name: Name of the config file.
	@param create: Create directories.
	@param packagename: Override the packagename.
	@param dir: Return directory name if true, filename otherwise.
	@return The name of the file or directory.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::path filename = packagename.empty() ? pname : packagename;
	if (name.empty()) {
		if (!dir)
			filename += default_ext;
	}
	else if (is_system)
		filename = name;
	else
		filename /= name;
	std::filesystem::path d;
	if (is_system) {
		d = system_path / pname;
		if (packagename.size() != 0 && packagename != pname)
			d /= packagename;
	}
	else
		d = home;
	std::filesystem::path target = d / filename;
	if (create) {
		if (!dir)
			std::filesystem::create_directories(target.parent_path());
		else
			std::filesystem::create_directories(d);
	}
	return target;
} // }}}

static std::list <std::filesystem::path> read_names( // {{{
		std::filesystem::path const &home,	// XDG home directory for this type (e.g. XDG_CONFIG_HOME)
		std::list <std::filesystem::path> const &system_paths,	// XDG system paths (e.g. {"/etc/xdg", "/usr/local/etc/xdg"})
		std::list <std::filesystem::path> const &paths,	// XDG user paths (e.g. XDG_CONFIG_DIRS, split into a std::list)
		std::string const &default_ext,	// Extension for generated filename.
		std::string const &name,	// Name of file to be read; can be empty for default filename.
		std::string const &packagename,	// Override for package name; can be empty to use default.
		bool dir,	// True if directories are requested, false otherwise.
		bool multiple	// True if multiple files are searched, false otherwise. If this is false, the returned list has 0 or 1 elements.
) {
	/**Open a config file for reading.  The paramers should be identical to what was used to create the file with write_config().
	@param name: Name of the config file.
	@param dir: Return a directory name if true, a file or filename if false (the default).
	@param opened: Open the file if true (the default), report the name if false.
	@param packagename: Override the packagename.
	@return The opened file, or the name of the file or directory.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::path filename;
	std::string pkg = packagename.empty() ? pname : packagename;
	if (name.empty()) {
		filename = pkg;
		if (!dir)
			filename += default_ext;
	}
	else
		filename = name;

	std::set <std::filesystem::path> seen;
	std::list <std::filesystem::path> result;

	auto contains = [seen](std::filesystem::path path) {
		for (auto p: seen) {
			if (std::filesystem::equivalent(p, path))
				return true;
		}
		return false;
	};

	if (!is_system) {
		std::filesystem::path t = home / (name.empty() ? filename : std::filesystem::path(pkg) / name);
		if (!contains(t) && (dir ? std::filesystem::is_directory(t) : std::filesystem::is_regular_file(t))) {
			result.push_back(t);
			if (!multiple)
				return result;
			seen.insert(t);
		}
	}
	std::list <std::filesystem::path> dirs = system_paths;
	if (!is_system)
		dirs.insert(dirs.end(), paths.begin(), paths.end());
	std::list <std::filesystem::path> all_dirs, packagename_dirs;
	if (!is_system)
		all_dirs = {pkg, std::filesystem::current_path(), _base};
	bool build_packagename_dirs = !packagename.empty() && packagename != pname;
	for (auto d: dirs) {
		if (build_packagename_dirs)
			packagename_dirs.push_back(d / packagename);
		all_dirs.push_back(d / pname);
	}
	if (build_packagename_dirs)
		all_dirs.insert(all_dirs.end(), packagename_dirs.begin(), packagename_dirs.end());

	for (auto d: dirs) {
		std::filesystem::path t = d / filename;
		if (!contains(t) && (dir ? std::filesystem::is_directory(t) : std::filesystem::is_regular_file(t))) {
			result.push_back(t);
			if (!multiple)
				return result;
			seen.insert(t);
		}
	}
	return result;
} // }}}

// Configuration files. {{{
/// XDG home directory.
std::filesystem::path XDG_CONFIG_HOME
	= []() -> std::filesystem::path {
		char const *envname = std::getenv("XDG_CONFIG_HOME");
		if (envname)
			return envname;
		return HOME / ".config";
	}();
/// XDG config directory search path.
std::list <std::filesystem::path> XDG_CONFIG_DIRS;

// The only point of non-binary mode is to make the files less useful in Windows. That is never useful, so always open everything as binary.
static auto write_mode = std::ios::out | std::ios::binary;
static auto read_mode = std::ios::in | std::ios::binary;

std::filesystem::path write_config_name(std::string const &name, bool create, std::string const &packagename, bool dir) { // {{{
	/**Open a config file for writing.  The file is not truncated if it exists.
	@param name: Name of the config file.
	@param create: Create directories.
	@param packagename: Override the packagename.
	@param dir: Return directory name if true, filename otherwise.
	@return The name of the file or directory.
	*/
	return write_name(XDG_CONFIG_HOME, "/etc/xdg", ".cfg", name, create, packagename, dir);
} // }}}
std::ofstream write_config_file(std::string const &name, std::string const &packagename) { // {{{
	std::filesystem::path filename = write_config_name(name, true, packagename, false);
	return std::ofstream(filename, write_mode);
} // }}}
std::filesystem::path write_config_dir(std::string const &name, bool create, std::string const &packagename) { // {{{
	return write_config_name(name, create, packagename, true);
} // }}}

std::list <std::filesystem::path> read_config_names(std::string const &name, std::string const &packagename, bool dir, bool multiple) { // {{{
	/**Open a config file for reading.  The paramers should be identical to what was used to create the file with write_config().
	@param name: Name of the config file.
	@param dir: Return a directory name if true, a file or filename if false (the default).
	@param opened: Open the file if true (the default), report the name if false.
	@param packagename: Override the packagename.
	@return The opened file, or the name of the file or directory.
	*/
	return read_names(XDG_CONFIG_HOME, {"/etc/xdg", "/usr/local/etc/xdg"}, XDG_CONFIG_DIRS, ".cfg", name, packagename, dir, multiple);
} // }}}
std::ifstream read_config_file(std::string const &name, std::string const &packagename) { // {{{
	auto names = read_config_names(name, packagename, false, false);
	if (names.empty())
		return std::ifstream();
	return std::ifstream(names.front(), read_mode);
} // }}}
std::filesystem::path read_config_dir(std::string const &name, std::string const &packagename) { // {{{
	return read_config_names(name, packagename, true, false).front();
} // }}}

void remove_config_file(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a config file.  Use the same parameters as were used to create it with write_config_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove(read_config_names(name, packagename, false, false).front());
} // }}}
void remove_config_dir(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a config file.  Use the same parameters as were used to create it with write_config_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove_all(read_config_names(name, packagename, true, false).front());
} // }}}
// }}}

// Commandline argument handling. {{{
// Commandline parser helper functions. {{{
static void help_text() { // {{{
	if (_info.help.empty()) {
		if (_info.version.empty())
			std::cerr << "this is " << pname;
		else
			std::cerr << "this is " << pname << " version " << _info.version;
	}
	else
		std::cerr << _info.help;

	std::cerr << "\n\nSupported option arguments:\n";
	for (int module_help = 0; module_help < 2; ++module_help) {
		for (auto option: OptionBase::all_options) {
			// TODO: support module options.
			if (module_help > 0)
				continue;
			//if (option->args.module_name.empty() != bool(module_help))
			//	continue;
			std::string m;
			if (option->multiple)
				m += " (This option can be passed multiple times)";
			std::string optname = std::string("--") + option->name;
			if (!option->can_have_parameter) {
				if (option->shortopt)
					optname += std::string(", -") + option->shortopt;
				std::cerr << "\t" << optname << "\n\t\t" << option->help << m << "\n";
			}
			else {
				if (option->must_have_parameter) {
					optname += "=<value>";
					if (option->shortopt)
						optname += std::string(", -") + option->shortopt + "<value>";
				}
				else {
					optname += "[=<value>]";
					if (option->shortopt)
						optname += std::string(", -") + option->shortopt + "[<value>]";
				}
				std::cerr << "\t" << optname << "\n\t\t" << option->help << "\n\t\tDefault: " << option->print(OptionBase::PRINT_DEFAULT) << m << "\n";
			}
		}
	}

	if (!_info.contact.empty())
		std::cerr << "\nPlease send feedback and bug reports to " << _info.contact << "\n";
	std::cerr << std::flush;
} // }}}

static void version_text() { // {{{
	if (_info.version.empty())
		std::cerr << pname << "\n";
	else
		std::cerr << pname << " version " << _info.version << "\n";
	if (!_info.contact.empty())
		std::cerr << "\nPlease send feedback and bug reports to " << _info.contact << "\n";
/* TODO: Add module info.
	if len(_module_info) > 0:
		print('\nUsing modules:', file = sys.stderr)
		for mod in _module_info:
			print('\t%s version %s\n\t\t%s' % (mod, _module_info[mod]['version'], _module_info[mod]['desc']), file = sys.stderr)
			if _module_info[mod]['contact'] is not None:
				print('\t\tPlease send feedback and bug reports for %s to %s' % (mod, _module_info[mod]['contact']), file = sys.stderr)
				*/
} // }}}

static void load_config(std::string const &filename = std::string(), std::string const &packagename = std::string()) { // {{{
	auto config = read_config_file(filename, packagename);
	if (!config.is_open())
		return;
	while (true) {
		std::string cfg;
		std::getline(config, cfg, '\n');
		if (!config)
			break;
		if (cfg.empty() || cfg[0] == '#')
			continue;
		auto p = std::find(cfg.begin(), cfg.end(), '=');
		std::string key;
		std::string rawvalue;
		bool have_value;
		if (p == cfg.end()) {
			key = cfg;
			have_value = false;
		}
		else {
			key = cfg.substr(0, p - cfg.begin());
			rawvalue = cfg.substr(p + 1 - cfg.begin());
			have_value = true;
		}
		OptionBase *option = nullptr;
		for (auto o: OptionBase::all_options) {
			if (key == o->name) {
				option = o;
				break;
			}
		}
		if (!option) {
			std::cerr << "invalid key " << key << " in config file " << filename << std::endl;
			continue;
		}
		if (!option->can_have_parameter) {
			if (have_value) {
				std::cerr << "option " << key << " does not accept parameter (" << rawvalue << ") in config file " << filename << std::endl;
				continue;
			}
		}
		else if (option->must_have_parameter && !have_value) {
			std::cerr << "option " << key << " needs a parameter in config file " << filename << std::endl;
			continue;
		}

		// Use only the first time an option is found.
		if (!option->is_default)
			continue;

		option->parse(rawvalue);
		option->is_default = false;
	}
} // }}}

static void save_config(std::string const &name = std::string(), std::string const &packagename = std::string()) { // {{{
	/**Save a dict as a configuration file.
	Write the config dict to a file in the configuration directory.  The
	file is named <packagename>.ini, unless overridden.
	@param config: The data to be saved.  All values are converted to str.
	@param name: The name of the file to be saved.  ".ini" is appended to
		this.
	@param packagename: Override for the name of the package, to determine
		the directory to save to.
	*/
	assert(initialized != UNINITIALIZED);
	auto config = write_config_file(name, packagename);
	for (auto o: OptionBase::all_options) {
		// Don't store options that were not specified.
		if (o->is_default)
			continue;

		config << o->print(OptionBase::PRINT_STORE);
	}
	config << std::flush;
} // }}}
// }}}

/// Use short option if it wasn't used by any other option yet.
static char maybe_short(char opt) { // {{{
	for (auto o: OptionBase::all_options) {
		if (o->shortopt == opt)
			return 0;
	}
	return opt;
} // }}}

void init(char **argv, std::string const &help, std::string const &version, std::string const &contact, std::string const &packagename) { // {{{
	/**Initialize the module.
	This function must be called before any other in this module (except
	option definitions and module_init(), which must be called before this
	function).
	Configuration is read from the commandline, and from the configuration
	files named <packagename>.ini in any of the configuration directories,
	or specified with --configfile.  A configuration file must contain
	name=value pairs.  The configuration that is used can be saved using
	--saveconfig, which can optionally have the filename to save to as a
	parameter.
	@param config: Configuration dict. Deprecated. Keep set to None.
	@param packagename: The name of the program.  This is used as a default
		for all other functions.  It has a default of the basename of
		the program.
	@param system: If true, system paths will be used for writing and user
		paths will be ignored for reading.
	@param game: If true, game system directories will be used (/usr/games,
		/usr/share/games, etc.) instead of regular system directories.
	@return vector of non-option arguments.
	*/
	assert(initialized != INITIALIZED);
	initialized = INITIALIZING;

	_info.help = help;
	_info.version = version;
	_info.contact = contact;

	// Set up computed variables. {{{

	auto getfirst = [](char const *a, char const *b) -> std::string { return a ? a : b; };

	// XDG_CONFIG_DIRS
	XDG_CONFIG_DIRS.push_back(XDG_CONFIG_HOME);
	std::string dirs = getfirst(std::getenv("XDG_CONFIG_DIRS"), "/etc/xdg");
	for (auto p: std::views::split(dirs, ":"))
		XDG_CONFIG_DIRS.push_back(std::string_view(p));

	// XDG_DATA_DIRS
	XDG_DATA_DIRS.push_back(XDG_DATA_HOME);
	dirs = getfirst(std::getenv("XDG_DATA_DIRS"), "/usr/local/share:/usr/share");
	for (auto p: std::views::split(dirs, ":"))
		XDG_DATA_DIRS.push_back(std::string_view(p));

	std::filesystem::path mypath(argv[0]);
	if (packagename.empty()) {
		char const *p = std::getenv("PACKAGE_NAME");
		if (p)
			pname = p;
		else
			pname = mypath.stem();
	}
	else
		pname = packagename;

	// Compute base directory (location of executable).
	_base = std::filesystem::absolute(mypath.parent_path());
	// }}}

	// Define default options. {{{
	// If these default options are passed by the user, this will raise an exception.
	// While initializing, options are stored in the front, so put them in reverse order.
	BoolOption *system_option = nullptr;
	if (is_system < 0)
		system_option = new BoolOption("system", "Use only system paths");
	BoolOption saveconfig_option("saveconfig", "Save active commandline configuration");
	StringOption configfile_option("configfile", "Use this file for loading and/or saving configuration", {}, "commandline.ini");
	BoolOption version_option("version", "Show version information", maybe_short('v'));
	BoolOption help_option("help", "Show this help text", maybe_short('h'));
	// }}}

	// Parse commandline. {{{
	bool have_error = false;
	int a = 0;
	while (argv[a]) {
		std::string arg = argv[a];
		if (arg == "--") { // {{{ All remaining options are non-option arguments. Add them to return vector and return.
			while (argv[a]) {
				arguments.push_back(argv[a]);
				++a;
			}
			return;
		} // }}}
		if (arg.empty() || arg[0] != '-' || arg.size() == 1) { // {{{ Non-option argument.
			arguments.push_back(arg);
			++a;
			continue;
		} // }}}
		if (arg[1] == '-') { // {{{ Long option.
			std::string name, value;
			bool have_value;
			auto sep = std::find(arg.begin() + 2, arg.end(), '=');
			if (sep == arg.end()) {
				// No = sign in option.
				have_value = false;
				name = arg.substr(2);
			}
			else {
				// = sign in option.
				have_value = true;
				name = arg.substr(2, sep - (arg.begin() + 2));
				value = arg.substr(sep - arg.begin() + 1);
			}

			std::list <OptionBase *>::iterator option;
			for (option = OptionBase::all_options.begin(); option != OptionBase::all_options.end(); ++option) {
				if ((*option)->name == name)
					break;
			}
			if (option == OptionBase::all_options.end()) {
				std::cerr << "Unknown option " << arg << std::endl;
				++a;
				have_error = true;
				continue;
			}
			(*option)->is_default = false;
			if ((*option)->can_have_parameter) {
				if (have_value) {
					(*option)->parse(value);	// Handle "passed with parameter" event.
				}
				else {
					if ((*option)->must_have_parameter) {
						// Use next parameter as argument.
						++a;
						if (!argv[a]) {
							std::cerr << "Option " << arg << " requires a parameter" << std::endl;
							have_error = true;
							continue;
						}
						(*option)->parse(argv[a]);
					}
					else {
						(*option)->noparse();	// Handle "passed with no parameter" event.
					}
				}
			}
			else {
				// No parameter.
				if (have_value) {
					++a;
					std::cerr << "Option --" << name << " does not take a parameter" << std::endl;
					have_error = true;
					continue;
				}
				(*option)->noparse();	// Handle "passed with no parameter" event.
			}
		} // }}}
		else { // {{{ Short option(s).
			size_t p = 1;
			while (p < arg.size()) {
				std::list <OptionBase *>::iterator option;
				for (option = OptionBase::all_options.begin(); option != OptionBase::all_options.end(); ++option) {
					if ((*option)->shortopt == arg[p])
						break;
				}
				if (option == OptionBase::all_options.end()) {
					std::cerr << "Unknown option -" << arg[p] << std::endl;
					have_error = true;
					++p;
					continue;
				}
				(*option)->is_default = false;
				if ((*option)->can_have_parameter) {
					if (p + 1 < arg.size()) {
						(*option)->parse(arg.substr(p + 1));	// Handle "passed with parameter" event.
						// The rest of the option is used as parameter, so abort parsing short options.
						break;
					}
					else {
						if ((*option)->must_have_parameter) {
							// Use next parameter as argument.
							++a;
							if (!argv[a]) {
								std::cerr << "Option -" << arg[p] << " requires a parameter" << std::endl;
								have_error = true;
								break;
							}
							(*option)->parse(argv[a]);
						}
						else {
							(*option)->noparse();	// Handle "passed with no parameter" event.
						}
					}
				}
				else {
					// No parameter.
					(*option)->noparse();	// Handle "passed with no parameter" event.
				}
				++p;
			}
		} // }}}
		++a;
	} // }}}

	// Handle default options. {{{
	if (have_error || help_option.value) {
		help_text();
		exit(have_error);
	}

	if (version_option.value) {
		version_text();
		exit(0);
	}

	if (system_option)
		is_system = system_option->value;

	if (saveconfig_option.value) {
		save_config(configfile_option.value);
	}
	else {
		// Load configuration from config file(s).
		load_config(configfile_option.value);
	}
	// }}}

	std::atexit(clean_temps);

	//if (XDG_RUNTIME_DIR.empty())
	//	XDG_RUNTIME_DIR = write_temp_dir();

	while (_atinit.size() > 0) {
		auto r = _atinit.begin();
		r->cb(r->data);
		_atinit.pop_front();
	}

	initialized = INITIALIZED;
}
// }}}

void atinit(generic_cb target, void *data) { // {{{
	/// Register a function at init.
	assert(initialized != INITIALIZED);
	_atinit.push_back({target, data});
} // }}}

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
/// XDG runtime directory.  Note that XDG does not specify a default for this.  This library uses /run as the default for system services.
std::filesystem::path XDG_RUNTIME_DIR
	= []() -> std::filesystem::path {
		char const *envname = std::getenv("XDG_RUNTIME_DIR");
		if (envname)
			return envname;
		return "";
	}();

std::filesystem::path write_runtime_name(std::string const &name, bool create, std::string const &packagename, bool dir) { // {{{
	/**Open a runtime file for writing.  The file is not truncated if it exists.
	@param name: Name of the runtime file.
	@param create: Create directories.
	@param packagename: Override the packagename.
	@param dir: Return directory name if true, filename otherwise.
	@return The name of the file or directory.
	*/
	return write_name(XDG_RUNTIME_DIR, "/run", ".txt", name, create, packagename, dir);
} // }}}
std::ofstream write_runtime_file(std::string const &name, std::string const &packagename) { // {{{
	std::filesystem::path filename = write_runtime_name(name, true, packagename, false);
	return std::ofstream(filename, write_mode);
} // }}}
std::filesystem::path write_runtime_dir(std::string const &name, bool create, std::string const &packagename) { // {{{
	return write_runtime_name(name, create, packagename, true);
} // }}}

std::list <std::filesystem::path> read_runtime_names(std::string const &name, std::string const &packagename, bool dir, bool multiple) { // {{{
	/**Open a runtime file for reading.  The paramers should be identical to what was used to create the file with write_runtime().
	@param name: Name of the runtime file.
	@param dir: Return a directory name if true, a file or filename if false (the default).
	@param opened: Open the file if true (the default), report the name if false.
	@param packagename: Override the packagename.
	@return The opened file, or the name of the file or directory.
	*/
	return read_names(XDG_RUNTIME_DIR, {"/run"}, {}, ".txt", name, packagename, dir, multiple);
} // }}}
std::ifstream read_runtime_file(std::string const &name, std::string const &packagename) { // {{{
	auto names = read_runtime_names(name, packagename, false, false);
	if (names.empty())
		return std::ifstream();
	return std::ifstream(names.front(), read_mode);
} // }}}
std::filesystem::path read_runtime_dir(std::string const &name, std::string const &packagename) { // {{{
	return read_runtime_names(name, packagename, true, false).front();
} // }}}

void remove_runtime_file(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a runtime file.  Use the same parameters as were used to create it with write_runtime_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove(read_runtime_names(name, packagename, false, false).front());
} // }}}
void remove_runtime_dir(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a runtime file.  Use the same parameters as were used to create it with write_runtime_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove_all(read_runtime_names(name, packagename, true, false).front());
} // }}}
// }}}

// Data files. {{{
/// XDG data directory.
std::filesystem::path XDG_DATA_HOME
	= []() -> std::filesystem::path {
		char const *envname = std::getenv("XDG_DATA_HOME");
		if (envname)
			return envname;
		return HOME / ".local" / "share";
	}();
/// XDG data directory search path.
std::list <std::filesystem::path> XDG_DATA_DIRS;

std::filesystem::path write_data_name(std::string const &name, bool create, std::string const &packagename, bool dir) { // {{{
	/**Open a data file for writing.  The file is not truncated if it exists.
	@param name: Name of the data file.
	@param create: Create directories.
	@param packagename: Override the packagename.
	@param dir: Return directory name if true, filename otherwise.
	@return The name of the file or directory.
	*/
	return write_name(XDG_DATA_HOME, is_game ? "/var/games" : "/var/lib", ".dat", name, create, packagename, dir);
} // }}}
std::ofstream write_data_file(std::string const &name, std::string const &packagename) { // {{{
	std::filesystem::path filename = write_data_name(name, true, packagename, false);
	return std::ofstream(filename, write_mode);
} // }}}
std::filesystem::path write_data_dir(std::string const &name, bool create, std::string const &packagename) { // {{{
	return write_data_name(name, create, packagename, true);
} // }}}

std::list <std::filesystem::path> read_data_names(std::string const &name, std::string const &packagename, bool dir, bool multiple) { // {{{
	/**Open a data file for reading.  The paramers should be identical to what was used to create the file with write_data().
	@param name: Name of the data file.
	@param dir: Return a directory name if true, a file or filename if false (the default).
	@param opened: Open the file if true (the default), report the name if false.
	@param packagename: Override the packagename.
	@return The opened file, or the name of the file or directory.
	*/
	return read_names(XDG_DATA_HOME, is_game ?
			std::list <std::filesystem::path> {"/var/local/games", "/var/games", "/usr/local/lib/games", "/usr/lib/games", "/usr/local/share/games", "/usr/share/games", "/var/local/lib", "/var/lib", "/usr/local/lib", "/usr/lib", "/usr/local/share", "/usr/share"} :
			std::list <std::filesystem::path> {"/var/local/lib", "/var/lib", "/usr/local/lib", "/usr/lib", "/usr/local/share", "/usr/share"},
		XDG_DATA_DIRS, ".cfg", name, packagename, dir, multiple);
} // }}}
std::ifstream read_data_file(std::string const &name, std::string const &packagename) { // {{{
	auto names = read_data_names(name, packagename, false, false);
	if (names.empty())
		return std::ifstream();
	return std::ifstream(names.front(), read_mode);
} // }}}
std::filesystem::path read_data_dir(std::string const &name, std::string const &packagename) { // {{{
	return read_data_names(name, packagename, true, false).front();
} // }}}

void remove_data_file(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a data file.  Use the same parameters as were used to create it with write_data_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove(read_data_names(name, packagename, false, false).front());
} // }}}
void remove_data_dir(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a data file.  Use the same parameters as were used to create it with write_data_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove_all(read_data_names(name, packagename, true, false).front());
} // }}}
// }}}

// Cache files. {{{
/// XDG cache directory.
std::filesystem::path XDG_CACHE_HOME
	= []() -> std::filesystem::path {
		char const *envname = std::getenv("XDG_CACHE_HOME");
		if (envname)
			return envname;
		return HOME / ".cache";
	}();

std::filesystem::path write_cache_name(std::string const &name, bool create, std::string const &packagename, bool dir) { // {{{
	/**Open a cache file for writing.  The file is not truncated if it exists.
	@param name: Name of the cache file.
	@param create: Create directories.
	@param packagename: Override the packagename.
	@param dir: Return directory name if true, filename otherwise.
	@return The name of the file or directory.
	*/
	return write_name(XDG_CACHE_HOME, "/var/cache", ".txt", name, create, packagename, dir);
} // }}}
std::ofstream write_cache_file(std::string const &name, std::string const &packagename) { // {{{
	std::filesystem::path filename = write_cache_name(name, true, packagename, false);
	return std::ofstream(filename, write_mode);
} // }}}
std::filesystem::path write_cache_dir(std::string const &name, bool create, std::string const &packagename) { // {{{
	return write_cache_name(name, create, packagename, true);
} // }}}

std::list <std::filesystem::path> read_cache_names(std::string const &name, std::string const &packagename, bool dir, bool multiple) { // {{{
	/**Open a cache file for reading.  The paramers should be identical to what was used to create the file with write_cache().
	@param name: Name of the cache file.
	@param dir: Return a directory name if true, a file or filename if false (the default).
	@param opened: Open the file if true (the default), report the name if false.
	@param packagename: Override the packagename.
	@return The opened file, or the name of the file or directory.
	*/
	return read_names(XDG_CACHE_HOME, {"/var/cache"}, {}, ".txt", name, packagename, dir, multiple);
} // }}}
std::ifstream read_cache_file(std::string const &name, std::string const &packagename) { // {{{
	auto names = read_cache_names(name, packagename, false, false);
	if (names.empty())
		return std::ifstream();
	return std::ifstream(names.front(), read_mode);
} // }}}
std::filesystem::path read_cache_dir(std::string const &name, std::string const &packagename) { // {{{
	return read_cache_names(name, packagename, true, false).front();
} // }}}

void remove_cache_file(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a cache file.  Use the same parameters as were used to create it with write_cache_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove(read_cache_names(name, packagename, false, false).front());
} // }}}
void remove_cache_dir(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a cache file.  Use the same parameters as were used to create it with write_cache_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove_all(read_cache_names(name, packagename, true, false).front());
} // }}}
// }}}

// Spool files. {{{
std::filesystem::path write_spool_name(std::string const &name, bool create, std::string const &packagename, bool dir) { // {{{
	/**Open a spool file for writing.  The file is not truncated if it exists.
	@param name: Name of the spool file.
	@param create: Create directories.
	@param packagename: Override the packagename.
	@param dir: Return directory name if true, filename otherwise.
	@return The name of the file or directory.
	*/
	return write_name(XDG_CACHE_HOME, "/var/spool", ".txt", name, create, packagename, dir);
} // }}}
std::ofstream write_spool_file(std::string const &name, std::string const &packagename) { // {{{
	std::filesystem::path filename = write_spool_name(name, true, packagename, false);
	return std::ofstream(filename, write_mode);
} // }}}
std::filesystem::path write_spool_dir(std::string const &name, bool create, std::string const &packagename) { // {{{
	return write_spool_name(name, create, packagename, true);
} // }}}

std::list <std::filesystem::path> read_spool_names(std::string const &name, std::string const &packagename, bool dir, bool multiple) { // {{{
	/**Open a spool file for reading.  The paramers should be identical to what was used to create the file with write_spool().
	@param name: Name of the spool file.
	@param dir: Return a directory name if true, a file or filename if false (the default).
	@param opened: Open the file if true (the default), report the name if false.
	@param packagename: Override the packagename.
	@return The opened file, or the name of the file or directory.
	*/
	return read_names(XDG_CACHE_HOME, {"/var/spool"}, {}, ".txt", name, packagename, dir, multiple);
} // }}}
std::ifstream read_spool_file(std::string const &name, std::string const &packagename) { // {{{
	auto names = read_spool_names(name, packagename, false, false);
	if (names.empty())
		return std::ifstream();
	return std::ifstream(names.front(), read_mode);
} // }}}
std::filesystem::path read_spool_dir(std::string const &name, std::string const &packagename) { // {{{
	return read_spool_names(name, packagename, true, false).front();
} // }}}

void remove_spool_file(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a spool file.  Use the same parameters as were used to create it with write_spool_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove(read_spool_names(name, packagename, false, false).front());
} // }}}
void remove_spool_dir(std::string const &name, std::string const &packagename) { // {{{
	/**Remove a spool file.  Use the same parameters as were used to create it with write_spool_file().
	@param name: The file to remove.
	@param dir: If true, remove a directory.  If false (the default), remove a file.
	@param packagename: Override the packagename.
	@return None.
	*/
	assert(initialized != UNINITIALIZED);
	std::filesystem::remove_all(read_spool_names(name, packagename, true, false).front());
} // }}}
// }}}

// Log files. {{{
std::filesystem::path write_log_name(std::string const &name, bool create, std::string const &packagename, bool dir) { // {{{
	/**Open a log file for writing.  The file is not truncated if it exists.
	@param name: Name of the log file.
	@param create: Create directories.
	@param packagename: Override the packagename.
	@param dir: Return directory name if true, filename otherwise.
	@return The name of the file or directory.
	*/
	return write_name(std::filesystem::path(), "/var/log", ".txt", name, create, packagename, dir);
} // }}}
std::ofstream write_log_file(std::string const &name, std::string const &packagename) { // {{{
	assert(is_system);
	std::filesystem::path filename = write_log_name(name, true, packagename, false);
	return std::ofstream(filename, write_mode);
} // }}}
std::filesystem::path write_log_dir(std::string const &name, bool create, std::string const &packagename) { // {{{
	assert(is_system);
	return write_log_name(name, create, packagename, true);
} // }}}
// }}}

// Temporary files. {{{
static size_t temp_file_counter = 0;
static std::filesystem::path get_temp_name(std::string const &name) { // {{{
	if (_temp_file_dir == std::filesystem::path()) {
		assert(temp_file_counter == 0);
		// Don't allow packagename override, because this is not only for current call.
		std::string fullname = std::filesystem::temp_directory_path() / pname;
		size_t size = fullname.size();
		char tpl[size + 8];
		memcpy(tpl, fullname.data(), size);
		tpl[size] = '-';
		for (int i = 0; i < 6; ++i)
			tpl[size + 1 +  i] = 'X';
		tpl[size + 7] = '\0';
		assert(mkdtemp(tpl));
		_temp_file_dir = tpl;
	}
	std::ostringstream n;
	n << std::hex << std::setw(4) << std::setfill('0') << temp_file_counter++ << "-" << name;
	return _temp_file_dir / n.str();
} // }}}

std::ofstream write_temp_file(std::string const &name) { // {{{
	return std::ofstream(get_temp_name(name), std::ios::in | std::ios::out | std::ios::trunc | std::ios::noreplace | std::ios::binary);
} // }}}

std::filesystem::path write_temp_dir(std::string const &name) { // {{{
	std::filesystem::path filename = get_temp_name(name);
	std::filesystem::create_directory(filename);
	return filename;
} // }}}
// }}}

}
