#ifndef EVENTLOG_CC
#define EVENTLOG_CC

#include <node.h>
#include <node_buffer.h>

#include <sstream>
#include <string>

#include <stdarg.h>
#include <string.h>

#include <windows.h>

#include "eventlog.h"

#ifdef _WIN32
static char errbuf[1024];
#endif
const char* eventlog_strerror (int code) {
#ifdef _WIN32
	if (FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM, 0, code, 0, errbuf,
			1024, NULL)) {
		return errbuf;
	} else {
		strcpy (errbuf, "Unknown error");
		return errbuf;
	}
#else
	return strerror (code);
#endif
}

namespace eventlog {

static Persistent<String> CloseSymbol;
static Persistent<String> EmitSymbol;

void InitAll (Handle<Object> target) {
	CloseSymbol = NODE_PSYMBOL("close");
	EmitSymbol = NODE_PSYMBOL("emit");

	EventLogWrap::Init (target);
}

NODE_MODULE(eventlog, InitAll)

void EventLogWrap::Init (Handle<Object> target) {
	HandleScope scope;
	
	Local<FunctionTemplate> tpl = FunctionTemplate::New (New);
	
	tpl->InstanceTemplate ()->SetInternalFieldCount (1);
	tpl->SetClassName (String::NewSymbol ("EventLogWrap"));
	
	NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);
	NODE_SET_PROTOTYPE_METHOD(tpl, "open", Open);
	NODE_SET_PROTOTYPE_METHOD(tpl, "read", Read);
	
	target->Set (String::NewSymbol ("EventLogWrap"), tpl->GetFunction ());
}

EventLogWrap::EventLogWrap (const char* event_log_name)
		: event_log_name_ (event_log_name) {
	event_log_mutex_ = CreateMutex (NULL, FALSE, NULL);
}

EventLogWrap::~EventLogWrap () {
	this->CloseEventLog ();
	CloseHandle (event_log_mutex_);
}

Handle<Value> EventLogWrap::Close (const Arguments& args) {
	HandleScope scope;
	EventLogWrap* log = EventLogWrap::Unwrap<EventLogWrap> (args.This ());

	log->CloseEventLog ();

	return scope.Close (args.This ());
}

void EventLogWrap::CloseEventLog (void) {
	HandleScope scope;
	
	this->Lock ();
	
	if (event_log_handle_) {
		::CloseEventLog (event_log_handle_);
		event_log_handle_ = NULL;

		Local<Value> emit = this->handle_->Get (EmitSymbol);
		Local<Function> cb = emit.As<Function> ();

		Local<Value> args[1];
		args[0] = Local<Value>::New (CloseSymbol);

		cb->Call (this->handle_, 1, args);
	}
	
	this->UnLock ();
}

void EventLogWrap::Lock (void) {
	WaitForSingleObject (event_log_mutex_, INFINITE);
}

Handle<Value> EventLogWrap::New (const Arguments& args) {
	HandleScope scope;
	
	if (args.Length () < 1) {
		ThrowException (Exception::Error (String::New (
				"One argument is required")));
		return scope.Close (args.This ());
	}
	
	if (! args[0]->IsString ()) {
		ThrowException (Exception::TypeError (String::New (
				"Name argument must be a string")));
		return scope.Close (args.This ());
	}
	String::AsciiValue event_log_name (args[0]->ToString ());
	
	EventLogWrap* log = new EventLogWrap (*event_log_name);
	
	log->Wrap (args.This ());

	return scope.Close (args.This ());
}

void EventLogWrap::OpenRequestBegin (uv_work_t* request) {
	OpenRequest *open_request = (OpenRequest*) request->data;
	
	open_request->rcode = 0;
	
	open_request->log->Lock ();

	if (! open_request->log->event_log_handle_) {
		if (! (open_request->log->event_log_handle_ = ::OpenEventLog (NULL,
				open_request->log->event_log_name_.c_str ()))) {
			open_request->rcode = GetLastError ();
		}
	}
	
	open_request->log->UnLock ();
}

void EventLogWrap::OpenRequestEnd (uv_work_t* request, int status) {
	HandleScope scope;
	OpenRequest *open_request = (OpenRequest*) request->data;

	if (status) {
		Local<Value> argv[1];
		argv[0] = Exception::Error (String::New (eventlog_strerror (
				uv_last_error (uv_default_loop ()).code)));
		open_request->cb->Call (open_request->log->handle_, 1, argv);
	} else {
		if (open_request->rcode > 0) {
			Local<Value> argv[1];
			DWORD rcode = open_request->rcode;
			argv[0] = Exception::Error (String::New (eventlog_strerror (rcode)));
			open_request->cb->Call (open_request->log->handle_, 1, argv);
		} else {
			open_request->cb->Call (open_request->log->handle_, 0, NULL);
		}
	}
	
	open_request->cb.Dispose ();
	delete open_request;
}

Handle<Value> EventLogWrap::Open (const Arguments& args) {
	HandleScope scope;
	EventLogWrap* log = EventLogWrap::Unwrap<EventLogWrap> (args.This ());

	if (args.Length () < 1) {
		ThrowException (Exception::Error (String::New (
				"One argument is required")));
		return scope.Close (args.This ());
	}

	if (! args[0]->IsFunction ()) {
		ThrowException (Exception::TypeError (String::New (
				"Callback argument must be a function")));
		return scope.Close (args.This ());
	}

	OpenRequest *request = new OpenRequest;
	request->uv_request.data = (void*) request;
	
	request->cb = Persistent<Function>::New (Local<Function>::Cast (args[0]));
	request->log = log;
	
	uv_queue_work (uv_default_loop (), &request->uv_request,
			OpenRequestBegin, OpenRequestEnd);

	return scope.Close (args.This ());
}

DWORD EventLogWrap::ParseEvent (const char *buffer, PEVENTLOGRECORD record,
		ParsedEvent &parsed_event) {
	parsed_event.source_name = buffer + sizeof (*record);
	parsed_event.computer_name = buffer + sizeof (*record)
			+ parsed_event.source_name.length () + 1;
	char message[EVENTLOG_BUFFER_SIZE];

	std::string key_name = EVENTLOG_KEY_PREFIX + event_log_name_
			+ "\\" + parsed_event.source_name;

	HKEY key_handle;
	DWORD rc, found_message_file = false;
	char message_file[EVENTLOG_KEY_SIZE];

	rc = RegOpenKeyEx (HKEY_LOCAL_MACHINE, key_name.c_str (), 0,
			KEY_READ, &key_handle);
	if (rc != ERROR_SUCCESS) {
		RegCloseKey (key_handle);
		if (rc != ENOENT)
			return rc;
	} else {
		DWORD key_size = EVENTLOG_KEY_SIZE, key_type;
		rc = RegQueryValueEx (key_handle, "EventMessageFile", NULL,
				&key_type, (LPBYTE) message_file, &key_size);
		RegCloseKey (key_handle);
		if (rc != ERROR_SUCCESS) {
			if (rc != ENOENT)
				return rc;
		} else {
			found_message_file = true;
		}
	}

	char formatted_message_file[EVENTLOG_KEY_SIZE];
	bool create_default_message = true;

	if (found_message_file) {
		rc = ExpandEnvironmentStrings (message_file,
				formatted_message_file, EVENTLOG_KEY_SIZE);
		if (rc == 0)
			return GetLastError ();
		
		char *current = formatted_message_file, *next;
		while (current) {
			next = strchr (current, ';');
			if (next) {
				*next = '\0';
				next++;
			}
			
			HMODULE library = LoadLibraryEx (current, NULL,
					LOAD_LIBRARY_AS_DATAFILE);
			if (library == NULL) {
				if (GetLastError () != ENOENT)
					return GetLastError ();
			} else {
				const char** strings_array = new const char*[record->NumStrings];
				const char* string = buffer + record->StringOffset;
				int i = 0;
				while (i < record->NumStrings) {
					strings_array[i] = string;
					string = string + strlen (string) + 1;
					i++;
				}

				rc = FormatMessage (FORMAT_MESSAGE_FROM_HMODULE
						| FORMAT_MESSAGE_ARGUMENT_ARRAY,
						library, record->EventID, NULL,
						message, EVENTLOG_BUFFER_SIZE,
						(va_list*) strings_array);
				DWORD last_error = (rc <= 0) ? GetLastError () : 0;

				FreeLibrary (library);
				delete[] strings_array;

				if (rc <= 0) {
					if (last_error != ERROR_MR_MID_NOT_FOUND)
						return last_error;
				} else {
					create_default_message = false;
					parsed_event.message = message;
					break;
				}
			}
			
			current = next;
		}
	} else {
		create_default_message = true;
	}

	if (create_default_message) {
		std::stringstream message_stream;
		const char* string = buffer + record->StringOffset;
		int i = 0;
		while (i < record->NumStrings) {
			if (i > 0)
				message_stream << ", ";
			message_stream << string;
			string = string + strlen (string) + 1;
			i++;
		}
		parsed_event.message = message_stream.str ();
	}

	parsed_event.record_number = record->RecordNumber;
	parsed_event.time_generated = record->TimeGenerated;
	parsed_event.time_written = record->TimeWritten;
	parsed_event.event_type = record->EventType;
	parsed_event.event_category = record->EventCategory;
	
	return 0;
}

void EventLogWrap::ReadRequestBegin (uv_work_t* request) {
	ReadRequest *read_request = (ReadRequest*) request->data;
	
	read_request->rcode = 0;
	
	read_request->log->Lock ();
	bool event_log_open = read_request->log->event_log_handle_ ? true : false;
	read_request->log->UnLock ();

	if (! event_log_open) {
		read_request->event_log_not_open = true;
		return;
	} else {
		read_request->event_log_not_open = false;
	}

	char buffer[EVENTLOG_BUFFER_SIZE], *cursor;
	DWORD read = 0, required = 0;
	PEVENTLOGRECORD record;
	
	read_request->log->Lock ();
	BOOL rc = ReadEventLog (read_request->log->event_log_handle_,
			EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, read_request->offset,
			buffer, EVENTLOG_BUFFER_SIZE, &read, &required);
	read_request->log->UnLock ();

	if (! rc) {
		read_request->rcode = GetLastError ();
		/**
		 ** ERROR_INVALID_PARAMETER implies we tried to read from an offset
		 ** past the end of the last record in the event log
		 **/
		if (read_request->rcode == ERROR_INVALID_PARAMETER)
			read_request->rcode = ERROR_HANDLE_EOF;
		return;
	}
	
	for (cursor = buffer; cursor - buffer < read; ) {
		record = (PEVENTLOGRECORD) cursor;

		ParsedEvent *parsed_event = new ParsedEvent;
		DWORD rc = read_request->log->ParseEvent (cursor, record, *parsed_event);
		if (rc > 0) {
			delete parsed_event;
			read_request->rcode = rc;
			return;
		}

		read_request->parsed_events.push_back (*parsed_event);

		cursor += record->Length;
	}
}

void EventLogWrap::ReadRequestEnd (uv_work_t* request, int status) {
	HandleScope scope;
	ReadRequest *read_request = (ReadRequest*) request->data;

	if (status) {
		Local<Value> argv[1];
		argv[0] = Exception::Error (String::New (eventlog_strerror (
				uv_last_error (uv_default_loop ()).code)));
		read_request->cb->Call (read_request->log->handle_, 1, argv);
	} else {
		if (read_request->rcode > 0) {
			if (read_request->rcode == ERROR_HANDLE_EOF) {
				read_request->cb->Call (read_request->log->handle_, 0, NULL);
			} else {
				Local<Value> argv[1];
				DWORD rcode = read_request->rcode;
				argv[0] = Exception::Error (String::New (eventlog_strerror (
						rcode)));
				if (rcode == ERROR_EVENTLOG_FILE_CHANGED)
					argv[0]->ToObject ()->Set (String::NewSymbol (
							"event_log_file_changed"), Boolean::New (true));
				read_request->cb->Call (read_request->log->handle_, 1, argv);
			}
		} else if (read_request->event_log_not_open) {
			Local<Value> argv[1];
			argv[0] = Exception::Error (String::New ("Event log has not "
					"been opened"));
			read_request->cb->Call (read_request->log->handle_, 1, argv);
		} else {
			std::list<ParsedEvent>::iterator iterator
					= read_request->parsed_events.begin ();

			Local<Value> argv[2];
			argv[0] = Local<Value>::New (Null ());

			while (iterator != read_request->parsed_events.end ()) {
				ParsedEvent& parsed_event = *iterator;

				Local<Object> event = Object::New ();

				event->Set (String::NewSymbol ("sourceName"),
						String::New (parsed_event.source_name.c_str ()));
				event->Set (String::NewSymbol ("computerName"),
						String::New (parsed_event.computer_name.c_str ()));
				event->Set (String::NewSymbol ("recordNumber"),
						Integer::NewFromUnsigned (parsed_event.record_number));
				event->Set (String::NewSymbol ("timeGenerated"),
						Integer::NewFromUnsigned (parsed_event.time_generated));
				event->Set (String::NewSymbol ("timeWritten"),
						Integer::NewFromUnsigned (parsed_event.time_written));
				event->Set (String::NewSymbol ("eventType"),
						Integer::NewFromUnsigned (parsed_event.event_type));
				event->Set (String::NewSymbol ("eventCategory"),
						Integer::NewFromUnsigned (parsed_event.event_category));
				event->Set (String::NewSymbol ("message"),
						String::New (parsed_event.message.c_str ()));

				argv[1] = event;
				
				read_request->cb->Call (read_request->log->handle_, 2, argv);
				
				iterator++;
			}
			
			read_request->cb->Call (read_request->log->handle_, 0, NULL);
		}
	}

	read_request->cb.Dispose ();
	delete read_request;
}

Handle<Value> EventLogWrap::Read (const Arguments& args) {
	HandleScope scope;
	EventLogWrap* log = EventLogWrap::Unwrap<EventLogWrap> (args.This ());
	
	V8::IdleNotification();

	if (args.Length () < 2) {
		ThrowException (Exception::Error (String::New (
				"Two arguments are required")));
		return scope.Close (args.This ());
	}
	
	if (! args[0]->IsUint32 ()) {
		ThrowException (Exception::TypeError (String::New (
				"Offset argument must be a number")));
		return scope.Close (args.This ());
	}

	if (! args[1]->IsFunction ()) {
		ThrowException (Exception::TypeError (String::New (
				"Feed callback argument must be a function")));
		return scope.Close (args.This ());
	}
	
	DWORD offset = args[0]->ToUint32 ()->Value ();

	ReadRequest *request = new ReadRequest;
	request->uv_request.data = (void*) request;
	
	request->cb = Persistent<Function>::New (Local<Function>::Cast (args[1]));
	request->log = log;
	
	request->offset = offset;
	
	uv_queue_work (uv_default_loop (), &request->uv_request,
			ReadRequestBegin, ReadRequestEnd);

	return scope.Close (args.This ());
}

void EventLogWrap::UnLock (void) {
	ReleaseMutex (event_log_mutex_);
}

}; /* namespace eventlog */

#endif /* EVENTLOG_CC */
