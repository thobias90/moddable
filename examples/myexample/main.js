import {Server} from "websocket"

let server = new Server({port:80});
server.callback = function (message, value) {
	switch (message) {
		case Server.connect:
			trace("main.js: socket connect.\n");
			break;

		case Server.handshake:
			trace("main.js: websocket handshake success\n");
			break;

		case Server.receive:
			trace(`main.js: websocket message received: ${value}\n`);
			break;

		case Server.disconnect:
			trace("main.js: websocket close\n");
			break;
	}
}