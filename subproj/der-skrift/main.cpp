#ifndef DER_SKRIPT
#include "arlib.h"

// TODO: https://blog.panicsoftware.com/your-first-coroutine/

static bool isualpha(uint8_t c) { return isalpha(c) || c=='_'; }
static bool isualnum(uint8_t c) { return isalnum(c) || c=='_'; }

static bool firstword(cstring a, const char * b)
{
	return a.startswith(b) && !isualnum(a[strlen(b)]);
}

static string recompile(cstring text_raw)
{
	if (!text_raw) return "";
	
	// a function is defined as
	// - (opt) "template" "<" *** ">"
	// - identifier
	// - (opt) "<" *** ">"
	// - identifier
	// - "(" *** ")" "{" *** "}"
	// where whitespace can be between any token pair, and *** means any set of tokens as long as brackets match
	// brackets are defined as () [] {}, or <> if directly in a <> (but template<1+(2>3)> is considered matching)
	// a function cannot be inside any set of brackets
	// attributes are not supported
	
	// a class is defined as
	// - (opt) "template" "<" *** ">"
	// - "class", "struct" or "enum"
	// - identifier
	// - (opt) ":" (opt)identifier identifier (opt){ "<" *** ">" }
	// - (if above; opt; multiple) "," (opt)identifier identifier (opt){ "<" *** ">" }
	// - "{" *** "}" ";"
	
	// start with halfheartedly tokenizing the file (match identifiers, comments and strings, but ignore multi-byte operators like +=)
	
	string text = text_raw;
	array<string> tokens;
	while (text)
	{
#define MATCH(re) if (auto m = REGEX(re).match(text)) { tokens.append(m[0].str()); text = text.substr(m[0].str().length(), ~0); continue; }
		MATCH(R"(#.*(\\\n.*)*\n[\n\t ]*)");
		MATCH(R"(//.*\n[\n\t ]*)");
		MATCH(R"(/\*[^]+?\*/[\n\t ]*)");
		MATCH(R"(R"([^()\\]*)\(.*\)\1"[\n\t ]*)");
		MATCH(R"("(\\.|[^\\"\n])*"[\n\t ]*)");
		MATCH(R"('(\\.|[^\\'\n])*'[\n\t ]*)");
		MATCH(R"([a-zA-Z_0-9]+[\n\t ]*)");
		MATCH(R"([^][\n\t ]*)");
	}
	tokens.append("");
	
	if (tokens[0].startswith("#!")) tokens.remove(0);
	
	// flatten all token sequences that are in parens, brackets or braces
	size_t at = 0;
	while (at < tokens.size())
	{
		if (strchr("([{", tokens[at][0]))
		{
			size_t start = at;
			int depth = 1;
			at++;
			while (at < tokens.size() && depth)
			{
				if (strchr("([{", tokens[at][0])) depth++;
				if (strchr(")]}", tokens[at][0])) depth--;
				tokens[start] += tokens.pop(at);
			}
		}
		else at++;
	}
	
	// flatten <>, but only if it does not contain a semicolon; also merge with the previous token, if it's an identifier
	at = 0;
	while (at < tokens.size())
	{
		if (tokens[at][0] == '<')
		{
			size_t start = at;
			int depth = 1;
			at++;
			while (at < tokens.size() && depth)
			{
				if (tokens[at][0] == ';') break;
				if (tokens[at][0] == '<') depth++;
				if (tokens[at][0] == '>') depth--;
				tokens[start] += tokens.pop(at);
			}
			if (depth == 0 && start > 0 && isualpha(tokens[start-1][0]))
			{
				tokens[start-1] += tokens.pop(start);
			}
		}
		else at++;
	}
	
	// flatten ::
	at = 0;
	while (at < tokens.size())
	{
		if (tokens[at] == ":" && tokens.get_or(at+1, "")==":" &&
		    isualnum(tokens.get_or(at-1, "")[0]) && isualnum(tokens.get_or(at+2, "")[0]))
		{
			tokens[at-1] += "::"+tokens[at+2];
			tokens.remove(at+2);
			tokens.remove(at+1);
			tokens.remove(at);
		}
		else at++;
	}
	
	// with these changes, functions and classes become these "tokens"
	// (opt) "template< *** >"
	// identifier (opt)"< *** >"
	// identifier
	// "( *** )"
	// "{ *** }"
	
	// (opt) "template" "<" *** ">"
	// "class", "struct" or "enum"
	// identifier
	// (opt) ":"
	//   (opt)identifier
	//   identifier (opt)"< *** >"
	//   (opt, multiple)
	//     ","
	//     (opt)identifier
	//     identifier (opt)"< *** >"
	// "{ *** }"
	// ";"
	
	// (I'll ignore typedefs for now)
	
	bool has_main = false;
	
	array<bool> is_early;
	is_early.resize(tokens.size());
	
	at = 0;
	while (at < tokens.size())
	{
		if (firstword(tokens[at], "#include"))
			is_early[at] = true;
		// function
		if (isualnum(tokens[at+0][0]) && // return type
		    isualnum(tokens[at+1][0]) && // name
		    tokens[at+2][0] == '(' &&    // arguments
		    tokens[at+3][0] == '{')      // contents
		{
			if (firstword(tokens[at+1], "main"))
				has_main = true;
			is_early[at+0] = true;
			is_early[at+1] = true;
			is_early[at+2] = true;
			is_early[at+3] = true;
			if (at > 0 && firstword(tokens[at-1], "template"))
				is_early[at-1] = true;
			at += 4;
		}
		else if (firstword(tokens[at], "class") || firstword(tokens[at], "struct") ||
		         firstword(tokens[at], "union") || firstword(tokens[at], "enum") ||
		         firstword(tokens[at], "typedef"))
		{
			// these are keywords; if they're not used correctly, they're syntax errors, so false positives here are fine
			if (at > 0 && firstword(tokens[at-1], "template"))
				is_early[at-1] = true;
			while (at < tokens.size() && tokens[at][0] != ';')
			{
				is_early[at] = true;
				at++;
			}
			if (at == tokens.size()) break; // should be impossible
			is_early[at] = true;
			at++;
		}
		else
		{
			at++;
		}
	}
	
	string early;
	string late;
	
	for (size_t n : range(tokens.size()))
	{
		if (has_main || is_early[n]) early += tokens[n];
		else late += tokens[n];
	}
	
	if (!has_main)
	{
		early += "int main(int argc, char** argv) {";
		late += "}\n";
	}
	
	return "#include \"" STR(BUILD_DIR) "/arlib.h\"\n"+early+late;
}

int main(int argc, char** argv, char** envp)
{
	arlib_init_manual_args(&argc, &argv);
	
	if (argc < 1)
	{
		puts("invalid usage");
		exit(1);
	}
	
	enum mode_t {
		m_run,
		m_run_dbg,
		m_run_vg,
		m_compile,
		m_compile_windows,
	};
	mode_t mode;
	
	string in_script = argv[2];
	int first_arg = 3;
	
	if (argv[1] == (string)"-c")
		mode = m_compile;
	else if (argv[1] == (string)"-cw")
		mode = m_compile_windows;
	else if (argv[1] == (string)"-d")
		mode = m_run_dbg;
	else if (argv[1] == (string)"-v" || argv[1] == (string)"-vg")
		mode = m_run_vg;
	else
	{
		mode = m_run;
		first_arg = 2;
		in_script = argv[1];
	}
	
	bool is_run = (mode < m_compile);
	
	string in_script_dir = file::dirname(file::realpath(in_script));
	string tmp_fn_base = "obj/ds-"+file::realpath(in_script).replace("+","++").replace("/","+");
	string tmp_fn_cpp_rel = tmp_fn_base+".cpp";
	string tmp_fn_cpp_abs = STR(BUILD_DIR) "/" + tmp_fn_cpp_rel;
	string tmp_fn_exe_abs;
	if (is_run)
		tmp_fn_exe_abs = STR(BUILD_DIR) "/" + tmp_fn_base + ".elf";
	else
		tmp_fn_exe_abs = file::realpath(file::change_ext(in_script, ""));
	
	string cpp = recompile(file::readallt(in_script).replace("\r\n","\n"));
	if (!cpp)
	{
		puts("error: bad input.");
		exit(1);
	}
	string prev_cpp = file::readallt(tmp_fn_cpp_abs);
	
	if (cpp != prev_cpp || access(tmp_fn_exe_abs, X_OK) != 0 || mode != m_run)
	{
		if (mode == m_run)
			file::unlink(tmp_fn_exe_abs);
		file::writeall(tmp_fn_cpp_abs, cpp);
		
		process p(runloop::global());
		p.set_stdout(process::output::create_stderr());
		p.set_stderr(process::output::create_stderr());
		p.onexit([](int status){ runloop::global()->exit(); });
#define MAKE_SHARED_ARGS "-C", STR(BUILD_DIR), "DS_IN="+tmp_fn_cpp_rel, "DS_OUT="+tmp_fn_exe_abs, "-kj8", \
			"DS_SRCDIR="+in_script_dir
		if (is_run)
			p.launch("make", MAKE_SHARED_ARGS);
		if (mode == m_compile)
			p.launch("make", "OPT=1", MAKE_SHARED_ARGS);
		if (mode == m_compile_windows)
			p.launch("wine", "mingw32-make", "OPT=1", MAKE_SHARED_ARGS);
#undef MAKE_SHARED_ARGS
		runloop::global()->enter();
		if (!is_run || p.status() != 0) exit(p.status());
	}
	
	array<const char *> args;
	const char * the_program;
	
	if (mode == m_run_dbg)
	{
		args.append("gdb");
		args.append("--args");
		args.append(tmp_fn_exe_abs);
		the_program = "gdb";
	}
	else if (mode == m_run_vg)
	{
		args.append("valgrind");
		args.append(tmp_fn_exe_abs);
		the_program = "valgrind";
	}
	else
	{
		args.append(argv[1]);
		the_program = tmp_fn_exe_abs;
	}
	
	for (int i=first_arg;argv[i];i++) args.append(argv[i]);
	args.append(NULL);
	execvp(the_program, (char**)args.ptr());
	exit(127);
}
#endif
