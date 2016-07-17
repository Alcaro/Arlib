#include "bml.h"
#include "test.h"
#include <ctype.h>

struct bmlparser_st {
	bmlparser* target;
	cstring indent;
	cstring data;
	bool* ok;
};

static cstring cut(cstring& input, int cut)
{
	cstring ret = input.csubstr(0, cut);
	input = input.csubstr(cut, ~0);
	return ret;
}

static cstring cut(cstring& input, int skipstart, int cut, int skipafter)
{
	cstring ret = input.csubstr(skipstart, cut);
	input = input.csubstr(cut+skipafter, ~0);
	return ret;
}

//takes a single line, returns the first node in it
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
		case '\n':
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

static void bml_parse(bmlparser_st& state)
{
	while (state.data.length())
	{
		//TODO: nuke comments
		//TODO: validate whitespace
		
		int nlpos;
		for (nlpos=0;state.data[nlpos]!='\n' && state.data[nlpos]!='\0';nlpos++) {}
		
		cstring line = cut(state.data, 0, nlpos, (state.data[nlpos]=='\n') ? 1 : 0);
		
		cstring node;
		bool hasvalue=false;
		cstring value;
//puts(line);
		if (!bml_parse_inline_node(line, node, hasvalue, value))
		{
			state.ok[0]=false;
			break;
		}
		
		state.target->enter(node, value);
		//TODO: parse multiline
		while (line.length())
		{
			if (!bml_parse_inline_node(line, node, hasvalue, value))
			{
				state.ok[0]=false;
				break;
			}
			state.target->enter(node, value);
			state.target->exit();
		}
		//TODO: parse children on upcoming lines
		state.target->exit();
printf("((%s))((%s))\n",line.data(),state.data.data());
//break;
	}
}

bool bml_parse(cstring bml, bmlparser* ret)
{
	bmlparser_st parser;
	parser.target = ret;
	parser.indent = "";
	parser.data = bml;
	bool ok;
	parser.ok = &ok;
	bml_parse(parser);
	return ok;
}



#ifdef ARLIB_TEST
enum { e_enter, e_exit, e_error, e_finish };
struct expect {
	int action;
	const char * name;
	const char * val;
};

class bmltest : public bmlparser {
public:
	expect* expected;
	
	void action(int what, cstring name, cstring val)
	{
		if (!expected) return;
		if (expected->name==NULL) expected->name="";
		if (expected->val==NULL) expected->val="";
printf("e=%i [%s] [%s]\n", expected->action, expected->name, expected->val);
printf("a=%i [%s] [%s]\n\n", what, (const char*)name, (const char*)val);
		
		//if (expected->action != what) puts("fail1");
		//if (expected->name != name) puts("fail2");
		//if (expected->val != val) puts("fail3");
		if (expected->action != what || expected->name != name || expected->val != val) expected=NULL;
		else expected++;
	}
	
	void enter(cstring name, cstring val)
	{
		action(e_enter, name, val);
	}
	
	void exit()
	{
		action(e_exit, "", "");
	}
	
	void error(cstring what)
	{
		action(e_error, "", "");
	}
};

const char * test1 =
"node\n"
"node=foo\n"
"node=\"foo bar\"\n"
"node: foo bar\n"
"node child=foo\n"
"#bar";
expect test1e[]={
	{ e_enter, "node" },
	{ e_exit },
	{ e_enter, "node", "foo" },
	{ e_exit },
	{ e_enter, "node", "foo bar" },
	{ e_exit },
	{ e_enter, "node", "foo bar" },
	{ e_exit },
	{ e_enter, "node" },
	{ e_enter, "child", "foo" },
	{ e_exit },
	{ e_exit },
	{ e_finish }
};

const char * test2 =
"parent\n"
" node=123 child1=456 child2: 789 123\n"
"  child3\n";
expect test2e[]={
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
expect test3e[]={
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
expect test4e[]={
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

static bool testbml(const char * bml, expect* expected)
{
	bmltest foo;
	foo.expected = test1e;
	bml_parse(test1, &foo);
	return (foo.expected && foo.expected->action == e_finish);
}

test()
{
puts("");
	assert(testbml(test1, test1e));
	assert(testbml(test2, test2e));
	assert(testbml(test3, test3e));
	assert(testbml(test4, test4e));
	return true;
}
#endif
