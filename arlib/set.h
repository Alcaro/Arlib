#pragma once
#include "global.h"
#include "array.h"
#include "hash.h"
#include "linqbase.h"

template<typename T>
class set : public linqbase<T, set<T>> {
	//this is a hashtable, using open addressing and linear probing
	enum { i_empty, i_deleted };
	
	T* m_data_;
	uint8_t& tag(size_t id) { return *(uint8_t*)(m_data_+id); }
	array<bool> m_valid;
	size_t m_count;
	
	void rehash(size_t newsize)
	{
//debug("rehash pre");
		T* prev_data = m_data_;
		array<bool> prev_valid = std::move(m_valid);
		
		m_data_ = calloc(newsize, sizeof(T));
		m_valid.reset();
		m_valid.resize(newsize);
		
		for (size_t i=0;i<prev_valid.size();i++)
		{
			if (!prev_valid[i]) continue;
			
			size_t pos = find_pos(prev_data[i]);
			memcpy(&m_data_[pos], &prev_data[i], sizeof(T));
			m_valid[pos] = true;
		}
		free(prev_data);
//debug("rehash post");
	}
	
	void grow()
	{
		// half full -> rehash
		if (m_count >= m_valid.size()/2) rehash(m_valid.size()*2);
	}
	
	bool slot_empty(size_t pos)
	{
		return !m_valid[pos];
	}
	bool slot_deleted(size_t pos)
	{
		return !m_valid[pos] && tag(pos)==i_deleted;
	}
	
	template<typename T2>
	size_t find_pos(const T2& item)
	{
		size_t hashv = hash_shuffle(hash(item)) % m_valid.size();
		size_t i = 0;
		
		size_t emptyslot = -1;
		
		while (true)
		{
			//I could use hashv + i+(i+1)/2 <http://stackoverflow.com/a/15770445>
			//but due to hash_shuffle, it hurts as much as it helps.
			size_t pos = (hashv + i) % m_valid.size();
			if (m_valid[pos] && m_data_[pos]==item) return pos;
			if (!m_valid[pos])
			{
				if (emptyslot == (size_t)-1) emptyslot = pos;
				if (tag(pos) == i_empty) return emptyslot;
			}
			i++;
			if (i == m_valid.size())
			{
				//happens if all slots contain 'something was here' placeholders
				rehash(m_valid.size());
				//can't use emptyslot, it may no longer be empty
				//guaranteed to not be an infinite loop, there's always at least one empty slot
				return find_pos(item);
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
		if (m_valid[pos]) return &m_data_[pos];
		else return NULL;
	}
	//also used by map
	template<typename T2>
	T& get_create(const T2& item)
	{
		size_t pos = find_pos(item);
		
		if (!m_valid[pos])
		{
			grow();
			pos = find_pos(item); // recalculate this, grow() may have moved it
			//do not move earlier, grow() invalidates references and get_create(item_that_exists) is not allowed to do that
			
			new(&m_data_[pos]) T(item);
			m_valid[pos] = true;
			m_count++;
		}
		
		return m_data_[pos];
	}
	
	void construct()
	{
		m_data_ = calloc(8, sizeof(T));
		m_valid.resize(8);
		m_count = 0;
	}
	void construct(const set& other)
	{
		m_data_ = calloc(other.m_valid.size(), sizeof(T));
		m_valid = other.m_valid;
		m_count = other.m_count;
		
		for (size_t i=0;i<m_valid.size();i++)
		{
			if (m_valid[i])
			{
				new(&m_data_[i]) T(other.m_data_[i]);
			}
		}
	}
	void construct(set&& other)
	{
		m_data_ = std::move(other.m_data_);
		m_valid = std::move(other.m_valid);
		m_count = std::move(other.m_count);
		
		other.construct();
	}
	
	void destruct()
	{
		for (size_t i=0;i<m_valid.size();i++)
		{
			if (m_valid[i])
			{
				m_data_[i].~T();
			}
		}
		m_count = 0;
		free(m_data_);
		m_valid.reset();
	}
	
public:
	set() { construct(); }
	set(const set& other) { construct(other); }
	set(set&& other) { construct(std::move(other)); }
	set(std::initializer_list<T> c)
	{
		construct();
		for (const T& item : c) add(item);
	}
	set& operator=(const set& other) { destruct(); construct(other); }
	set& operator=(set&& other) { destruct(); construct(std::move(other)); }
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
			m_data_[pos].~T();
			tag(pos) = i_deleted;
			m_valid[pos] = false;
			m_count--;
			if (m_count < m_valid.size()/4 && m_valid.size() > 8) rehash(m_valid.size()/2);
		}
	}
	
	size_t size() const { return m_count; }
	
	void reset() { destruct(); construct(); }
	
	class iterator {
		friend class set;
		
		const set* parent;
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
		
		iterator(const set<T>* parent, size_t pos) : parent(parent), pos(pos)
		{
			if (pos != (size_t)-1) to_valid();
		}
		
	public:
		
		const T& operator*()
		{
			return parent->m_data_[pos];
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
	
	//messing with the set during iteration half-invalidates all iterators
	//a half-invalid iterator may return values you've already seen and may skip values, but will not crash or loop forever
	//exception: you may not dereference a half-invalid iterator, use operator++ first
	//as such, 'for (T i : my_set) { my_set.remove(i); }' is safe (though may keep some instances)
	iterator begin() const { return iterator(this, 0); }
	iterator end() const { return iterator(this, -1); }

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
public:
	struct node {
		const Tkey key;
		Tvalue value;
		
		node() : key(), value() {}
		node(const Tkey& key) : key(key), value() {}
		node(const Tkey& key, const Tvalue& value) : key(key), value(value) {}
		//node(node other) : key(other.key), value(other.value) {}
		
		size_t hash() const { return ::hash(key); }
		bool operator==(const Tkey& other) { return key == other; }
		bool operator==(const node& other) { return key == other.key; }
	};
private:
	set<node> items;
	
public:
	//map() {}
	//map(const map& other) : items(other.items) {}
	//map(map&& other) : items(std::move(other.items)) {}
	//map& operator=(const map& other) { items = other.items; }
	//map& operator=(map&& other) { items = std::move(other.items); }
	//~map() { destruct(); }
	
	//can't call it set(), conflict with set<>
	void insert(const Tkey& key, const Tvalue& value)
	{
		items.add(node(key, value));
	}
	
	//if nonexistent, undefined behavior
	Tvalue& get(const Tkey& key)
	{
		return items.get(key)->value;
	}
	
	//if nonexistent, returns 'def'
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
	Tvalue& get_create(const Tkey& key)
	{
		return items.get_create(key).value;
	}
	Tvalue& operator[](const Tkey& key) { return get_create(key); } // C# does this better...
	
	Tvalue& insert(const Tkey& key)
	{
		return get_create(key);
	}
	
	bool contains(const Tkey& item)
	{
		return items.contains(item);
	}
	
	void remove(const Tkey& item)
	{
		items.remove(item);
	}
	
	void reset()
	{
		items.reset();
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
