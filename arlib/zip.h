#pragma once
#include "array.h"
#include "file.h"

class zipfile : nocopy {
	class impl;
public:
	static zipfile* create(file* core);
	static zipfile* create(filewrite* core);
	
	virtual arrayview<string> files() = 0;
	virtual array<byte> read(cstring name) = 0;
	
	//Only usable if the zipfile was created with a filewrite.
	//Writing a blank array deletes the file.
	virtual void write(cstring name, arrayview<byte> data) = 0;
	
	virtual ~zipfile() {}
};
