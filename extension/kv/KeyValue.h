#pragma once

///////////////
// Key Value //
///////////////
// Usage:
//
// KeyValueRoot kv("My RadKv"); // If you don't want to pass your string in the constructor, use kv.Parse(yourStringHere);
//
// // Writing to the KeyValue
// kv.AddNode("AwesomeNode")->Add("Taco", "Time!"); // Adds the Node "AwesomeNode" {} and gives it a child pair "Taco" "Time!"
// kv.Add("CoolKey", "CoolValue"); // Adds the KeyValue pair "CoolKey" "CoolValue"
//
// // Optimizing speeds
// kv.Solidify(); // Use this if you have a big file and need quicker access times. Warning: It will make the kv read-only!
//
// // Reading from the KeyValue
// printf(kv["AwesomeNode"]["Taco"].value.string); // Accesses the node AwesomeNode's child, Taco, and prints Taco's value
// printf(kv[2].value.string); // Accesses the third pair, CoolKey, and prints its value, CoolValue
//
// // Printing the KeyValue
// char printBuffer[1024];
// kv.ToString(printBuffer, 1024); // Prints 1024 characters of the KeyValue to the buffer for printing
// printf(printBuffer);
//
// Notes:
// NEVER EVER use childCount as a method of checking if a Key Value has children! 
// It shares memory with the length of the string value! USE hasChildren INSTEAD!!

#include <cstddef>
#include <iterator>

enum class KeyValueErrorCode
{
	ERROR_NONE,
	INCOMPLETE_BLOCK,
	UNEXPECTED_START_OF_BLOCK,
	UNEXPECTED_END_OF_BLOCK,
	INCOMPLETE_STRING,
};

// Little helper struct for keeping track of strings
struct kvString_t
{
	char* string;
	size_t length;
};

class KeyValueRoot;

class KeyValue;
template<typename T>
class KeyValuePool;

class KeyValueIterator : public std::iterator<std::input_iterator_tag, KeyValue> {
	KeyValue& kv;
	int index;
public:
	KeyValueIterator(KeyValue& kv, int index) : kv(kv), index(index) {}
	KeyValueIterator& operator++() { ++index; return *this; }
	bool operator!=(const KeyValueIterator& other) const { return index != other.index; }
	KeyValue& operator*();
};

class KeyValue
{
public:
	KeyValue() {};

	KeyValue& operator[](const char* key);
	KeyValue& operator[](size_t index);

	KeyValue& Get(const char* key);
	KeyValue& Get(size_t index);



	// These two only work for classes with children!
	KeyValue* Add(const char* key, const char* value);
	KeyValue* AddNode(const char* key);


	void ToString(char* str, size_t maxLength) { ToString(str, maxLength, 0); if (maxLength > 0) str[0] = '\0'; }
	char* ToString();

	bool IsValid();

	KeyValueIterator begin() { return hasChildren ? KeyValueIterator(*this, 0) : end(); }
	KeyValueIterator end() { return KeyValueIterator(*this, childCount); }

protected:
	// This is used for creating the invalid kv
	// Could be better?
	KeyValue(bool invalid);

	// Deleting should really only be done by the root
	~KeyValue();


	KeyValue* CreateKVPair(kvString_t key, kvString_t string, KeyValuePool<KeyValue>& pool);

	KeyValueErrorCode Parse(const char*& str, const bool isRoot);
	void BuildData(char*& destBuffer);
	void Solidify();


	void ToString(char*& str, size_t& maxLength, int tabCount);
	size_t ToStringLength(int tabCount);

	// An invalid KV for use in returns with references
	static KeyValue& GetInvalid();

	KeyValueRoot* rootNode;

public:
	kvString_t key;

	// These two structs must be the same size!!
	union
	{
		kvString_t value;

		// Maybe swap this with a hash map at some point?
		struct {

			KeyValue* children;
			size_t childCount;
		};
	};
	KeyValue* lastChild;

	bool hasChildren;

	KeyValue* next;
};

template<typename T>
class KeyValuePool
{
private:
	KeyValuePool();
	~KeyValuePool();

	void Drain();

	T* Get();

	inline bool IsFull() { return position >= currentPool->length; }

	class PoolChunk
	{
	public:
		PoolChunk(size_t length);
		PoolChunk(size_t length, PoolChunk* last);

		T* pool;
		size_t length;

		PoolChunk* next;
	};

	size_t position;

	PoolChunk* firstPool;
	PoolChunk* currentPool;

	// Nothing other than KeyValue and KeyValueRoot should touch this!
	friend KeyValue;
	friend KeyValueRoot;
};

class KeyValueRoot : public KeyValue
{
public:
	KeyValueRoot(const char* str);
	KeyValueRoot();
	~KeyValueRoot();

	// Makes access times faster and decreases memory usage at the cost of irreversibly making the kv read-only and slowing down delete time
	void Solidify();
	KeyValueErrorCode Parse(const char* str);

private:

	// This string buffer exists to hold *all parsed* key and value strings. 
	char* stringBuffer;
	// bufferSize is tallied up during the parse as the total length of all parsed strings, and stringBuffer is allocated using it.
	size_t bufferSize;

	KeyValuePool<KeyValue> readPool;
	KeyValuePool<KeyValue> writePool;
	KeyValuePool<char*> writePoolStrings;

	bool solidified;

	friend KeyValue;
};
