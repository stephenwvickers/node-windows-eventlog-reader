
var events = require ("events");
var eventlog = require ("./build/Release/eventlog");
var util = require ("util");

for (var key in events.EventEmitter.prototype) {
  eventlog.EventLogWrap.prototype[key] = events.EventEmitter.prototype[key];
}

function _expandConstantObject (object) {
	var keys = [];
	for (key in object)
		keys.push (key);
	for (var i = 0; i < keys.length; i++)
		object[object[keys[i]]] = parseInt (keys[i]);
}

var EventType = {
	1:  "Error",
	2:  "Warning",
	4:  "Information",
	8:  "AuditSuccess",
	16: "AuditFailure"
};

_expandConstantObject (EventType);

function EventLogClearedError (messsage) {
	this.name = "EventLogClearedError";
	this.message = messsage;
}
util.inherits (EventLogClearedError, Error);

function Reader (name) {
	Reader.super_.call (this);

	this.name = name;
	this.wrap = new eventlog.EventLogWrap (this.name);
	
	var me = this;
	this.wrap.on ("close", me.onClose.bind (me));
};

util.inherits (Reader, events.EventEmitter);

Reader.prototype.close = function () {
	this.wrap.close ();
	return this;
}

function patchEvent (event) {
	event.eventType = EventType[event.eventType] || ("Uknown(" + event.eventType + ")");
	event.timeGenerated = new Date (event.timeGenerated * 1000);
	event.timeWritten = new Date (event.timeWritten * 1000);
	return event;
}

Reader.prototype.onClose = function () {
	this.emit ("close");
}

Reader.prototype.open = function (cb) {
	this.wrap.open (cb);
	return this;
}

function readCb (req, error, event) {
	if (error || ! event) {
		req.doneCb (error);
	} else {
		req.feedCb (patchEvent (event));
	}
}

Reader.prototype.read = function (offset, feedCb, doneCb) {
	var me = this;
	var req = {
		feedCb: feedCb,
		doneCb: doneCb
	};
	this.wrap.read (offset || 1, readCb.bind (me, req));
	return this;
}

function readAllCb (req, error, event) {
	if (error || ! event) {
		if (! req.lastRecordNumber) {
			req.doneCb (error);
		} else {
			me = this;
			req.offset = req.lastRecordNumber + 1;
			req.lastRecordNumber = 0;
			this.wrap.read (req.offset, readAllCb.bind (me, req));
		}
	} else {
		req.lastRecordNumber = event.recordNumber;
		req.feedCb (patchEvent (event));
	}
}

Reader.prototype.readAll = function (offset, feedCb, doneCb) {
	var me = this;
	var req = {
		feedCb: feedCb,
		doneCb: doneCb,
		offset: offset || 1,
		lastRecordNumber: 0
	};
	this.wrap.read (req.offset, readAllCb.bind (me, req));
	return this;
}

function tailCb (req, error, event) {
	if (error || ! event) {
		if (! req.lastRecordNumber) {
			if (error) {
				if (error.event_log_file_changed) {
					var newError = new EventLogClearedError (error.message);
					req.cb (newError);
				} else {
					req.cb (error);
				}
			} else {
				var me = this;
				setTimeout (function () {
					me.wrap.read (req.offset, tailCb.bind (me, req));
				}, req.interval);
			}
		} else {
			me = this;
			req.offset = req.lastRecordNumber + 1;
			req.lastRecordNumber = 0;
			this.wrap.read (req.offset, tailCb.bind (me, req));
		}
	} else {
		req.lastRecordNumber = event.recordNumber;
		req.cb (null, patchEvent (event));
	}
}

Reader.prototype.tail = function (offset, interval, cb) {
	if (! cb) {
		cb = interval;
		interval = 3000;
	}
	var req = {
		offset: offset,
		lastRecordNumber: 0,
		interval: interval,
		cb: cb
	};
	var me = this;
	me.wrap.read (req.offset, tailCb.bind (me, req));
	return this;
}

exports.createReader = function (name) {
	return new Reader (name);
};

exports.Reader = Reader;

exports.EventLogClearedError = EventLogClearedError;
