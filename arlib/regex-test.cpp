#include "regex.h"
#include "test.h"

template<typename... Args>
static void testfn(const char * re, const char * input, Args... args)
{
	regex rx;
	assert(rx.parse(re));
	regex::match_t<5> result = rx.match(input);
	
	const char * exp_capture_raw[] = { args... };
	string exp_capture;
	string capture;
	for (size_t n=0;n<sizeof...(args);n++)
	{
		if (n) exp_capture += "/";
		exp_capture += (exp_capture_raw[n] ? exp_capture_raw[n] : "(null)");
	}
	
	for (size_t n=0;n<result.size();n++)
	{
		if (n) capture += "/";
		auto actual = result[n];
		if (actual.start && actual.end) capture += cstring(arrayview<char>(actual.start, actual.end-actual.start));
		else if (!actual.start && !actual.end) capture += "(null)";
		else capture += "<ERROR>"; // probably caused by something returning neither null nor next submatch
	}
	
	testctx(re)
		assert_eq(capture, exp_capture);
}
#define test1(exp, input, ...) testcall(testfn(exp, input, __VA_ARGS__))
#define test1fail(exp) do { regex rg; assert(!rg.parse(exp)); } while(0)

test("regex", "string", "regex")
{
	test1("abc", "abc", "abc");
	test1("abc", "abcd", "abc");
	test1("(ab)c", "abc", "abc", "ab");
	test1("abc", "def", nullptr);
	test1("[Aa]", "A", "A");
	test1("[Aa][Bb][Cc]", "Abc", "Abc");
	test1("[Aa][Bb][Cc]", "bcd", nullptr);
	test1("[abc-]", "b", "b");
	test1("[a-b-c-d]", "c", "c");
	test1("[a-b-c-d]", "-", "-");
	test1("[]", "a", nullptr);
	test1("[^]", "a", "a");
	test1("\xC3[\xB8\x98]", "ø", "ø");
	test1("\\xC3\\xB8", "ø", "ø");
	test1("(a){5}", "aaaaaa", "aaaaa", "a");
	test1("(abc|def)", "abcx", "abc", "abc");
	test1("(abc|def)", "defx", "def", "def");
	test1("(abc|def)", "ghix", nullptr, nullptr);
	test1("(abc|abcd)de", "abcde",  "abcde",  "abc");
	test1("(abc|abcd)de", "abcdde", "abcdde", "abcd");
	test1("(abcd|abc)de", "abcde",  "abcde",  "abc");
	test1("(abcd|abc)de", "abcdde", "abcdde", "abcd");
	test1("(abc|def)(ghi|jkl)", "abcghix", "abcghi", "abc", "ghi");
	test1("(abc|def)(ghi|jkl)", "abcjklx", "abcjkl", "abc", "jkl");
	test1("(abc|def)(ghi|jkl)", "defghix", "defghi", "def", "ghi");
	test1("(abc|def)(ghi|jkl)", "defjklx", "defjkl", "def", "jkl");
	test1("(abc|def)(ghi|jkl)", "abcdef", nullptr, nullptr, nullptr);
	test1("(abc|def)(ghi|jkl)", "abcgkl", nullptr, nullptr, nullptr);
	test1("(abc)?\\1", "", "", nullptr);
	test1("(abc)?\\1", "abc", "", nullptr);
	test1("(abc)?\\1", "abcabc", "abcabc", "abc");
	test1("([ab])*", "ab", "ab", "b");
	test1("([ab])*", "a", "a", "a");
	test1("([ab])*", "", "", nullptr);
	test1("([ab])+?c", "abc", "abc", "b");
	test1("((.)\\2){3}", "aabbccddeeff", "aabbcc", "cc", "c");
	test1("((.)\\2){2,4}", "aabbcc", "aabbcc", "cc", "c");
	test1("((.)\\2){2,4}?", "aabbcc", "aabb", "bb", "b");
	test1("((.)..)+", "12345678", "123456", "456", "4");
	test1("((.)..)+...", "12345678", "123456", "123", "1");
	test1("((.)..){1,5}", "12345678", "123456", "456", "4");
	test1("((.)..){1,5}...", "12345678", "123456", "123", "1");
	test1("((?=(.b)))a", "ab", "a", "", "ab");
	test1("((?!(.b)))a", "ab", nullptr, nullptr, nullptr);
	test1("((?=(.b)))a", "ac", nullptr, nullptr, nullptr);
	test1("((?!(.b)))a", "ac", "a", "", nullptr);
	test1("(?!(.)\\1)a", "ab", "a", nullptr);
	test1("(?!(.)\\1)a", "aa", nullptr, nullptr);
	test1("(?!(?!(a)))", "a", "", nullptr);
	test1("(?!(?!(a)))", "b", nullptr, nullptr);
	test1("\\b.\\b.\\B", "a+", "a+");
	test1("\\B.\\b.\\b", "+a", "+a");
	test1(".\\b.", "++", nullptr);
	test1(".\\b.", "aa", nullptr);
	test1("\\b.", "+", nullptr);
	test1("\\B.", "a", nullptr);
	test1(".\\b", "+", nullptr);
	test1(".\\B", "a", nullptr);
	test1("\\b", "a", "");
	test1("\\B", "a", nullptr);
	test1("\\b", "%", nullptr);
	test1("\\B", "%", "");
	test1("\\b", "", nullptr);
	test1("\\B", "", "");
	test1("((a)|(b))+", "ab", "ab", "b", nullptr, "b");
	test1("((a)|(b))+", "ba", "ba", "a", "a", nullptr);
	test1("((a)|(b)){2}", "ab", "ab", "b", nullptr, "b");
	test1("((a)|(b)){2}", "ba", "ba", "a", "a", nullptr);
	test1("((a)\\2|(b)\\3){2}", "aabb", "aabb", "bb", nullptr, "b");
	test1("((a)\\2|(b)\\3){2}", "bbaa", "bbaa", "aa", "a", nullptr);
	test1("^(?:a|ab)*", "aabababaaaabab", "aa"); // matches a twice, and never tries ab without backtracking; regex ends there, so no backtracking needed
	test1("^(?:a|ab)*$", "aabababaaaabab", "aabababaaaabab"); // this, however, needs to backtrack
	test1("^(?:ab|a)*", "aabababaaaabab", "aabababaaaabab"); // this tries ab first and captures everything
	test1("(?:aa)+(?:aaa)+", "aaaaaaaaaa", "aaaaaaaaa"); // similar to the above, the longest legal match isn't necessarily the right one
	test1("a?a?(?:aa)?", "aaa", "aa"); // like the above, but with no + or *
	
	test1("a?a?a?a?a?bc", "aaabcd", "aaabc");
	test1("a?a?a?a?a?bc", "aaaaaabcd", nullptr);
	test1("a?a?a?a?a?", "aaaaaabcd", "aaaaa");
	test1("a??a??a??a??a??", "aaaaaabcd", "");
	test1("a*b*c*", "aaabcccd", "aaabccc");
	test1("a*b*c*?", "aaabcccd", "aaab");
	test1("a+b+c+", "aaabcccd", "aaabccc");
	test1("a+b+c+?", "aaabcccd", "aaabc");
	test1("a{2,5}bc", "aaaabcd", "aaaabc");
	test1("a{2,5}", "aaaabcd", "aaaa");
	test1("a{2,5}?", "aaaabcd", "aa");
	test1("a{2,5}?b", "aaaabcd", "aaaab");
	test1("a{2,5}?b", "aaaaaabcd", nullptr);
	test1("a{5}", "aaaaaabcd", "aaaaa");
	test1("a{5}?", "aaaaaabcd", "aaaaa"); // lazy quantifier has no effect on fixed-width repeats
	test1("a{1}?", "aaaaaabcd", "a");
	test1("a{,5}", "aaa", "aaa");
	test1("a{,5}", "aaabc", "aaa");
	test1("a{,5}?", "aaa", "");
	test1("a{,5}?", "aaabc", "");
	test1("a{,5}bc", "aaa", nullptr);
	test1("a{,5}bc", "aaabc", "aaabc");
	test1("a{,5}?bc", "aaa", nullptr);
	test1("a{,5}?bc", "aaabc", "aaabc");
	test1("a{3,}?", "aaaaa", "aaa");
	test1("a{3,}", "aaaaa", "aaaaa");
	test1("ax{0}bc", "abc", "abc");
	test1("(ab)*", "ababababa", "abababab", "ab");
	test1("(ab){3}", "ababababa", "ababab", "ab");
	test1("(ab){3}\\1", "ababababa", "abababab", "ab");
	test1("a\\nb", "a\nb", "a\nb");
	test1("a\nb", "a\nb", "a\nb");
	test1("a[\n]b", "a\nb", "a\nb");
	test1("a\\sb", "a\nb", "a\nb");
	test1("a\\Db", "a\nb", "a\nb");
	test1("a\\x62c", "abc", "abc");
	test1("\\cB", "\1", "\1");
	test1("a[abc]c", "abc", "abc");
	test1("a[a-z]c", "abc", "abc");
	test1("a[a-zA-Z]c", "aBc", "aBc");
	test1fail("a[A-]]c");
	test1fail("(?!(a))\\1");
	test1("a[\\w]c", "abc", "abc");
	test1("a\\wc", "abc", "abc");
	test1("a|b|cd", "b", "b");
	test1("a|b|cd", "cd", "cd");
	test1("(?:a)b", "ab", "ab");
	test1("(a)b\\1", "aba", "aba", "a");
	test1fail("(a)|\\4");
	test1fail("(a)|\\1");
	test1("(?:(a)|b)\\1", "cd", nullptr, nullptr);
	test1("(?:(a)|b)\\1", "b", "b", nullptr);
	test1("(?:(a)b|aa)\\1", "aaa", "aa", nullptr);
	test1(".\\b.", "a%", "a%");
	test1(".\\B.", "ab", "ab");
	test1(".\\b.", "ab", nullptr);
	test1(".\\B.", "a%", nullptr);
	test1("(?:)+e", "e", "e"); // should not be an infinite loop
	
	test1("a|b||c", "b", "b");
	test1("(?:a|b||c)d", "bd", "bd");
	test1("(?:a*)a", "aa", "aa");
	test1("(?:a*)b", "ab", "ab");
	test1("(?:ab)*ab", "abab", "abab");
	test1("(?:ab)*aab", "abaab", "abaab");
	test1("a?", "aaa", "a");
	test1("a??", "aaa", "");
	test1("a+", "aaa", "aaa");
	test1("a+?", "aaa", "a");
	test1("a*", "aaa", "aaa");
	test1("a*?", "aaa", "");
	test1("a|", "abc", "a");
	test1("|a", "abc", "");
	test1("aa|a|aaa", "aaa", "aa"); // can't be nfa
	test1("a*b+c?d", "abcd", "abcd");
	test1("ab*c", "abbc", "abbc");
	test1("a(?:aa)*|(?:aa)*", "aaaa", "aaa"); // can't be nfa
	test1("aaabc", "aaabcd", "aaabc");
	test1("cd?e?f+g*hi", "cdfffghi", "cdfffghi");
	test1("a|b|cd", "b", "b");
	test1("a||cd", "cd", ""); // no input string can return matches in non-ascending non-descending order, but still no nfa
	test1("a(?:a||cd)b", "acdb", "acdb");
	test1("q(?:a|b|cd?e?f+g)h", "qcdfffgh", "qcdfffgh");
	test1("(?:q^|a|b|cd|e|$)", "cd", "cd");
	test1("(?:a^|b|(?:c|d?e|(?:f|g|hi)j+k)l|mn|o|$)", "gjkl", "gjkl");
	test1("a*(?:auth|axolotl|axe)", "aaaxolotl", "aaaxolotl");
	test1("(?:ab?c?)*(?:auth|author|axolotl|axe)", "abaabcaxolotl", "abaabcaxolotl");
	test1("[abc]@[def]", "b@d", "b@d"); // try to tickle the unique_bytes cross-chunk case
	test1("[abc]\\?[def]", "b?d", "b?d");
	test1(R"(<(@[&!]?\d+|#\d+)>)", "<@12345>", "<@12345>", "@12345"); // caused some trouble with the DFA deduplicator
	
	assert_eq((cstring)(regex("bc").search("abc")[0].start), "bc");
	assert_eq(regex("f(oo)").replace("foofoobarfoo", "\\1"), "oooobaroo");
	
	assert_eq(cstring("foo bar baz").csplit(regex("\\b|a")).join(","), "foo, ,b,r, ,b,z");
	assert_eq(cstring("foo bar baz").csplit<1>(regex(" |(?=\\n)")).join(","), "foo,bar baz");
	assert_eq(cstring("foo\nbar baz").csplit<1>(regex(" |(?=\\n)")).join(","), "foo,\nbar baz");
	assert_eq(cstring("").csplit<1>(regex("a")).size(), 1);
	assert_eq(cstring("aabcaada").csplit(regex("a")).join(","), ",,bc,,d,");
	
	cstrnul text = "abc 123";
	auto m = regex("(abc) (123)").match(text); // ensure it doesn't copy 'text' then return a match array full of UAF
	assert_eq(m[1].str(), "abc");
	
	//test_nomalloc {
		//assert(REGEX("abc").match("abc"));
	//};
	
	regex r;
	assert(r.parse("(?:)"));
	assert(r);
	assert(!r.parse("(?:"));
	assert(!r);
	assert(r.parse(""));
	assert(r);
	
	assert_eq(regex::required_substrs("a"), array<string>{"a"});
	assert_eq(regex::required_substrs("abc"), array<string>{"abc"});
	assert_eq(regex::required_substrs("abc.def.ghi"), (array<string>{"abc","def","ghi"}));
	assert_eq(regex::required_substrs("abc(def)ghi"), (array<string>{"abc","def","ghi"}));
	assert_eq(regex::required_substrs("abc+"), (array<string>{"ab"})); // many of these could be improved, but no real point
	assert_eq(regex::required_substrs("abc|abc"), (array<string>{}));
	assert_eq(regex::required_substrs("(abc)+"), (array<string>{}));
	
	//test1("(?:)*?$", "a", ""); // infinite loop
	//test1(".+a................................a", "b", ""); // stack overflow in nfa->dfa converter
	test1("(?:|(a?){0,2})\\1b", "a", nullptr, nullptr);
}
#undef test1
#undef test1fail

static void testfn_search(const char * exp, const char * str, int off)
{
	regex_search rs;
	assert(rs.parse(exp));
//rs.dump();
	const char * ret = rs.search(str);
	int off_act = (ret ? ret-str : -1);
	assert_eq(off_act, off);
}
#define test1(exp, input, off) testcall(testfn_search(exp, input, off))
#define test1fail(exp) do { regex_search rs; assert(!rs.parse(exp)); } while(0)
test("regex search", "string", "regex")
{
	test1("a", "walrus", 1);
	test1("[abc]", "walrus", 1);
	test1("bcd", "abcdefg", 1);
	test1("b|bcd|bc", "abcdefg", 1);
	test1("bcde|cd[^]*", "abcdefg", 1);
	test1("bcde|cd[^]*", "abcdZfg", 2);
	test1("bcde|cd[^]*", "abcZefg", -1);
	test1("abc|[^]*", "b", 0);
	test1("a+a+a+", "abaabaaabaaaa", 5);
	test1("a*a*", "abaabaaabaaaa", 0);
	test1("bb", "bababababababababababb", 20);
	test1("a*a*a*bc*c*c*d*e*e*e*", "abaabaaabaaaa", 0);
	test1("a(?:b*b*c*c*|d*d*e*e*|f*f*g*g*|)*h", "haccededh", 1);
	test1("(?:a*a*b*)*c", "abaabaaabaaaac", 0);
	test1("", "walrus", 0);
	test1fail("^");
	test1fail("(a)");
	test1fail("(?=a)");
	test1(R"([^0-9A-Za-z\s\x80-\xBF\xC3-\xFF]|\n\n| {2,}\n|\w+:\S)", "k", -1);
}
