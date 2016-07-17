#include "bml.h"
#include "test.h"
#include <ctype.h>

/*

[bml]
a
b
 c
d e f
g
 :h
 :i
j
 k
  l
  m

read "a"
{a}
set m_indent_step[0]
return enter a

read "b"
m_indent_step.size(){1} > m_indent.length(){0}, so:
 {b}
 clear last true element of m_indent_step
 clear trailing false elements of m_indent_step
 restore "b" to read buffer
 return exit

read "b", goto {a}

read " c"
{c}
set m_indent to " "
m_indent_step.size(){1} <= m_indent.length(){1}
set m_indent_step[1]
return enter c

read "d e"
set m_indent to ""
m_indent_step.size(){2} > m_indent.length(){0}, so goto {b}

read "d e"
m_indent_step.size(){1} > m_indent.length(){0}, so goto {b}

read "d e"
m_indent_step.size(){0} <= m_indent.length(){0}
set m_inlines = " e f" (or "e f", not sure and doesn't matter)
goto {a}

m_inlines is not empty, so:
{e}
read "e f"
set m_exit
set m_inlines to "f"
return enter e

m_exit is set, so:
{ex}
clear m_exit
return exit

m_inlines is not empty, so goto {e} [m_inlines = ""]
m_exit is set, so goto {ex}


[bml]
g
 :h
 :i
j
 k
  l
  m

read "g"
read " :h"
if it doesn't start with colon, restore to read buffer
but it does, so:
  set m_indent = " "
  read " :i"
  it too starts with colon, so ensure that indentation is identical
  read "j"
  it does not start with colon, so restore to read buffer
  set m_indent_step[0]
  return enter g="h i"

read "j", goto {a}
read " k", goto {c}

read "  l"
set m_indent to "  "
m_indent_step.size(){2} <= m_indent.length(){2}
set m_indent_step[2]
return enter l

read "  m"
m_indent_step.size(){3} > m_indent.length(){2}, so goto {b}

read "  m"
m_indent_step.size(){2} <= m_indent.length(){2}
set m_indent_step[2]
return enter m

read ""
set m_indent to ""
m_indent_step.size(){2} > m_indent.length(){0}, so goto {b}

read ""
m_indent_step.size(){1} > m_indent.length(){0}, so goto {b}

read ""
nothing else to do, so return finish

*/


namespace {
class bmlparser_impl : public bmlparser {
public:
	bool m_exit;
	cstring m_inlines;
	cstring m_indent;
	array<bool> m_indent_step;
	cstring m_nextline;
	cstring m_data;
	
	static cstring cut(cstring& input, int skipstart, int cut, int skipafter)
	{
		cstring ret = input.csubstr(skipstart, cut);
		input = input.csubstr(cut+skipafter, ~0);
		return ret;
	}
	
	static cstring cutline(cstring& input)
	{
		int nlpos = 0;
		while (input[nlpos]!='\r' && input[nlpos]!='\n' && input[nlpos]!='\0') nlpos++;
		return cut(input, 0, nlpos, (input[nlpos]=='\r') ? 2 : (input[nlpos]=='\n') ? 1 : 0);
	}
	
	//takes a single line, returns the first node in it
	//hasvalue is to differentiate 'foo' from 'foo='; only the former allows a multi-line value
	static bool bml_parse_inline_node(cstring& data, cstring& node, bool& hasvalue, cstring& value)
	{
		int nodestart = 0;
		while (data[nodestart]==' ' || data[nodestart]=='\t') nodestart++;
		
		int nodelen = nodestart;
		while (isalnum(data[nodelen]) || data[nodelen]=='-' || data[nodelen]=='.') nodelen++;
		node = cut(data, nodestart, nodelen, 0);
		switch (data[0])
		{
			case '\0':
			case ' ':
			case '\t':
			{
				hasvalue=false;
				return true;
			}
			case ':':
			{
				hasvalue=true;
				int valstart = 1;
				while (data[valstart]==' ' || data[valstart]=='\t') valstart++;
				int valend = valstart;
				while (data[valend]!='\0') valend++;
				value = cut(data, valstart, valend, 0);
				return true;
			}
			case '=':
			{
				if (data[1]=='"')
				{
					hasvalue = true;
					int valend = 2;
					while (data[valend]!='"' && data[valend]!='\0') valend++;
					if (data[valend]!='"')
					{
						while (data[valend]!='\0') valend++;
						data = data.csubstr(valend, ~0);
						return false;
					}
					value = cut(data, 2, valend, 1);
					return true;
				}
				else
				{
					hasvalue = true;
					int valend = 0;
					while (data[valend]!=' ' && data[valend]!='\0') valend++;
					value = cut(data, 1, valend, 0);
					return true;
				}
			}
			default:
				return false;
		}
	}
	
	event next()
	{
		if (m_exit)
		{
			m_exit = false;
			return (event){ exit };
		}
		
		if (m_inlines)
		{
			event ev = { enter };
			bool dummy;
			if (!bml_parse_inline_node(m_inlines, ev.name, dummy, ev.value))
			{
				return (event){ error };
			}
			
			m_exit = true;
			return ev;
		}
		
		if (m_nextline)
		{
		handleline:
			event ev = { enter };
			bool banmultiline;
			if (!bml_parse_inline_node(m_nextline, ev.name, banmultiline, ev.value))
			{
				return (event){ error };
			}
			m_inlines = m_nextline;
			
			m_nextline = 
			
			m_exit = true;
			return ev;
		}
		
		if (m_data)
		{
		nextline:
			m_nextline = cutline(m_data);
			int indentlen = 0;
			while (m_nextline[indentlen] == ' ' || m_nextline[indentlen] == '\t') indentlen++;
			cstring newindent = cut(m_nextline, 0, indentlen, 0);
			
			if (m_nextline[0] == '#' || m_nextline[0] == '\0') goto nextline;
			
			if (memcmp(m_indent.nt(), newindent.nt(), min(m_indent.length(), newindent.length())) != 0)
			{
				return (event){ error };
			}
			goto handleline;
		}
		
		return (event){ finish };
	}
	
	bmlparser_impl(cstring bml)
	{
		m_exit = false;
		m_data = bml;
	}
};
}

bmlparser* bmlparser::create(cstring bml)
{
	return new bmlparser_impl(bml);
}



#ifdef ARLIB_TEST
#define e_enter bmlparser::enter
#define e_exit bmlparser::exit
#define e_error bmlparser::error
#define e_finish bmlparser::finish

const char * test1 =
"node\n"
"node=foo\n"
"node=\"foo bar\"\n"
"node: foo bar\n"
"node\n"
" child\n"
"node child=foo\n"
"node=\n"
"node=\"\"\n"
"node:\n"
"node\tchild\n"
"#bar\n"
"node";
bmlparser::event test1e[]={
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node", "foo" },
	{ e_exit },
	{ e_enter, "node", "foo bar" },
	{ e_exit },
	{ e_enter, "node", "foo bar" },
	{ e_exit },
	{ e_enter, "node" },
		{ e_enter, "child" },
		{ e_exit },
	{ e_exit },
	{ e_enter, "node" },
		{ e_enter, "child", "foo" },
		{ e_exit },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node" },
		{ e_enter, "child" },
		{ e_exit },
	{ e_exit },
	{ e_enter, "node" },
	{ e_exit },
	{ e_finish }
};

const char * test2 =
"parent\n"
" node=123 child1=456 child2: 789 123\n"
"  child3\n";
bmlparser::event test2e[]={
	{ e_enter, "parent" },
		{ e_enter, "node", "123" },
			{ e_enter, "child1", "456" },
			{ e_exit },
			{ e_enter, "child2", "789 123" },
			{ e_exit },
			{ e_enter, "child3" },
			{ e_exit },
		{ e_exit },
	{ e_exit },
	{ e_finish }
};

const char * test3 =
"a b=1 c=2 d: 3\n"
" e=4 f=5\n"
" g h=6\n"
"  :7\n"
"i";
bmlparser::event test3e[]={
	{ e_enter, "a" },
		{ e_enter, "b", "1" },
		{ e_exit },
		{ e_enter, "c", "2" },
		{ e_exit },
		{ e_enter, "d", "3" },
		{ e_exit },
		{ e_enter, "e", "4" },
			{ e_enter, "f", "5" },
			{ e_exit },
		{ e_exit },
		{ e_enter, "g", "7" },
			{ e_enter, "h", "6" },
			{ e_exit },
		{ e_exit },
	{ e_exit },
	{ e_enter, "i" },
	{ e_exit },
	{ e_finish }
};

const char * test4 =
"Parent-1.0=A-value child child=\"B value\" child:C:\"value\"\n"
"  child:D:\"value\"\n"
"    grandchild\n"
"  child grandchild=A\n"
"    :multi-line\n"
"    :text-field\n"
"    grandchild=B foo=bar\n"
"      foo=bar\n"
"\n"
"Parent-1.0";
bmlparser::event test4e[]={
	{ e_enter, "Parent-1.0", "A-value" },
		{ e_enter, "child" },
		{ e_exit },
		{ e_enter, "child", "B value" },
		{ e_exit },
		{ e_enter, "child", "C:\"value\"" },
		{ e_exit },
		{ e_enter, "child", "D:\"value\"" },
			{ e_enter, "grandchild" },
			{ e_exit },
		{ e_exit },
		{ e_enter, "child", "multi-line\ntext-field" },
			{ e_enter, "grandchild", "A" },
			{ e_exit },
			{ e_enter, "grandchild", "B" },
				{ e_enter, "foo", "bar" },
				{ e_exit },
				{ e_enter, "foo", "bar" },
				{ e_exit },
			{ e_exit },
		{ e_exit },
	{ e_exit },
	{ e_enter, "Parent-1.0" },
	{ e_exit },
	{ e_finish }
};

static bool testbml(const char * bml, bmlparser::event* expected)
{
	autoptr<bmlparser> parser = bmlparser::create(bml);
	while (true)
	{
		bmlparser::event actual = parser->next();
		
printf("e=%i [%s] [%s]\n", expected->action, expected->name.data(), expected->value.data());
printf("a=%i [%s] [%s]\n\n", actual.action, actual.name.data(), actual.value.data());
		assert_eq(expected->action, actual.action);
		assert_eq(expected->name, actual.name);
		assert_eq(expected->value, actual.value);
		
		if (expected->action == e_finish || actual.action == e_finish) return true;
		
		expected++;
	}
}

static bool testbml_error(const char * bml)
{
	autoptr<bmlparser> parser = bmlparser::create(bml);
	for (int i=0;i<100;i++)
	{
		bmlparser::event ev = parser->next();
		if (ev.action == e_error) return true;
	}
	assert(!"expected error");
}

test()
{
puts("");
	assert(testbml(test1, test1e));
	assert(testbml(test2, test2e));
	assert(testbml(test3, test3e));
	assert(testbml(test4, test4e));
	assert(testbml_error("-"));
	assert(testbml_error("a\n  b\n c"));
	assert(testbml_error("a\n b\n\tc"));
	assert(testbml_error("a=b\n :c"));
	return true;
}
#endif
