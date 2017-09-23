#pragma once
#include "global.h"

//A runloop keeps track of a number of file descriptors, calling their handlers whenever the relevant operation is available.
//There are no fairness guarantees. If an event doesn't terminate properly, it may inhibit other fds.
//Do not call enter() or step() while inside a callback. However, set_*(), remove() and exit() are fine.
class runloop : nomove { // Objects are expected to keep pointers to their runloop, so no moving.
protected:
	runloop() {}
public:
	static runloop* global(); // The global runloop handles GUI events, in addition to whatever fds it's told to track.
	static runloop* create(); // For best results, use only one runloop per thread, and use the global one if applicable.
	
	//Callback argument is the fd, in case one object maintains multiple fds. To remove, set both callbacks to NULL.
	//A fd can only be used once per runloop.
	virtual void set_fd(uintptr_t fd, function<void(uintptr_t)> cb_read, function<void(uintptr_t)> cb_write = NULL) = 0;
	
	//Runs once.
	void set_timer_abs(time_t when, function<void()> callback);
	//Runs again if the callback returns true.
	//Accuracy is not guaranteed; it may or may not round the timer frequency to something it finds appropriate,
	// in either direction, and may or may not try to 'catch up' if a call is late (or early).
	virtual void set_timer_rel(unsigned ms, function<bool()> callback) = 0;
	
	//Return value from each set_*() is a token which can be used to cancel the event. Only usable before the timer fires.
	//virtual void remove(uintptr_t id) = 0;
	
	//Executes the mainloop until ->exit() is called. Recommended for most programs.
	virtual void enter() = 0;
	virtual void exit() = 0;
	
	//Runs until there are no more events to process, then returns. Recommended for high-performance programs like games. Call it frequently.
	virtual void step() = 0;
	
	virtual ~runloop() {}
};
