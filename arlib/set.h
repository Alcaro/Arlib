#pragma once
#include "global.h"
#include "array.h"
#include "hash.h"

//#include"stringconv.h"

template<typename T>
class set {
	//this is a hashtable, using open addressing and quadratic probing
	enum { i_empty, i_deleted };
	struct alignas(T) maybe_T {
	public:
		char bytes[sizeof(T)];
		T* data() { return (T*)bytes; }
		const T* data() const { return (T*)bytes; }
		
		template<typename... Args>
		void enable(Args&&... args)
		{
			new(data()) T(std::forward<Args>(args)...);
		}
		
		void disable(char tag)
		{
			data()->~T();
			bytes[0] = tag;
		}
		
		T& member()
		{
			return *data();
		}
		
		const T& member() const
		{
			return *data();
		}
		
		//set to i_empty (never used) or i_deleted (used)
		char tag()
		{
			return bytes[0];
		}
	};
	
	array<maybe_T> m_data;
	array<bool> m_valid;
	size_t m_count;
	
	void rehash(size_t newsize)
	{
//debug("rehash pre");
		array<maybe_T> prev_data = std::move(m_data);
		array<bool> prev_valid = std::move(m_valid);
		
		m_data.reset();
		m_valid.reset();
		m_data.resize(newsize);
		m_valid.resize(newsize);
		
		for (size_t i=0;i<prev_data.size();i++)
		{
			if (!prev_valid[i]) continue;
			
			size_t pos = find_pos(prev_data[i].member());
			memcpy(m_data[pos].bytes, prev_data[i].bytes, sizeof(maybe_T::bytes));
			m_valid[pos] = true;
		}
//debug("rehash post");
	}
	
	void grow()
	{
		// half full -> rehash
		if (m_count >= m_data.size()/2) rehash(m_data.size()*2);
	}
	
	bool slot_empty(size_t pos)
	{
		return !m_valid[pos];
	}
	bool slot_deleted(size_t pos)
	{
		return !m_valid[pos] && m_data[pos].invalid_kind==1;
	}
	
	template<typename T2>
	size_t find_pos(const T2& item)
	{
		size_t hashv = hash(item);
		size_t i = 0;
		
		size_t emptyslot = -1;
		
		while (true)
		{
			//http://stackoverflow.com/a/15770445 says this is the optimal distribution function
			//testing up to 65536 confirms this
			size_t pos = (hashv + (i+1)*i/2) % m_data.size();
			if (m_valid[pos] && m_data[pos].member()==item) return pos;
			if (!m_valid[pos])
			{
				if (emptyslot == (size_t)-1) emptyslot = pos;
				if (m_data[pos].tag() == i_empty) return emptyslot;
			}
			i++;
			if (i == m_data.size())
			{
				//happens if all slots contain 'something was here' placeholders
				return emptyslot;
			}
		}
	}
	
	template<typename,typename>
	friend class map;
	//used by map
	//if the item doesn't exist, NULL
	template<typename T2>
	T* get(const T2& item)
	{
		size_t pos = find_pos(item);
		if (m_valid[pos]) return &m_data[pos].member();
		else return NULL;
	}
	//also used by map
	template<typename T2>
	T& get_create(const T2& item)
	{
		grow();
		
		size_t pos = find_pos(item);
		
		if (!m_valid[pos])
		{
			m_data[pos].enable(item);
			m_valid[pos] = true;
			m_count++;
		}
		
		return m_data[pos].member();
	}
	
	void construct()
	{
		m_data.resize(8);
		m_valid.resize(8);
		m_count = 0;
	}
	void construct(const set& other)
	{
		m_data = other.m_data;
		m_valid = other.m_valid;
		m_count = other.m_count;
		
		for (size_t i=0;i<m_data.size();i++)
		{
			if (m_valid[i])
			{
				const array<maybe_T>& z = other.m_data;
				const maybe_T& y = z[i];
				const T& x = y.member();
				m_data[i].enable(x);
				//m_data[i].enable(other.m_data[i].member());
			}
		}
	}
	void construct(set&& other)
	{
		m_data = other.m_data;
		m_valid = other.m_valid;
		m_count = other.m_count;
		
		other.m_data.reset();
		other.m_valid.reset();
	}
	
	void destruct()
	{
		for (size_t i=0;i<m_data.size();i++)
		{
			if (m_valid[i]) m_data[i].disable(0);
			m_valid[i] = false;
		}
		m_count = 0;
	}
	
public:
	set() { construct(); }
	set(const set& other) { construct(other); }
	set(set&& other) { construct(other); }
	set& operator=(const set& other) { destruct(); construct(other); }
	set& operator=(set&& other) { destruct(); construct(other); }
	~set() { destruct(); }
	
	template<typename T2>
	void add(const T2& item)
	{
		get_create(item);
	}
	
	template<typename T2>
	bool contains(const T2& item)
	{
		size_t pos = find_pos(item);
		return m_valid[pos];
	}
	
	template<typename T2>
	void remove(const T2& item)
	{
		size_t pos = find_pos(item);
		
		if (m_valid[pos])
		{
			m_data[pos].disable(1);
			m_valid[pos] = false;
			m_count--;
		}
	}
	
	size_t size() { return m_count; }
	
	void reset() { destruct(); }
	
private:
	class iterator {
		friend class set;
		
		set* parent;
		size_t pos;
		
		void to_valid()
		{
			while (!parent->m_valid[pos])
			{
				pos++;
				//use -1 to ensure iterating a map while shrinking it doesn't go OOB
				if (pos >= parent->m_valid.size())
				{
					pos = -1;
					break;
				}
			}
		}
		
		iterator(set<T>* parent, size_t pos) : parent(parent), pos(pos)
		{
			if (pos != (size_t)-1) to_valid();
		}
		
	public:
		
		const T& operator*()
		{
			return parent->m_data[pos].member();
		}
		
		iterator& operator++()
		{
			pos++;
			to_valid();
			return *this;
		}
		
		bool operator!=(const iterator& other)
		{
			return (this->parent != other.parent || this->pos != other.pos);
		}
	};
	
public:
	//messing with the set during iteration half-invalidates all iterators
	//a half-invalid iterator may return values you've already seen and may skip values, but will not crash or loop forever
	//exception: you may not dereference a half-invalid iterator, use operator++ first
	iterator begin() { return iterator(this, 0); }
	iterator end() { return iterator(this, -1); }

//string debug_node(int n) { return tostring(n); }
//string debug_node(string& n) { return n; }
//template<typename T2> string debug_node(T2& n) { return "?"; }
//void debug(const char * why)
//{
//puts("---");
//for (size_t i=0;i<m_data.size();i++)
//{
//	printf("%s %lu: valid %i, tag %i, data %s, found slot %lu\n",
//		why, i, (bool)m_valid[i], m_data[i].tag(), (const char*)debug_node(m_data[i].member()), find_pos(m_data[i].member()));
//}
//puts("---");
//}
};



template<typename Tkey, typename Tvalue>
class map {
	struct node_ref {
		const Tkey* key;
		const Tvalue* value;
		
		node_ref(const Tkey* key, const Tvalue* value) : key(key), value(value) {}
		
		size_t hash() { return ::hash(*key); }
	};
public:
	struct node {
		const Tkey key;
		Tvalue value;
		
		node(Tkey other) : key(other), value() {}
		node(node_ref other) : key(*other.key), value(*other.value) {}
		
		//allow nodes to be passed around by map users
		node() : key(), value() {}
		
		size_t hash() { return ::hash(key); }
		bool operator==(const Tkey& other) { return key == other; }
		bool operator==(const node_ref& other) { return key == *other.key; }
		bool operator==(const node& other) { return key == other.key; }
	};
private:
	set<node> items;
	
public:
	//can't call it set(), conflict with set<>
	void insert(const Tkey& key, const Tvalue& value)
	{
		items.add(node_ref(&key, &value)); // use node_ref rather than node, to avoid an unnecessary copy/move
	}
	
	//if nonexistent, undefined behavior
	Tvalue& get(const Tkey& key)
	{
		return items.get(key)->value;
	}
	
	//if nonexistent, returns the argument
	Tvalue& get_or(const Tkey& key, Tvalue& def)
	{
		node* ret = items.get(key);
		if (ret) return ret->value;
		else return def;
	}
	Tvalue get_or(const Tkey& key, Tvalue def)
	{
		node* ret = items.get(key);
		if (ret) return ret->value;
		else return def;
	}
	
	Tvalue& insert(const Tkey& key)
	{
		return items.get_create(key).value;
	}
	
	bool contains(const Tkey& item)
	{
		return items.contains(item);
	}
	
	void remove(const Tkey& item)
	{
		items.remove(item);
	}
	
	size_t size() { return items.size(); }
	
private:
	class iterator {
		typename set<node>::iterator it;
	public:
		
		iterator(typename set<node>::iterator it) : it(it) {}
		
		node& operator*()
		{
			return const_cast<node&>(*it);
		}
		
		iterator& operator++()
		{
			++it;
			return *this;
		}
		
		bool operator!=(const iterator& other)
		{
			return it != other.it;
		}
	};
	
public:
	//messing with the map during iteration half-invalidates all iterators
	//a half-invalid iterator may return values you've already seen and may skip values, but will not crash or loop forever
	//exception: you may not dereference a half-invalid iterator, use operator++ first
	
	iterator begin() { return items.begin(); }
	iterator end() { return items.end(); }
	
	template<typename Ts>
	void serialize(Ts& s)
	{
		if (s.serializing)
		{
			for (node& p : *this)
			{
				s.item(tostring(p.key), p.value);
			}
		}
		else
		{
			Tkey tmpk;
			if (fromstring(s.next(), tmpk))
			{
				Tvalue& tmpv = items.get_create(tmpk).value;
				s.item(s.next(), tmpv);
			}
		}
	}
};
