#ifndef EVENTLOG_H
#define EVENTLOG_H

#define EVENTLOG_BUFFER_SIZE 51200

#define EVENTLOG_KEY_SIZE 8192
#define EVENTLOG_KEY_PREFIX "SYSTEM\\CurrentControlSet\\Services\\Eventlog\\"

#include <node.h>

#include <list>
#include <string>

using namespace v8;

namespace eventlog {

struct ParsedEvent {
	ParsedEvent (){}
	~ParsedEvent () {}

	std::string source_name;
	std::string computer_name;
	unsigned int record_number;
	unsigned int time_generated;
	unsigned int time_written;
	unsigned int event_type;
	unsigned int event_category;
	std::string message;
};

class EventLogWrap : public node::ObjectWrap {
public:
	static void Init (Handle<Object> target);

	static void OpenRequestBegin (uv_work_t* request);
	static void OpenRequestEnd (uv_work_t* request, int status);

	static void ReadRequestBegin (uv_work_t* request);
	static void ReadRequestEnd (uv_work_t* request, int status);

private:
	EventLogWrap (const char* event_log_name_);
	~EventLogWrap ();

	static Handle<Value> Close (const Arguments& args);

	void CloseEventLog (void);
	
	void Lock (void);

	static Handle<Value> New (const Arguments& args);
	static Handle<Value> Open (const Arguments& args);

	DWORD ParseEvent (const char *buffer, PEVENTLOGRECORD record,
			ParsedEvent &parsed_event);

	static Handle<Value> Read (const Arguments& args);
	
	void UnLock (void);

	std::string event_log_name_;
	HANDLE event_log_handle_;
	HANDLE event_log_mutex_;
};

struct OpenRequest {
	OpenRequest () {}
	~OpenRequest () {}
	
	uv_work_t uv_request;
	
	Persistent<Function> cb;
	
	DWORD rcode;
	
	EventLogWrap *log;
};

struct ReadRequest {
	ReadRequest () {}
	~ReadRequest () {}
	
	uv_work_t uv_request;
	
	Persistent<Function> cb;
	
	DWORD offset;
	
	bool event_log_not_open;
	DWORD rcode;
	
	std::list<ParsedEvent> parsed_events;
	
	EventLogWrap *log;
};

}; /* namespace eventlog */

#endif /* EVENTLOG_H */
