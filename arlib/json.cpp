#include "json.h"
#include "stringconv.h"

char jsonparser::nextch()
{
again: ;
	char ret = m_data[m_pos++];
	if (ret >= 33) return ret;
	if (isspace(ret)) goto again;
	if (m_pos >= m_data.length()) m_pos--;
	return '\0';
}

bool jsonparser::skipcomma()
{
	char ch = nextch();
	if (ch == ',' || ch == '\0')
	{
		if ((bool)m_nesting == (ch == '\0')) return false;
		if (m_nesting && m_nesting[m_nesting.size()-1]==true)
		{
			m_want_key = true;
		}
		return true;
	}
	if (ch == ']' || ch == '}')
	{
		m_pos--;
		return true;
	}
	return false;
}

jsonparser::event jsonparser::next()
{
	char ch = nextch();
	if (m_want_key)
	{
		m_want_key = false;
		if (ch == '"') goto parse_key;
		else return { error };
	}
	
	if (ch == '\0')
	{
		if (m_nesting)
		{
			if (!m_unexpected_end)
			{
				m_unexpected_end = true;
				return { error };
			}
			bool map = (m_nesting[m_nesting.size()-1] == true);
			m_nesting.resize(m_nesting.size()-1);
			return { map ? exit_map : exit_list };
		}
		return { finish };
	}
	if (ch == '"')
	{
		bool is_key;
		is_key = false;
		if (false)
		{
		parse_key:
			is_key = true;
		}
		string val;
		while (true)
		{
			char ch = m_data[m_pos++];
			if (ch < 32 && ch != '\t')
			{
				m_pos--;
				return { error };
			}
			if (ch == '\\')
			{
				abort();
				continue;
			}
			if (ch == '"') break;
			val += ch;
		}
		if (is_key)
		{
			if (nextch() != ':') return { error };
			return { map_key, val };
		}
		else
		{
			if (!skipcomma()) return { error };
			return { str, val };
		}
	}
	if (ch == '[')
	{
		m_nesting.append(false);
		return { enter_list };
	}
	if (ch == ']')
	{
		if (!m_nesting || m_nesting[m_nesting.size()-1] != false) return { error };
		m_nesting.resize(m_nesting.size()-1);
		if (!skipcomma()) return { error };
		return { exit_list };
	}
	if (ch == '{')
	{
		m_nesting.append(true);
		m_want_key = true;
		return { enter_map };
	}
	if (ch == '}')
	{
		if (!m_nesting || m_nesting[m_nesting.size()-1] != true) return { error };
		m_nesting.resize(m_nesting.size()-1);
		if (!skipcomma()) return { error };
		return { exit_map };
	}
	if (ch == '-' || isdigit(ch))
	{
		m_pos--;
		size_t start = m_pos;
		if (m_data[m_pos] == '-') m_pos++;
		if (m_data[m_pos] == '0') m_pos++;
		else
		{
			while (isdigit(m_data[m_pos])) m_pos++;
		}
		if (m_data[m_pos] == '.')
		{
			m_pos++;
			if (!isdigit(m_data[m_pos])) return { error };
			while (isdigit(m_data[m_pos])) m_pos++;
		}
		if (m_data[m_pos] == 'e' || m_data[m_pos] == 'E')
		{
			m_pos++;
			if (m_data[m_pos] == '+' || m_data[m_pos] == '-') m_pos++;
			if (!isdigit(m_data[m_pos])) return { error };
			while (isdigit(m_data[m_pos])) m_pos++;
		}
		
		double d;
		if (!fromstring(m_data.csubstr(start, m_pos), d) || !skipcomma()) return { error };
		return { num, d };
	}
	if (ch == 't' && m_data[m_pos++]=='r' && m_data[m_pos++]=='u' && m_data[m_pos++]=='e')
	{
		if (!skipcomma()) return { error };
		return { jtrue };
	}
	if (ch == 'f' && m_data[m_pos++]=='a' && m_data[m_pos++]=='l' && m_data[m_pos++]=='s' && m_data[m_pos++]=='e')
	{
		if (!skipcomma()) return { error };
		return { jfalse };
	}
	if (ch == 'n' && m_data[m_pos++]=='u' && m_data[m_pos++]=='l' && m_data[m_pos++]=='l')
	{
		if (!skipcomma()) return { error };
		return { jnull };
	}
	
	return { error };
}


#include "test.h"
#ifdef ARLIB_TEST
#define e_jfalse jsonparser::jfalse
#define e_jtrue jsonparser::jtrue
#define e_jnull jsonparser::jnull
#define e_str jsonparser::str
#define e_num jsonparser::num
#define e_enter_list jsonparser::enter_list
#define e_exit_list jsonparser::exit_list
#define e_enter_map jsonparser::enter_map
#define e_map_key jsonparser::map_key
#define e_exit_map jsonparser::exit_map
#define e_error jsonparser::error
#define e_finish jsonparser::finish

static const char * test1 =
"\"x\"\n"
;

static jsonparser::event test1e[]={
	{ e_str, "x" },
	{ e_finish }
};

static const char * test2 =
"[ 1, 2.5e+1, 3 ]"
;

static jsonparser::event test2e[]={
	{ e_enter_list },
		{ e_num, 1 },
		{ e_num, 25 },
		{ e_num, 3 },
	{ e_exit_list },
	{ e_finish }
};

static const char * test3 =
"{ \"foo\": [ true, false, null ] }\n"
;

static jsonparser::event test3e[]={
	{ e_enter_map },
		{ e_map_key, "foo" },
		{ e_enter_list },
			{ e_jtrue },
			{ e_jfalse },
			{ e_jnull },
		{ e_exit_list },
	{ e_exit_map },
	{ e_finish }
};

static const char * test4 =
"{ \"a\": [ { \"b\": [ 1, 2 ], \"c\": [ 3, 4 ] }, { \"d\": [ 5, 6 ], \"e\": [ 7, 8 ] } ],\n"
"  \"f\": [ { \"g\": [ 9, 0 ], \"h\": [ 1, 2 ] }, { \"i\": [ 3, 4 ], \"j\": [ 5, 6 ] } ] }"
;

static jsonparser::event test4e[]={
	{ e_enter_map },
		{ e_map_key, "a" },
		{ e_enter_list },
			{ e_enter_map },
				{ e_map_key, "b" },
				{ e_enter_list }, { e_num, 1 }, { e_num, 2 }, { e_exit_list },
				{ e_map_key, "c" },
				{ e_enter_list }, { e_num, 3 }, { e_num, 4 }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "d" },
				{ e_enter_list }, { e_num, 5 }, { e_num, 6 }, { e_exit_list },
				{ e_map_key, "e" },
				{ e_enter_list }, { e_num, 7 }, { e_num, 8 }, { e_exit_list },
			{ e_exit_map },
		{ e_exit_list },
		{ e_map_key, "f" },
		{ e_enter_list },
			{ e_enter_map },
				{ e_map_key, "g" },
				{ e_enter_list }, { e_num, 9 }, { e_num, 0 }, { e_exit_list },
				{ e_map_key, "h" },
				{ e_enter_list }, { e_num, 1 }, { e_num, 2 }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "i" },
				{ e_enter_list }, { e_num, 3 }, { e_num, 4 }, { e_exit_list },
				{ e_map_key, "j" },
				{ e_enter_list }, { e_num, 5 }, { e_num, 6 }, { e_exit_list },
			{ e_exit_map },
		{ e_exit_list },
	{ e_exit_map },
	{ e_finish }
};

static void testjson(const char * json, jsonparser::event* expected)
{
	jsonparser parser(json);
	while (true)
	{
		jsonparser::event actual = parser.next();
		
//printf("e=%i [%s] [%s]\n", expected->action, (const char*)expected->name, (const char*)expected->value);
//printf("a=%i [%s] [%s]\n\n", actual.action,  (const char*)actual.name,    (const char*)actual.value);
		assert_eq(actual.action, expected->action);
		assert_eq(actual.str, expected->str);
		if (expected->action == e_num) assert_eq(actual.num, expected->num);
		
		if (expected->action == e_finish || actual.action == e_finish) return;
		
		expected++;
	}
}

static void testjson_error(const char * json)
{
	jsonparser parser(json);
	int depth = 0;
	bool error = false;
	int events = 0;
	while (true)
	{
		jsonparser::event ev = parser.next();
//if (events==999)
//printf("a=%i [%s] [%s]\n\n", ev.action, ev.name.data().ptr(), ev.value.data().ptr());
		if (ev.action == e_error) error = true; // any error is fine
		if (ev.action == e_enter_list || ev.action == e_enter_map) depth++;
		if (ev.action == e_exit_list  || ev.action == e_exit_map)  depth--;
		if (ev.action == e_finish) break;
		assert(depth >= 0);
		
		events++;
		assert(events < 1000); // fail on infinite error loops
	}
	assert_eq(error, true);
	assert_eq(depth, 0);
}

test()
{
	testcall(testjson(test1, test1e));
	testcall(testjson(test2, test2e));
	testcall(testjson(test3, test3e));
	testcall(testjson(test4, test4e));
	
	testcall(testjson_error("{"));
	testcall(testjson_error("{\"a\""));
	testcall(testjson_error("{\"a\":"));
	testcall(testjson_error("{\"a\":1"));
	testcall(testjson_error("{\"a\":1,"));
	testcall(testjson_error("["));
	testcall(testjson_error("[1"));
	testcall(testjson_error("[1,"));
	testcall(testjson_error("\""));
	testcall(testjson_error("01"));
	testcall(testjson_error("1."));
	testcall(testjson_error("1."));
	testcall(testjson_error("1e"));
	testcall(testjson_error("1e+"));
	testcall(testjson_error("1e-"));
	testcall(testjson_error("z"));
}
#endif
