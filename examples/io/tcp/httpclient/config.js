import TCP from "embedded:io/socket/tcp";
import UDP from "embedded:io/socket/udp";
import Resolver from "embedded:network/dns/resolver/udp";

import HTTPClient from "embedded:network/http/client";

const dns = {
	io: Resolver,
	servers: [
		"1.1.1.1", //...
	],
	socket: {
		io: UDP,
	},
};
globalThis.device = Object.freeze({
	...globalThis.device,
	network: {
		http: {
			io: HTTPClient,
			dns,
			socket: {
				io: TCP,
			},		
		},
	},
}, true);

