
var eventlog = require ("../");

if (process.argv.length < 4) {
	console.log ("node tail <name> <offset>");
	process.exit (-1);
}

var name = process.argv[2];
var offset = parseInt (process.argv[3]);

function cb (error, event) {
	if (error)
		console.error (error.toString ());
	else
		if (event)
			console.dir (event);
}

var reader = eventlog.createReader (name);

reader.on ("close", function () {
	console.log ("event log closed");
});

reader.open (function (error) {
	if (error) {
		console.error (error.toString ());
	} else {
		reader.tail (offset, cb);
	}
});
