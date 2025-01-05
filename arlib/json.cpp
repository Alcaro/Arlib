#include "json.h"
#include "stringconv.h"
#include "simd.h"
#include "endian.h"
#include "js-identifier.h"
#include <math.h>

// TODO: find some better place for this
// if input is 0, undefined behavior
static inline int ctz32(uint32_t in)
{
#if defined(__GNUC__)
	return __builtin_ctz(in);
#elif defined(_MSC_VER)
	DWORD n;
	_BitScanForward(&n, in);
	return n;
#else
	int ret = 0;
	if (!(in&0xFFFF)) { ret += 16; in >>= 16; }
	if (!(in&0xFF))   { ret += 8;  in >>= 8; }
	if (!(in&0xF))    { ret += 4;  in >>= 4; }
	if (!(in&0x3))    { ret += 2;  in >>= 2; }
	if (!(in&0x1))    { ret += 1;  in >>= 1; }
	return ret;
#endif
}


// https://spec.json5.org/
// I disagree with some of the design choices, but such is what json5 says
//   \u in unquoted keys, \e being e and not error, control chars in quoted strings,
//   and https://spec.json5.org/#prod-JSON5Identifier defers to https://262.ecma-international.org/5.1/#sec-7.6, which explicitly says
//     "ECMAScript implementations may recognise identifier characters defined in later editions of the Unicode Standard."
//   so it's unclear whether {\u03ff:1} aka {Ͽ:1} is, or should be, legal

template<bool json5>
jsonparser::event jsonparser::next_inner()
{
	skip_spaces<json5>();
	if (m_need_error)
	{
		m_need_error = false;
		m_errored = true;
		return { jsonparser::error };
	}
	const uint8_t * end = m_data_holder.bytes().ptr() + m_data_holder.length();
	if (m_at == end)
	{
		if (m_nesting)
		{
			m_need_error = true;
			bool is_map = m_nesting[m_nesting.size()-1];
			if (is_map && !m_want_key)
			{
				m_want_key = true;
				return { jsonparser::jnull };
			}
			m_nesting.resize(m_nesting.size()-1);
			bool next_map = (m_nesting && m_nesting[m_nesting.size()-1]);
			m_want_key = next_map;
			if (is_map)
				return { jsonparser::exit_map };
			else
				return { jsonparser::exit_list };
		}
		return { jsonparser::finish };
	}
	bytesw ret_str;
	if (m_want_key)
	{
		if (*m_at == '"' || (json5 && *m_at == '\''))
		{
			goto parse_str_key;
		str_key_return: ;
		}
		else if (*m_at == '}')
		{
			goto close_brace;
		}
		else if (json5)
		{
			size_t ret_len = js_identifier_name(m_at);
			if (!ret_len)
			{
				m_at++;
				m_errored = true;
				return { jsonparser::error };
			}
			ret_str = bytesw(m_at, ret_len);
			m_at += ret_len;
		}
		else m_need_error = true;
		skip_spaces<json5>();
		if (*m_at == ':')
			m_at++;
		else
			m_need_error = true;
		m_want_key = false;
		return { jsonparser::map_key, decode_backslashes<json5>(ret_str) };
	}
	if (*m_at == '{')
	{
		m_at++;
		m_want_key = true;
		m_nesting.append(true);
		return { jsonparser::enter_map };
	}
	if (*m_at == '[')
	{
		m_at++;
		m_nesting.append(false);
		return { jsonparser::enter_list };
	}
	if (*m_at == ']')
	{
		bool is_brace;
		is_brace = false;
		if (false)
		{
		close_brace:
			is_brace = true;
		}
		m_at++;
		if (!m_nesting || m_nesting[m_nesting.size()-1] != is_brace)
		{
			m_errored = true;
			return { jsonparser::error };
		}
		m_nesting.resize(m_nesting.size()-1);
		return prepare_next<json5>({ is_brace ? jsonparser::exit_map : jsonparser::exit_list });
	}
	if (*m_at == '"' || (json5 && *m_at == '\''))
	{
	parse_str_key:
		char term = *m_at++;
		uint8_t * start = m_at;
		while (true)
		{
			if (m_at[0] == '\\' && m_at[1] != '\0')
			{
				if (m_at[1] == '\r' && m_at[2] == '\n')
					m_at += 3;
				else
					m_at += 2;
				continue;
			}
			if (!json5 && m_at[0] < 0x20)
				m_need_error = true;
			if (*m_at == '\n' || *m_at == '\r' || *m_at == '\0') // 2028 and 2029 are legal
			{
				m_need_error = true;
				break;
			}
			if (*m_at == term)
				break;
			m_at++;
		}
		
		ret_str = bytesw(start, m_at-start);
		if (*m_at == term)
			m_at++;
		
		if (m_want_key)
			goto str_key_return;
		return prepare_next<json5>({ jsonparser::str, decode_backslashes<json5>(ret_str) });
	}
	
	if (m_at+4 <= end && readu_le32(m_at) == 0x65757274) // true
		return prepare_next<json5>({ jsonparser::jtrue }, 4);
	if (m_at+5 <= end && readu_le32(m_at+1) == 0x65736c61) // alse
		return prepare_next<json5>({ jsonparser::jfalse }, 5);
	if (m_at+4 <= end && readu_le32(m_at) == 0x6c6c756e) // null
		return prepare_next<json5>({ jsonparser::jnull }, 4);
	
	// only numbers (including nan/inf) left
	const uint8_t * num_start = m_at;
	
	if ((json5 && *m_at == '+') || *m_at == '-')
		m_at++;
	
	if (json5 && m_at+8 <= end && readu_le64(m_at) == 0x7974696E69666E49) m_at += 8; // Infinity
	else if (json5 && m_at[0] == 'N' && m_at[1] == 'a' && m_at[2] == 'N') m_at += 3;
	else if (json5 && m_at[0] == '0' && (m_at[1] == 'x' || m_at[1] =='X'))
	{
		m_at += 2;
		if (!isxdigit(*m_at))
			m_need_error = true;
		while (isxdigit(*m_at))
			m_at++;
	}
	else
	{
		if (m_at[0] == '0' && isdigit(m_at[1]))
			m_need_error = true;
		if (!isdigit(*m_at) && (!json5 || *m_at != '.'))
		{
			m_at++;
			m_errored = true;
			return { jsonparser::error }; // if it doesn't look like a number, just discard it
		}
		if (!isdigit(m_at[0]) && !isdigit(m_at[1])) // reject lone decimal point
			m_need_error = true;
		while (isdigit(*m_at))
			m_at++;
		if (*m_at == '.')
		{
			m_at++;
			if (!json5 && !isdigit(*m_at))
				m_need_error = true;
			while (isdigit(*m_at))
				m_at++;
		}
		if (*m_at == 'e' || *m_at == 'E')
		{
			m_at++;
			if (*m_at == '+' || *m_at == '-')
				m_at++;
			if (!isdigit(*m_at))
				m_need_error = true;
			while (isdigit(*m_at))
				m_at++;
		}
	}
	
	return prepare_next<json5>({ jsonparser::num, cstring(bytesr(num_start, m_at-num_start)) });
}

template<bool json5>
void jsonparser::skip_spaces()
{
	// JSON5 considers any JS WhiteSpace or LineTerminator to be whitespace
	// WhiteSpace is 0009 000B 000C 0020 00A0 FEFF, or anything in Unicode 3.0 category Zs
	// LineTerminator is 000A 000D 2028 2029
	// Zs is 0020 00A0 1680 2000-200B 202F 3000 (not 205F - it is Zs, but only since Unicode 3.2)
	// normal JSON only accepts 0009 000A 000D 0020 as whitespace
again:
	if (LIKELY(*m_at < 0x80))
	{
		if (isspace(*m_at) || (json5 && (*m_at == '\f' || *m_at == '\v')))
		{
			m_at++;
			goto again;
		}
		if (json5 && m_at[0] == '/' && m_at[1] == '/')
		{
			// // comments can end with \r \n 2028 2029
			while (true)
			{
				m_at++;
				if (*m_at == '\n' || *m_at == '\r' || *m_at == '\0')
					break;
				if (m_at[0] == '\xE2' && m_at[1] == '\x80' && (m_at[2] == '\xA8' || m_at[2] == '\xA9'))
					break;
			}
			goto again;
		}
		if (json5 && m_at[0] == '/' && m_at[1] == '*')
		{
			// /* comments can contain everything, except */
			const char * end = strstr((char*)m_at+2, "*/");
			if (end)
				end += 2;
			else
			{
				m_need_error = true;
				end = strchr((char*)m_at+2, '\0');
			}
			m_at = (uint8_t*)end;
			goto again;
		}
		return;
	}
	if (!json5)
		return;
	if (memeq(m_at, u8"\u00A0", 2))
	{
		m_at += 2;
		goto again;
	}
	if (m_at[1] == '\x00')
		return;
	uint64_t u2000_series_whitespace =
		(1ull<<0x00) | (1ull<<0x01) | (1ull<<0x02) | (1ull<<0x03) | (1ull<<0x04) | (1ull<<0x05) | (1ull<<0x06) | (1ull<<0x07) |
		(1ull<<0x08) | (1ull<<0x09) | (1ull<<0x0A) | (1ull<<0x0B) | (1ull<<0x28) | (1ull<<0x29) | (1ull<<0x2F);
	if (memeq(m_at, u8"\u1680", 3) || memeq(m_at, u8"\u3000", 3) || memeq(m_at, u8"\uFEFF", 3) ||
	    (memeq(m_at, u8"\u2000", 2) && m_at[2] >= 0x80 && m_at[2] <= 0xBF && ((u2000_series_whitespace >> (m_at[2]&0x3F))&1)))
	{
		m_at += 3;
		goto again;
	}
}

void jsonparser::skip_spaces_json() { skip_spaces<false>(); }
void jsonparser::skip_spaces_json5() { skip_spaces<true>(); }

template<bool json5>
jsonparser::event jsonparser::prepare_next(event ev, size_t forward)
{
	m_at += forward;
	
	skip_spaces<json5>();
	if (!m_nesting)
	{
		if (m_at != m_data_holder.bytes().ptr() + m_data_holder.length())
		{
			m_need_error = true;
			m_at = m_data_holder.bytes().ptr() + m_data_holder.length();
		}
		return ev;
	}
	if (*m_at == ',')
	{
		m_at++;
		skip_spaces<json5>();
		if (!json5 && (*m_at == ']' || *m_at == '}'))
			m_need_error = true;
	}
	else if (*m_at != ']' && *m_at != '}')
		m_need_error = true;
	
	m_want_key = m_nesting[m_nesting.size()-1];
	return ev;
}

template<bool json5>
cstring jsonparser::decode_backslashes(bytesw by)
{
	if (!by.contains('\\'))
		return by;
	uint8_t* out = by.ptr();
	uint8_t* in = by.ptr();
	uint8_t* end = in + by.size();
	while (true)
	{
		// leave every character unchanged, except \, in which case (use first that matches)
		// - if followed by any of the sequences \r\n \n \r \u2028 \u2029, emit nothing
		// - if followed by u, parse 4-digit escape (including utf16 surrogates)
		// - if followed by x, parse 2-digit escape
		// - if followed by 0 and after is not 0-9, emit a NUL byte
		// - if followed by 0-9, emit nothing and set the error flag (octal is explicitly banned by the JS spec)
		// - if followed by one of bfnrtv, emit the corresponding control char
		// - if followed by one of ' " \, emit that byte unchanged
		// - if followed by anything else, emit that byte unchanged
		// of the above, normal JSON only accepts ubfnrt"\, everything else (including the 'anything else' fallback) is an error
		// normal JSON also rejects control chars in strings; JSON5 permits them
		uint8_t* slash = (uint8_t*)memchr(in, '\\', end-in);
		if (!slash)
		{
			memmove(out, in, end-in);
			out += end-in;
			return bytesr(by.ptr(), out-by.ptr());
		}
		memmove(out, in, slash-in);
		out += slash-in;
		if (slash[1] == '\0')
		{
			m_need_error = true;
			return bytesr(by.ptr(), out-by.ptr());
		}
		in = slash+2;
		
		if (json5 && slash[1] == '\r')
		{
			if (slash[2] == '\n')
				in++;
		}
		else if (json5 && slash[1] == '\n') {}
		else if (json5 && (memeq(slash+1, u8"\u2028", 3) || memeq(slash+1, u8"\u2029", 3)))
		{
			in = slash+4;
		}
		else if (slash[1] == 'u' || (json5 && slash[1] == 'x'))
		{
			size_t n_digits = (slash[1] == 'u' ? 4 : 2);
			uint32_t codepoint;
			if (!fromstringhex_ptr((char*)in, (char*)in+n_digits, codepoint))
			{
				m_need_error = true;
				continue;
			}
			in += n_digits;
			
			if (codepoint >= 0xD800 && codepoint <= 0xDCFF && in[0]=='\\' && in[1]=='u')
			{
				uint16_t low_sur;
				if (fromstringhex_ptr((char*)in+2, (char*)in+6, low_sur))
				{
					in += 6;
					codepoint = 0x10000 + ((codepoint-0xD800)<<10) + (low_sur-0xDC00);
				}
			}
			// else leave as is, string::codepoint will return fffd for unpaired surrogates
			
			out += string::codepoint(out, codepoint);
		}
		else if (json5 && slash[1] == '0' && !isdigit(slash[2]))
		{
			*out++ = '\0';
		}
		else if (isdigit(slash[1]))
		{
			m_need_error = true;
		}
		else if (slash[1] == 'b') *out++ = '\b';
		else if (slash[1] == 'f') *out++ = '\f';
		else if (slash[1] == 'n') *out++ = '\n';
		else if (slash[1] == 'r') *out++ = '\r';
		else if (slash[1] == 't') *out++ = '\t';
		else if (json5 && slash[1] == 'v') *out++ = '\v';
		else if (json5) *out++ = slash[1];
		else if (!json5 && (slash[1] == '"' || slash[1] == '\\' || slash[1] == '/')) *out++ = slash[1];
		else m_need_error = true;
	}
}

jsonparser::event jsonparser::next() { return next_inner<false>(); }
jsonparser::event jsonparser::next5() { return next_inner<true>(); }

bool json5parser::parse_num_as_double(cstring str, double& out)
{
	out = 0;
	const char * ptr = str.ptr_raw();
	const char * end = str.ptr_raw_end();
	if (ptr == end) return false;
	
	if (fromstring_ptr(ptr, end, out))
		return true;
	
	if (ptr+8 <= end && memeq(end-8, "Infinity", 8))
	{
		if (*ptr == '-') out = -HUGE_VAL;
		else out = HUGE_VAL;
		return true;
	}
	if (ptr+3 <= end && memeq(end-3, "NaN", 3))
	{
		out = NAN;
		return true;
	}
	
	intmax_t tmp;
	if (!parse_num_as_signed(str, tmp))
		return false;
	out = tmp;
	return true;
}
static bool parse_num_as_signless(const char * ptr, const char * end, uintmax_t& out)
{
	if (ptr+2 <= end && (ptr[1] == 'x' || ptr[1] == 'X'))
		return fromstringhex_ptr(ptr+2, end, out);
	else
		return fromstring_ptr(ptr, end, out);
}
bool json5parser::parse_num_as_unsigned(cstring str, uintmax_t& out)
{
	out = 0;
	const char * ptr = str.ptr_raw();
	const char * end = str.ptr_raw_end();
	if (ptr == end) return false;
	
	if (*ptr == '-') return false;
	else if (*ptr == '+') ptr++;
	
	return parse_num_as_signless(ptr, end, out);
}
bool json5parser::parse_num_as_signed(cstring str, intmax_t& out)
{
	out = 0;
	const char * ptr = str.ptr_raw();
	const char * end = str.ptr_raw_end();
	if (ptr == end) return false;
	
	bool negative = false;
	if (*ptr == '+') ptr++;
	else if (*ptr == '-') { negative=true; ptr++; }
	
	uintmax_t tmp;
	if (!parse_num_as_signless(ptr, end, tmp)) return false;
	out = (intmax_t)tmp;
	if (out < 0) return false;
	
	if (negative) out = -out;
	return true;
}


string jsonwriter::strwrap(cstring s)
{
	const uint8_t * sp = s.bytes().ptr();
	const uint8_t * spe = sp + s.length();
	
	string out = "\"";
	const uint8_t * previt = sp;
	for (const uint8_t * it = sp; it < spe; it++)
	{
#ifdef __SSE2__
		if (spe-it >= 16)
		{
			__m128i chs = _mm_loadu_si128((__m128i*)it);
			
			__m128i bad1 = _mm_cmplt_epi8(_mm_xor_si128(chs, _mm_set1_epi8(0x82)), _mm_set1_epi8((int8_t)(0x21^0x80)));
			__m128i bad2 = _mm_or_si128(_mm_cmpeq_epi8(chs, _mm_set1_epi8(0x7F)), _mm_cmpeq_epi8(chs, _mm_set1_epi8(0x5C)));
			__m128i bad = _mm_or_si128(bad1, bad2);
			int mask = _mm_movemask_epi8(bad);
			if (mask == 0)
			{
				it += 16-1; // -1 for the it++ above (oddly enough, this way is faster)
				continue;
			}
			it += ctz32(mask);
		}
#endif
		
		uint8_t c = *it;
		// DEL is legal according to nst/JSONTestSuite, but let's avoid it anyways
		if ((c^2) <= 32 || c == '\\' || c == 0x7F)
		{
			out += arrayview<uint8_t>(previt, it-previt);
			previt = it+1;
			
			if(0);
			else if (c=='\n') out += "\\n";
			else if (c=='\r') out += "\\r";
			else if (c=='\t') out += "\\t";
			else if (c=='\b') out += "\\b";
			else if (c=='\f') out += "\\f";
			else if (c=='\"') out += "\\\"";
			else if (c=='\\') out += "\\\\";
			else out += "\\u"+tostringhex<4>(c);
		}
	}
	out += arrayview<uint8_t>(previt, spe-previt);
	return out+"\"";
}

void jsonwriter::comma()
{
	if (m_comma) m_data += ',';
	m_comma = true;
	
	if (UNLIKELY(!m_indent_disable))
	{
		if (m_indent_is_value)
		{
			m_data += ' ';
			m_indent_is_value = false;
		}
		else if (m_indent_depth)
		{
			cstring indent_str = (&"        "[8-m_indent_size]);
			m_data += '\n';
			for (int i=0;i<m_indent_depth;i++)
			{
				m_data += indent_str;
			}
		}
	}
}

void jsonwriter::null()
{
	comma();
	m_data += "null";
}
void jsonwriter::boolean(bool b)
{
	comma();
	m_data += b ? "true" : "false";
}
void jsonwriter::str(cstring s)
{
	comma();
	m_data += strwrap(s);
}
void jsonwriter::list_enter()
{
	comma();
	m_data += "[";
	m_comma = false;
	m_indent_depth++;
}
void jsonwriter::list_exit()
{
	m_data += "]";
	m_comma = true;
	m_indent_depth--;
}
void jsonwriter::map_enter()
{
	comma();
	m_data += "{";
	m_comma = false;
	m_indent_depth++;
}
void jsonwriter::map_key(cstring s)
{
	str(s);
	m_data += ":";
	m_comma = false;
	m_indent_is_value = true;
}
void jsonwriter::map_exit()
{
	m_data += "}";
	m_comma = true;
	m_indent_depth--;
}



JSONw JSONw::c_null;
map<string,JSONw> JSONw::c_null_map;

void JSONw::construct(jsonparser& p, jsonparser::event& ev, bool* ok, size_t maxdepth)
{
	if(0);
	else if (ev.type == jsonparser::unset) set_to<jsonparser::unset>();
	else if (ev.type == jsonparser::jtrue) set_to<jsonparser::jtrue>();
	else if (ev.type == jsonparser::jfalse) set_to<jsonparser::jfalse>();
	else if (ev.type == jsonparser::jnull) set_to<jsonparser::jnull>();
	else if (ev.type == jsonparser::str) set_to<jsonparser::str>(ev.str);
	else if (ev.type == jsonparser::num) set_to<jsonparser::num>(ev.str);
	else if (ev.type == jsonparser::error) { set_to<jsonparser::error>(); *ok = false; }
	else if (maxdepth == 0)
	{
		set_to<jsonparser::error>();
		*ok = false;
		if (ev.type == jsonparser::enter_list || ev.type == jsonparser::enter_map)
		{
			// it would be faster to reset the jsonparser somehow,
			// but performance on hostile inputs isn't really a priority
			size_t xdepth = 1;
			while (xdepth)
			{
				jsonparser::event next = p.next();
				if (next.type == jsonparser::enter_list) xdepth++;
				if (next.type == jsonparser::enter_map) xdepth++;
				if (next.type == jsonparser::exit_list) xdepth--;
				if (next.type == jsonparser::exit_map) xdepth--;
			}
		}
		return;
	}
	else if (ev.type == jsonparser::enter_list)
	{
		set_to<jsonparser::enter_list>();
		array<JSONw>& children = content.get<jsonparser::enter_list>();
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.type == jsonparser::exit_list) break;
			if (next.type == jsonparser::error) *ok = false;
			children.append().construct(p, next, ok, maxdepth-1);
		}
	}
	else if (ev.type == jsonparser::enter_map)
	{
		set_to<jsonparser::enter_map>();
		map<string,JSONw>& children = content.get<jsonparser::enter_map>();
		while (true)
		{
			jsonparser::event next = p.next();
			if (next.type == jsonparser::exit_map) break;
			if (next.type == jsonparser::map_key)
			{
				jsonparser::event ev = p.next();
				children.insert(next.str).construct(p, ev, ok, maxdepth-1);
			}
			if (next.type == jsonparser::error) *ok = false;
		}
	}
}

bool JSON::parse(string s)
{
	// it would not do to have some function changing the value of null for everybody else
	// this check is only needed here; all of JSON's other members are conceptually const, though most aren't marked as such
	if (this == &c_null)
		abort();
	
	jsonparser p(std::move(s));
	bool ok = true;
	jsonparser::event ev = p.next();
	construct(p, ev, &ok, 1000);
	
	ev = p.next();
	if (!ok || ev.type != jsonparser::finish)
	{
		// this discards the entire object
		// but considering how rare damaged json is, that's acceptable
		set_to<jsonparser::error>();
		return false;
	}
	return true;
}

template<bool sort>
void JSON::serialize(jsonwriter& w) const
{
	switch (type())
	{
	case jsonparser::unset:
	case jsonparser::error:
	case jsonparser::jnull:
		w.null();
		break;
	case jsonparser::jtrue:
		w.boolean(true);
		break;
	case jsonparser::jfalse:
		w.boolean(false);
		break;
	case jsonparser::str:
		w.str(content.get<jsonparser::str>());
		break;
	case jsonparser::num:
		w.num_unsafe(content.get<jsonparser::num>());
		break;
	case jsonparser::enter_list:
		w.list_enter();
		for (const JSON& j : list())
			j.serialize<sort>(w);
		w.list_exit();
		break;
	case jsonparser::enter_map:
		w.map_enter();
		if (sort)
		{
			array<const map<string,JSONw>::node*> items;
			for (const map<string,JSONw>::node& e : assoc())
			{
				items.append(&e);
			}
			items.ssort([](const map<string,JSONw>::node* a, const map<string,JSONw>::node* b) { return string::less(a->key, b->key); });
			for (const map<string,JSONw>::node* e : items)
			{
				w.map_key(e->key);
				e->value.serialize<sort>(w);
			}
		}
		else
		{
			for (const map<string,JSONw>::node& e : assoc())
			{
				w.map_key(e.key);
				e.value.serialize<sort>(w);
			}
		}
		w.map_exit();
		break;
	default: abort(); // unreachable
	}
}

string JSON::serialize(int indent) const
{
	jsonwriter w(indent);
	serialize<false>(w);
	return w.finish();
}

string JSON::serialize_sorted(int indent) const
{
	jsonwriter w(indent);
	serialize<true>(w);
	return w.finish();
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
"[ 1, 2.5e+1, 3, 4.5e+0, 6.7e+08 ]"
;

static jsonparser::event test2e[]={
	{ e_enter_list },
		{ e_num, "1" },
		{ e_num, "2.5e+1" },
		{ e_num, "3" },
		{ e_num, "4.5e+0" },
		{ e_num, "6.7e+08" },
	{ e_exit_list },
	{ e_finish }
};

static const char * test3 =
"\r\n\t {\r\n\t \"foo\":\r\n\t [\r\n\t true\r\n\t ,\r\n\t false, null\r\n\t ]\r\n\t }\r\n\t "
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
"{ \"a\": [ { \"b\": [ 1, 2 ], \"c\": [ 3, 4 ] }, { \"d\": [ 5, 6 ], "
"\"e\": [ \"this is a 31 byte string aaaaaa\", \"this is a 32 byte string aaaaaaa\", \"this is a 33 byte string aaaaaaaa\" ] } ],\n"
"  \"f\": [ { \"g\": [ 7, 8 ], \"h\": [ 9, 0 ] }, { \"i\": [ 1, \"\xC3\xB8\" ], \"j\": [ {}, \"x\\nx\x7fx\" ] },"
" \"\\\"\\\\\\/\" ] }"
;

static jsonparser::event test4e[]={
	{ e_enter_map },
		{ e_map_key, "a" },
		{ e_enter_list },
			{ e_enter_map },
				{ e_map_key, "b" },
				{ e_enter_list }, { e_num, "1" }, { e_num, "2" }, { e_exit_list },
				{ e_map_key, "c" },
				{ e_enter_list }, { e_num, "3" }, { e_num, "4" }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "d" },
				{ e_enter_list }, { e_num, "5" }, { e_num, "6" }, { e_exit_list },
				{ e_map_key, "e" },
				{ e_enter_list },
					{ e_str, "this is a 31 byte string aaaaaa" },
					{ e_str, "this is a 32 byte string aaaaaaa" },
					{ e_str, "this is a 33 byte string aaaaaaaa" },
				{ e_exit_list },
			{ e_exit_map },
		{ e_exit_list },
		{ e_map_key, "f" },
		{ e_enter_list },
			{ e_enter_map },
				{ e_map_key, "g" },
				{ e_enter_list }, { e_num, "7" }, { e_num, "8" }, { e_exit_list },
				{ e_map_key, "h" },
				{ e_enter_list }, { e_num, "9" }, { e_num, "0" }, { e_exit_list },
			{ e_exit_map },
			{ e_enter_map },
				{ e_map_key, "i" },
				{ e_enter_list }, { e_num, "1" }, { e_str, "\xC3\xB8" }, { e_exit_list },
				{ e_map_key, "j" },
				{ e_enter_list }, { e_enter_map }, { e_exit_map }, { e_str, "x\nx\x7fx" }, { e_exit_list },
			{ e_exit_map },
		{ e_str, "\"\\/" },
		{ e_exit_list },
	{ e_exit_map },
	{ e_finish }
};

static const char * test5 =
"{ \"foo\": \"\xC2\x80\\u0080\\ud83d\\udCA9\" }\n"
;

static jsonparser::event test5e[]={
	{ e_enter_map },
		{ e_map_key, "foo" },
		{ e_str, "\xC2\x80\xC2\x80\xF0\x9F\x92\xA9" },
	{ e_exit_map },
	{ e_finish }
};

static const char * test6 =
"\"#\\\n#\\\r#\\\r\n#\\\xE2\x80\xA8#\\\xE2\x80\xA9#\\\xE2\x80\xAA#\""
;

static jsonparser::event test6e[]={
	{ e_str, "######\xE2\x80\xAA#" },
	{ e_finish }
};

template<typename T>
static void testjson(cstring json, jsonparser::event* expected)
{
	T parser(json);
	while (true)
	{
		jsonparser::event actual = parser.next();
		
//printf("e=%d [%s]\n", expected->type, (const char*)expected->str.c_str());
//printf("a=%d [%s]\n\n", actual.type,  (const char*)actual.str.c_str());
		if (expected)
		{
			assert_eq(actual.type, expected->type);
			assert_eq(actual.str, expected->str);
			
			if (expected->type == e_finish) return;
		}
		if (actual.type == e_finish) return;
		
		expected++;
	}
}

template<typename T, bool valid=false>
static void testjson_error(cstring json)
{
	T parser(json);
	array<int> events;
	while (true)
	{
		jsonparser::event ev = parser.next();
		events.append(ev.type);
		assert_lt(events.size(), 1000); // fail on infinite error loops
		if (ev.type == e_finish) break;
	}
	assert_eq(events.contains(e_error), !valid); // any error is fine
	
	array<int> events1 = events;
	for (ssize_t n=events.size()-1;n>=0;n--)
	{
		if (events[n] == e_error)
			events.remove(n);
		if (events[n] == e_str || events[n] == e_num || events[n] == e_jtrue || events[n] == e_jfalse)
			events[n] = e_jnull;
		while (events[n] == e_enter_list && events[n+1] == e_jnull)
		{
			events.remove(n+1);
		}
		if (events[n] == e_enter_list && events[n+1] == e_exit_list)
		{
			events[n] = e_jnull;
			events.remove(n+1);
		}
		while (events[n] == e_enter_map && events[n+1] == e_map_key && events[n+2] == e_jnull)
		{
			events.remove(n+1);
			events.remove(n+1);
		}
		if (events[n] == e_enter_map && events[n+1] == e_exit_map)
		{
			events[n] = e_jnull;
			events.remove(n+1);
		}
	}
	if (events[0] == e_jnull)
		events.remove(0);
	else
		assert(!valid);
	assert(events[0] == e_finish);
	
	//enum {
		//unset      = 0,
		//jtrue      = 1,
		//jfalse     = 2,
		//jnull      = 3,
		//str        = 4,
		//num        = 5,
		//enter_list = 6,
		//exit_list  = 7,
		//enter_map  = 8,
		//map_key    = 9,
		//exit_map   = 10,
		//error      = 11,
		//finish     = 12,
	//};
	
	//string evs_str = tostring_dbg(events);
	//if (evs_str == "[3,12]") {}
	//else if (!valid && evs_str == "[12]") {}
	//else
	//{
		//puts(tostring_dbg(events1)+tostring_dbg(events)+json);
		//assert(false);
	//}
}

template<typename T, bool json5>
static void testjson_all()
{
	testcall(testjson<T>(test1, test1e));
	testcall(testjson<T>(test2, test2e));
	testcall(testjson<T>(test3, test3e));
	testcall(testjson<T>(test4, test4e));
	testcall(testjson<T>(test5, test5e));
	if (json5)
		testcall(testjson<T>(test6, test6e));
	
	testcall(testjson_error<T>(""));
	testcall(testjson_error<T>("           "));
	testcall(testjson_error<T>("{"));
	testcall(testjson_error<T>("{\"a\""));
	testcall(testjson_error<T>("{\"a\":"));
	testcall(testjson_error<T>("{\"a\":1"));
	testcall(testjson_error<T>("{\"a\":1,"));
	testcall(testjson_error<T>("{\"a\":}"));
	testcall(testjson_error<T>("["));
	testcall(testjson_error<T>("[1"));
	testcall(testjson_error<T>("[1,"));
	testcall(testjson_error<T>("\""));
	testcall(testjson_error<T>("01"));
	testcall(testjson_error<T>("1,2,3"));
	testcall(testjson_error<T>("1.23.4"));
	testcall(testjson_error<T>("0x120x34"));
	testcall(testjson_error<T>("1e"));
	testcall(testjson_error<T>("1e+"));
	testcall(testjson_error<T>("1e-"));
	testcall(testjson_error<T>("z"));
	testcall(testjson_error<T>("{ \"a\":1, \"b\":2, \"q\":*, \"a\":3, \"a\":4 }"));
	testcall(testjson_error<T>("\""));
	testcall(testjson_error<T>("\"\\"));
	testcall(testjson_error<T>("\"\\u"));
	testcall(testjson_error<T>("\"\\u1"));
	testcall(testjson_error<T>("\"\\u12"));
	testcall(testjson_error<T>("\"\\u123"));
	testcall(testjson_error<T>("\"\\u1234"));
	
	//try to make it read out of bounds
	//input length 31
	testcall(testjson_error<T>("\"this is a somewhat longer str\\u"));
	testcall(testjson_error<T>("\"this is a somewhat longer st\\u1"));
	testcall(testjson_error<T>("\"this is a somewhat longer s\\u12"));
	testcall(testjson_error<T>("\"this is a somewhat longer \\u123"));
	testcall(testjson_error<T>("\"this is a somewhat longer\\u1234"));
	//input length 32
	testcall(testjson_error<T>("\"this is a somewhat longer stri\\u"));
	testcall(testjson_error<T>("\"this is a somewhat longer str\\u1"));
	testcall(testjson_error<T>("\"this is a somewhat longer st\\u12"));
	testcall(testjson_error<T>("\"this is a somewhat longer s\\u123"));
	testcall(testjson_error<T>("\"this is a somewhat longer \\u1234"));
	//input length 15
	testcall(testjson_error<T>("\"short string \\u"));
	testcall(testjson_error<T>("\"short string\\u1"));
	testcall(testjson_error<T>("\"short strin\\u12"));
	testcall(testjson_error<T>("\"short stri\\u123"));
	testcall(testjson_error<T>("\"short str\\u1234"));
	testcall(testjson_error<T>("                    f"));
	testcall(testjson_error<T>("                    fa"));
	testcall(testjson_error<T>("                    fal"));
	testcall(testjson_error<T>("                    fals"));
	testcall(testjson_error<T>("                    t"));
	testcall(testjson_error<T>("                    tr"));
	testcall(testjson_error<T>("                    tru"));
	testcall(testjson_error<T>("                    n"));
	testcall(testjson_error<T>("                    nu"));
	testcall(testjson_error<T>("                    nul"));
	
	testcall(testjson_error<T>("[]\1"));
	testcall(testjson_error<T>("[],"));
	testcall(testjson_error<T>("123,"));
	testcall(testjson_error<T>("[1,,2]"));
	
	testcall(testjson_error<T>("123"+string::nul()));
	testcall(testjson_error<T>("[]"+string::nul()));
	
	testcall(testjson_error<T>("{                   \"\\uDBBB\\uDB"));
	
	testcall(testjson_error<T,json5>("1.")); // invalid JSON, but legal JSON5
	testcall(testjson_error<T,json5>("1 // comment"));
	testcall(testjson_error<T,json5>("1 // comment\n"));
	testcall(testjson_error<T,json5>("// comment\n1 // comment"));
	testcall(testjson_error<T,json5>("1 /* comment */"));
	testcall(testjson_error<T,json5>("/* comment */ 1"));
	testcall(testjson_error<T,json5>("\"\\x41\""));
	testcall(testjson_error<T,json5>("{unquoted:1}"));
	testcall(testjson_error<T,json5>("'single quoted'"));
	testcall(testjson_error<T,json5>("\"line\\\nbreak\""));
	testcall(testjson_error<T,json5>("\"line\\\rbreak\""));
	testcall(testjson_error<T,json5>("\"line\\\r\nbreak\""));
	testcall(testjson_error<T,json5>("\"#\\\xE2\x80\xA8#\\\xE2\x80\xA9#\""));
	testcall(testjson_error<T,json5>("-Infinity"));
	testcall(testjson_error<T,json5>("+NaN"));
	testcall(testjson_error<T,json5>("1\f\v"));
	testcall(testjson_error<T,json5>("0x42"));
	testcall(testjson_error<T,json5>("+1"));
	testcall(testjson_error<T,json5>("[\f]"));
	testcall(testjson_error<T,json5>("[\v]"));
	testcall(testjson_error<T,json5>("[\"\t\"]"));
	testcall(testjson_error<T,json5>("\"\\v\""));
	testcall(testjson_error<T,json5>("[1,]"));
	testcall(testjson_error<T,json5>("{\"a\":0,}"));
	testcall(testjson_error<T,json5>("[-.123]"));
	testcall(testjson_error<T,json5>("[.123]"));
	testcall(testjson_error<T,json5>("[123.,]"));
	testcall(testjson_error<T,json5>("['\"',\"'\"]"));
	testcall(testjson_error<T,json5>("\"\\U0001f4a9\"")); // though some of them really shouldn't be...
	testcall(testjson_error<T>("{unquoted☃:1}"));
	testcall(testjson_error<T>("{not-valid:1}"));
	testcall(testjson_error<T>("5 /*"));
	testcall(testjson_error<T,json5>("\"a\\0b\""));
	testcall(testjson_error<T>("\"a\\01b\""));
	testcall(testjson_error<T>("\"a\\1b\""));
	testcall(testjson_error<T,json5>(R"({
  // comments
  unquoted: 'and you can quote me on that',
  singleQuotes: 'I can use "double quotes" here',
  lineBreaks: "Look, Mom! \
No \\n's!",
  hexadecimal: 0xdecaf,
  leadingDecimalPoint: .8675309, andTrailing: 8675309.,
  positiveSign: +1,
  trailingComma: 'in objects', andIn: ['arrays',],
  "backwardsCompatible": "with JSON",
}
)"));
}

test("JSON parser", "string", "json") { testjson_all<jsonparser, false>(); }
test("JSON5 parser", "string", "json") { testjson_all<json5parser, true>(); }

test("JSON container", "string,array,set", "json")
{
	{
		JSON json("7");
		assert_eq((int)json, 7);
	}
	
	{
		JSON json("\"42\"");
		assert_eq(json.str(), "42");
	}
	
	{
		JSON json("                    false");
		assert_eq(json.type(), jsonparser::jfalse);
	}
	{
		JSON json("                    true");
		assert_eq(json.type(), jsonparser::jtrue);
	}
	{
		JSON json("                    null");
		assert_eq(json.type(), jsonparser::jnull);
	}
	
	{
		JSON json("[1,2,3]");
		assert_eq((int)json[0], 1);
		assert_eq((int)json[1], 2);
		assert_eq((int)json[2], 3);
	}
	
	{
		JSON json("{\"a\":null,\"b\":true,\"c\":false}");
		assert_eq((bool)json["a"], false);
		assert_eq((bool)json["b"], true);
		assert_eq((bool)json["c"], false);
	}
	
	{
		JSON("["); // these pass if they do not yield infinite loops
		JSON("[{}");
		JSON("[[]");
		JSON("[{},");
		JSON("[[],");
		JSON("{");
		JSON("{\"x\"");
		JSON("{\"x\":");
	}
	
	{
		JSONw json;
		json["a"] = 1;
		json["b"] = 2;
		json["c"] = 3;
		json["d"] = 4;
		json["e"] = 5;
		json["f"] = 6;
		json["g"] = 7;
		json["h"] = 8;
		json["i"] = 9;
		assert_eq(json.serialize_sorted(), R"({"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9})");
		assert_ne(json.serialize(),        R"({"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9})");
	}
	
	{
		JSONw json;
		json["\x01\x02\x03\n\\n☃\""] = "\x01\x02\x03\n\\n☃\"";
		assert_eq(json.serialize(), R"({"\u0001\u0002\u0003\n\\n☃\"":"\u0001\u0002\u0003\n\\n☃\""})");
	}
	
	{
		JSONw json;
		json[0] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
		json[1] = "aaaaaaaa\"aaaaaaaaaaaaaaaaaaaaaaa";
		json[2] = "aaaaaaaa\x1f""aaaaaaaaaaaaaaaaaaaaaaa";
		json[3] = "aaaaaaaa aaaaaaaaaaaaaaaaaaaaaaa";
		json[4] = "aaaaaaaa\\aaaaaaaaaaaaaaaaaaaaaaa";
		json[5] = 123;
		assert_eq((cstring)json[0], "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		assert_eq((double)json[5], 123.0);
		assert_eq((size_t)json[5], 123);
		assert_eq(json.serialize(), R"(["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaa\"aaaaaaaaaaaaaaaaaaaaaaa",)"
		           R"("aaaaaaaa\u001Faaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaa aaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaa\\aaaaaaaaaaaaaaaaaaaaaaa",123])");
	}
	
	if (false) // disabled, JSON::construct runs through all 200000 events from the jsonparser which is slow
	{
		char spam[200001];
		for (int i=0;i<100000;i++)
		{
			spam[i] = '[';
			spam[i+100000] = ']';
		}
		spam[200000] = '\0';
		JSON((char*)spam); // must not overflow the stack (pointless cast to avoid some c++ language ambiguity)
	}
}
#endif
