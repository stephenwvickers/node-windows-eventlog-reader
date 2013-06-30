
var eventlog = require ("../");

if (process.argv.length < 4) {
	console.log ("node read-all <name> <offset>");
	process.exit (-1);
}

var name = process.argv[2];
var offset = parseInt (process.argv[3]);

function feedCb (event) {
	console.dir (event);
}

function doneCb (error) {
	if (error)
		console.error (error.toString ());
}

var reader = eventlog.createReader (name);

reader.on ("close", function () {
	console.log ("event log closed");
});

reader.open (function (error) {
	if (error) {
		console.error (error.toString ());
	} else {
		reader.readAll (offset, feedCb, doneCb);
	}
});
