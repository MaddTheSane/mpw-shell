
#include "mpw-shell.h"

#include "fdset.h"
#include "value.h"

#include <string>
#include <vector>
#include <algorithm>

#include <cstdio>
#include <cctype>

namespace {


	std::string &lowercase(std::string &s) {
		std::transform(s.begin(), s.end(), s.begin(), [](char c){ return std::tolower(c); });
		return s;
	}

	// doesn't handle flag arguments but builtins don't have arguments.

	template<class FX>
	std::vector<std::string> getopt(const std::vector<std::string> &argv, FX fx) {
		
		std::vector<std::string> out;
		out.reserve(argv.size());

		std::copy_if(argv.begin()+1, argv.end(), std::back_inserter(out), [&fx](const std::string &s){

			if (s.empty()) return false; // ?
			if (s.front() == '-') {
				std::for_each(s.begin() + 1, s.end(), fx);
				return false;
			}
			return true;
		});

		return out;
	}




	/*
	 * the fdopen() will assume ownership of the fd and close it.
	 * this is not desirable.
	 */

	// linux uses size_t.  *BSD uses int.
	int readfn(void *cookie, char *buffer, int size) {
		return ::read((int)(std::ptrdiff_t)cookie, buffer, size);
	}

	int readfn(void *cookie, char *buffer, size_t size) {
		return ::read((int)(std::ptrdiff_t)cookie, buffer, size);
	}

	int writefn(void *cookie, const char *buffer, int size) {
		return ::write((int)(std::ptrdiff_t)cookie, buffer, size);
	}

	int writefn(void *cookie, const char *buffer, size_t size) {
		return ::write((int)(std::ptrdiff_t)cookie, buffer, size);
	}

	FILE *file_stream(int index, int fd) {
		if (fd < 0) {
			switch (index) {
				case 0: return stdin;
				case 1: return stdout;
				case 2: return stderr;
				default:
				return stderr;
			}
		}
		// will not close.

		#ifdef __linux__
		/* Linux */
		cookie_io_functions_t io = { readfn, writefn, nullptr, nullptr };
		return fopencookie((void *)(std::ptrdiff_t)fd, "w+", io);
		#else
		/* *BSD */
		return funopen((const void *)(std::ptrdiff_t)fd, readfn, writefn, nullptr, nullptr);
		#endif

	}


	class io_helper {

	public:
		FILE *in;
		FILE *out;
		FILE *err;

		io_helper(const fdmask &fds) {
			in = file_stream(0, fds[0]);
			out = file_stream(1, fds[1]);
			err = file_stream(2, fds[2]);
		}

		~io_helper() {
			#define __(x, target) if (x != target) fclose(x)
			__(in, stdin);
			__(out, stdout);
			__(err, stderr);
			#undef __
		}

		io_helper() = delete;
		io_helper(const io_helper &) = delete;
		io_helper &operator=(const io_helper &) = delete;
	};


}

#undef stdin
#undef stdout
#undef stderr

#define stdin io.in
#define stdout io.out
#define stderr io.err

int builtin_unset(const std::vector<std::string> &tokens, const fdmask &) {
	for (auto iter = tokens.begin() + 1; iter != tokens.end(); ++iter) {

		std::string name = *iter;
		lowercase(name);

		Environment.erase(name);
	}
	// unset [no arg] removes ALL variables
	if (tokens.size() == 1) {
		Environment.clear();
	}
	return 0;
}


int builtin_set(const std::vector<std::string> &tokens, const fdmask &fds) {
	// set var name  -- set
	// set var -- just print the value

	// 3.5 supports -e to also export it.

	io_helper io(fds);


	if (tokens.size() == 1) {

		for (const auto &kv : Environment) {
			std::string name = quote(kv.first);
			std::string value = quote(kv.second);

			fprintf(stdout, "Set %s%s %s\n",
				bool(kv.second) ? "-e " : "", 
				name.c_str(), value.c_str());
		}
		return 0;
	}

	if (tokens.size() == 2) {
		std::string name = tokens[1];
		lowercase(name);
		auto iter = Environment.find(name);
		if 	(iter == Environment.end()) {
			fprintf(stderr, "### Set - No variable definition exists for %s.\n", name.c_str());
			return 2;
		}

		name = quote(name);
		std::string value = quote(iter->second);
		fprintf(stdout, "Set %s%s %s\n", 
			bool(iter->second) ? "-e " : "", 
			name.c_str(), value.c_str());
		return 0;
	}

	bool exported = false;


	if (tokens.size() == 4 && tokens[1] == "-e") {
		exported = true;
	}

	if (tokens.size() > 3 && !exported) {
		fputs("### Set - Too many parameters were specified.\n", stderr);
		fputs("# Usage - set [name [value]]\n", stderr);
		return 1;
	}

	std::string name = tokens[1+exported];
	std::string value = tokens[2+exported];
	lowercase(name);

	Environment[name] = std::move(EnvironmentEntry(std::move(value), exported));
	return 0;
}



static int export_common(bool export_or_unexport, const std::vector<std::string> &tokens, io_helper &io) {

	const char *name = export_or_unexport ? "Export" : "Unexport";

	struct {
		int _r = 0;
		int _s = 0;
	} flags;
	bool error = false;

	std::vector<std::string> argv = getopt(tokens, [&](char c){
		switch(c) {
			case 'r':
			case 'R':
				flags._r = true;
				break;
			case 's':
			case 'S':
				flags._s = true;
				break;
			default:
				fprintf(stderr, "### %s - \"-%c\" is not an option.\n", name, c);
				error = true;
				break;
		}
	});

	if (error) {
		fprintf(stderr, "# Usage - %s [-r | -s | name...]\n", name);
		return 1;
	}

	if (argv.empty()) {
		if (flags._r && flags._s) goto conflict;

		// list of exported vars.
		// -r will generate unexport commands for exported variables.
		// -s will only print the names.


		name = export_or_unexport ? "Export " : "Unexport ";

		for (const auto &kv : Environment) {
			const std::string& vname = kv.first;
			if (kv.second == export_or_unexport)
				fprintf(stdout, "%s%s\n", flags._s ? "" : name, quote(vname).c_str());
		}
		return 0;
	}
	else {
		// mark as exported.

		if (flags._r || flags._s) goto conflict;

		for (std::string s : argv) {
			lowercase(s);
			auto iter = Environment.find(s);
			if (iter != Environment.end()) iter->second = export_or_unexport;
		}	
		return 0;
	}

conflict:
	fprintf(stderr, "### %s - Conflicting options or parameters were specified.\n", name);
	fprintf(stderr, "# Usage - %s [-r | -s | name...]\n", name);
	return 1;
}
int builtin_export(const std::vector<std::string> &tokens, const fdmask &fds) {

	io_helper io(fds);
	return export_common(true, tokens, io);
}

int builtin_unexport(const std::vector<std::string> &tokens, const fdmask &fds) {

	io_helper io(fds);
	return export_common(false, tokens, io);
}



int builtin_echo(const std::vector<std::string> &tokens, const fdmask &fds) {

	io_helper io(fds);

	bool space = false;
	bool n = false;

	for (auto iter = tokens.begin() + 1; iter != tokens.end(); ++iter) {

		const std::string &s = *iter;
		if (s == "-n" || s == "-N") {
			n = true;
			continue;
		}
		if (space) {
			fputs(" ", stdout);
		}
		fputs(s.c_str(), stdout);
		space = true;
	}
	if (!n) fputs("\n", stdout);
	return 0;
}

int builtin_quote(const std::vector<std::string> &tokens, const fdmask &fds) {
	// todo...

	io_helper io(fds);

	bool space = false;
	bool n = false;

	for (auto iter = tokens.begin() + 1; iter != tokens.end(); ++iter) {

		std::string s = *iter;
		if (s == "-n" || s == "-N") {
			n = true;
			continue;
		}
		if (space) {
			fputs(" ", stdout);
		}
		s = quote(std::move(s));
		fputs(s.c_str(), stdout);
		space = true;
	}
	if (!n) fputs("\n", stdout);
	return 0;
}

int builtin_parameters(const std::vector<std::string> &argv, const fdmask &fds) {

	io_helper io(fds);

	int i = 0;
	for (const auto &s : argv) {
		fprintf(stdout, "{%d} %s\n", i++, s.c_str());
	}
	return 0;
}


int builtin_directory(const std::vector<std::string> &tokens, const fdmask &fds) {
	// directory [-q]
	// directory path

	// for relative names, uses {DirectoryPath} (if set) rather than .
	// set DirectoryPath ":,{MPW},{MPW}Projects:"

	io_helper io(fds);

	bool q = false;
	bool error = false;

	std::vector<std::string> argv = getopt(tokens, [&](char c){
		switch(c)
		{
			case 'q':
			case 'Q':
				q = true;
				break;
			default:
				fprintf(stderr, "### Directory - \"-%c\" is not an option.\n", c);
				error = true;
				break;
		}
	});

	if (error) {
		fputs("# Usage - Directory [-q | directory]\n", stderr);
		return 1;
	}

	if (argv.size() > 1) {
		fputs("### Directory - Too many parameters were specified.\n", stderr);
		fputs("# Usage - Directory [-q | directory]\n", stderr);
		return 1;	
	}


	if (argv.size() == 1) {
		//cd
		if (q) {
			fputs("### Directory - Conflicting options or parameters were specified.\n", stderr);
			return 1;
		}

		return 0;
	}
	else {
		// pwd
		return 0;
	}
}

static bool is_assignment(int type) {
	switch(type)
	{
		case '=':
		case '+=':
		case '-=':
			return true;
		default:
			return false;
	}
}

int builtin_evaluate(std::vector<token> &&tokens, const fdmask &fds) {
	// evaluate expression
	// evaluate variable = expression
	// evaluate variable += expression
	// evaluate variable -= expression

	// flags -- -h -o -b -- print in hex, octal, or binary

	// convert the arguments to a stack.


	int output = 'd';

	io_helper io(fds);

	std::reverse(tokens.begin(), tokens.end());

	// remove 'Evaluate'
	tokens.pop_back();

	// check for -h -x -o
	if (tokens.size() >= 2 && tokens.back().type == '-') {

		const token &t = tokens[tokens.size() - 2];
		if (t.type == token::text && t.string.length() == 1) {
			int flag = tolower(t.string[0]);
			switch(flag) {
				case 'o':
				case 'h':
				case 'b':
					output = flag;
					tokens.pop_back();
					tokens.pop_back();
			}
		}

	}

	if (tokens.size() >= 2 && tokens.back().type == token::text)
	{
		int type = tokens[tokens.size() -2].type;

		if (is_assignment(type)) {

			std::string name = tokens.back().string;
			lowercase(name);

			tokens.pop_back();
			tokens.pop_back();

			int32_t i = evaluate_expression("Evaluate", std::move(tokens));

			switch(type) {
				case '=':
					Environment[name] = std::to_string(i);
					break;
				case '+=':
				case '-=':
					{
						value old;
						auto iter = Environment.find(name);
						if (iter != Environment.end()) old = (const std::string &)iter->second;

						switch(type) {
							case '+=':
								i = old.to_number() + i;
								break;
							case '-=':
								i = old.to_number() - i;
								break;
						}

						std::string s = std::to_string(i);
						if (iter == Environment.end())
							Environment.emplace(std::move(name), std::move(s));
						else iter->second = std::move(s);

					}
					break;
			}
			return 0;
		}
	}

	int32_t i = evaluate_expression("Evaluate", std::move(tokens));

	// todo -- format based on -h, -o, or -b flag.
	if (output == 'h') {
		fprintf(stdout, "0x%08x\n", i);
		return 0;
	}
	if (output == 'b') {
		fputc('0', stdout);
		fputc('b', stdout);
		for (int j = 0; j < 32; ++j) {
			fputc(i & 0x80000000 ? '1' : '0', stdout);
			i <<= 1;
		}
		fputc('\n', stdout);
		return 0;

	}
	if (output == 'o') {
		// octal.
		fprintf(stdout, "0%o\n", i);
		return 0;
	}
	fprintf(stdout, "%d\n", i);
	return 0;
}
