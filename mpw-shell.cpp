
#include <vector>
#include <string>
#include <unordered_map>

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <cerrno>

#include "mpw-shell.h"
#include "fdset.h"

#include "phase1.h"
#include "phase2.h"
#include "command.h"

#include "mapped_file.h"
#include "error.h"

//#include <histedit.h>
#include <editline/readline.h>

#include <sys/types.h>
#include <pwd.h>
#include <sysexits.h>

//#include <uuid/uuid.h>

std::string root() {

	static std::string root;

	if (root.empty()) {
		const char *cp = getenv("HOME");
		if (!cp ||  !*cp) {
			auto pw = getpwuid(getuid());
			if (!pw) {
				fprintf(stderr,"Unable to determine home directory\n.");
				exit(EX_NOUSER);
			}
			cp = pw->pw_dir;
		}
		root = cp;
		if (root.back() != '/') root.push_back('/');
		root += "mpw/";
	}
	return root;
}
// should set {MPW}, {MPWVersion}, then execute {MPW}StartUp
void init(Environment &env) {

	env.set("mpw", root());
	env.set("status", std::string("0"));
	env.set("exit", std::string("1")); // terminate script on error.
	env.set("echo", std::string("1"));
}

int read_file(phase1 &p, const std::string &file) {
	const mapped_file mf(file, mapped_file::readonly);

	p.process(mf.begin(), mf.end(), true);

	return 0;
}

int read_fd(phase1 &p, int fd) {

	unsigned char buffer[2048];
	ssize_t size;

	for (;;) {
		size = read(fd, buffer, sizeof(buffer));
		if (size == 0) break;
		if (size < 0) {
			if (errno == EINTR) continue;
			perror("read: ");
			return -1;
		}
		try {
			p.process(buffer, buffer + size);
		} catch(std::exception &ex) {
			fprintf(stderr, "%s\n", ex.what());
			p.reset();
		}
	}

	try {
		p.finish();
	} catch(std::exception &ex) {
		fprintf(stderr, "%s\n", ex.what());
		p.reset();
	}

	return 0;
}

int interactive(phase1 &p) {

	for(;;) {
		char *cp = readline("# ");
		if (!cp) break;

		std::string s(cp);
		free(cp);

		if (s.empty()) continue;

		// don't add if same as previous entry.
		HIST_ENTRY *he = current_history();
		if (he == nullptr || s != he->line)
				add_history(s.c_str());

		s.push_back('\n');
		try {
			p.process(s);

		} catch(std::exception &ex) {
			fprintf(stderr, "%s\n", ex.what());
			p.reset();
		}

	}

	try {
		p.finish();
	} catch(std::exception &ex) {
		fprintf(stderr, "%s\n", ex.what());
		p.reset();
	}

	fprintf(stdout, "\n");
	return 0;
}

void help() {

}
void define(Environment &env, const std::string &s) {

	auto pos = s.find('=');
	if (pos == s.npos) env.set(s, "1");
	else {
		std::string k = s.substr(0, pos);
		std::string v = s.substr(pos+1);
		env.set(k, v);
	}

}

int main(int argc, char **argv) {
	
	Environment e;
	init(e);

	const char *cflag = nullptr;

	int c;
	while ((c = getopt(argc, argv, "c:D:v:h")) != -1) {
		switch (c) {
			case 'c':
				// -c command
				cflag = optarg;
				break;
			case 'D':
				// -Dname or -Dname=value
				define(e, optarg);
				break;
			case 'v':
				// -v verbose
				e.set("echo", "1");
				break;
			case 'h':
				help();
				exit(0);

			default:
				help();
				exit(EX_USAGE);
		}
	}







	phase1 p1;
	phase2 p2;

	p1 >>= [&p2](std::string &&s) {
		if (s.empty()) p2.finish();
		else p2(std::move(s));
	};

	p2 >>= [&e](command_ptr_vector &&v) {

		for (auto iter = v.begin(); iter != v.end(); ++iter) {
			auto &ptr = *iter;
			fdmask fds;
			try {
				ptr->execute(e, fds);
			} catch (execution_of_input_terminated &ex) {
				if (!(ptr->terminal() && ++iter == v.end())) {
					fprintf(stderr, "%s\n", ex.what());
				}
				return;
			}
		}
	};

	if (!cflag) fprintf(stdout, "MPW Shell 0.0\n");
	e.startup(true);
	read_file(p1, "/Users/kelvin/mpw/Startup");
	//p2.finish();
	e.startup(false);

	if (cflag) {
		std::string s(cflag);
		s.push_back('\n');
		p1.process(s.data(), s.data() + s.length(), true);
		p2.finish();
		exit(e.status());
	}

	if (isatty(STDIN_FILENO))
		interactive(p1);
	else 
		read_fd(p1, STDIN_FILENO);
	p2.finish();

	exit(e.status());
}
