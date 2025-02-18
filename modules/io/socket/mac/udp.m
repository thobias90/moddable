/*
 * Copyright (c) 2022  Moddable Tech, Inc.
 *
 *   This file is part of the Moddable SDK Runtime.
 * 
 *   The Moddable SDK Runtime is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 * 
 *   The Moddable SDK Runtime is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 * 
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with the Moddable SDK Runtime.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
 
/*

	To do:

		- port
		- multicast
*/
 
#include "xsmc.h"
#include "mc.xs.h"			// for xsID_* values

#include "builtinCommon.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFSocket.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <net/if_types.h>
#include <net/if_dl.h>

#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define kMaxPackets (100)
 
struct UDPPacketRecord {
	struct UDPPacketRecord *next;
	uint16_t		port;
	char			address[INET_ADDRSTRLEN];
	int				byteLength;
	uint8_t			data[];
};
typedef struct UDPPacketRecord UDPPacketRecord;
typedef struct UDPPacketRecord *UDPPacket;

struct UDPRecord {
    int					skt;
	xsSlot				obj;
	int8_t				useCount;
	uint8_t				done;
	int16_t				port;
	CFSocketRef			cfSkt;
	CFRunLoopSourceRef	cfRunLoopSource;
	xsMachine			*the;
	xsSlot				*onReadable;
	UDPPacket			packets;

};
typedef struct UDPRecord UDPRecord;
typedef struct UDPRecord *UDP;

static void udpHold(UDP udp);
static void udpRelease(UDP udp);
static void xs_udp_mark(xsMachine* the, void* it, xsMarkRoot markRoot);
static void socketCallback(CFSocketRef s, CFSocketCallBackType cbType, CFDataRef addr, const void *data, void *info);

static const xsHostHooks xsUDPHooks = {
	xs_udp_destructor,
	xs_udp_mark,
	NULL
};

void xs_udp_constructor(xsMachine *the)
{
	UDP udp;
	xsSlot *onReadable;
	int port = 0;

	xsmcVars(1);

	if (kIOFormatBuffer != builtinInitializeFormat(the, kIOFormatBuffer))
		xsRangeError("unimplemented");

	onReadable = builtinGetCallback(the, xsID_onReadable);

//@@ port currently unused!
	xsmcGet(xsVar(0), xsArg(0), xsID_port);
	port = xsmcToInteger(xsVar(0));
	if ((port < 0) || (port > 65535))
		xsRangeError("invalid port");

	udp = c_calloc(1, sizeof(UDPRecord));
	if (!udp)
		xsRangeError("no memory");
	udp->skt = -1;

	xsmcSetHostData(xsThis, udp);

	CFSocketContext socketCtxt = {0, udp, NULL, NULL, NULL};
	udp->cfSkt = CFSocketCreate(kCFAllocatorDefault, PF_INET, SOCK_DGRAM, IPPROTO_UDP, kCFSocketReadCallBack, socketCallback, &socketCtxt);
	udp->skt = CFSocketGetNative(udp->cfSkt);
	if (udp->skt < 0) {
		CFSocketInvalidate(udp->cfSkt);
		CFRelease(udp->cfSkt);
		xsUnknownError("no socket");
	}

	int set = 1;
	setsockopt(udp->skt, SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(int));

	fcntl(udp->skt, F_SETFL, O_NONBLOCK | fcntl(udp->skt, F_GETFL, 0));

	udp->cfRunLoopSource = CFSocketCreateRunLoopSource(NULL, udp->cfSkt, 0);
	CFRunLoopAddSource(CFRunLoopGetCurrent(), udp->cfRunLoopSource, kCFRunLoopCommonModes);

	xsSetHostHooks(xsThis, (xsHostHooks *)&xsUDPHooks);
	udp->the = the;
	udp->obj = xsThis;
	xsRemember(udp->obj);
	udp->port = port;
	udpHold(udp);

	builtinInitializeTarget(the);

	udp->onReadable = onReadable;
}

void xs_udp_destructor(void *data)
{
	UDP udp = data;
	if (!udp) return;

	if (udp->cfRunLoopSource) {
		CFRunLoopRemoveSource(CFRunLoopGetCurrent(), udp->cfRunLoopSource, kCFRunLoopCommonModes);
		CFRelease(udp->cfRunLoopSource);
		udp->cfRunLoopSource = NULL;
	}

	if (udp->cfSkt) {
		CFSocketInvalidate(udp->cfSkt);
		CFRelease(udp->cfSkt);
		udp->cfSkt = NULL;
		udp->skt = -1;
	}

	if (udp->skt >= 0) {
		close(udp->skt);
		udp->skt = -1;
	}

	while (udp->packets) {
		void *next = udp->packets->next;
		free(udp->packets);
		udp->packets = next;
	}

	free(udp);
}

void xs_udp_close(xsMachine *the)
{
	UDP udp = xsmcGetHostData(xsThis);
	if (udp && xsmcGetHostDataValidate(xsThis, (void *)&xsUDPHooks)) {
		udp->done = 1;

		CFSocketDisableCallBacks(udp->cfSkt, kCFSocketReadCallBack | kCFSocketWriteCallBack | kCFSocketConnectCallBack);

		xsmcSetHostData(xsThis, NULL);
		xsForget(udp->obj);
		xsmcSetHostDestructor(xsThis, NULL);
		udpRelease(udp);
	}
}

void xs_udp_read(xsMachine *the)
{
	UDP udp = xsmcGetHostDataValidate(xsThis, (void *)&xsUDPHooks);
	UDPPacket packet = udp->packets;
	if (!packet)
		return;

	xsmcSetArrayBuffer(xsResult, packet->data, packet->byteLength);

	xsmcVars(1);
	xsmcSetInteger(xsVar(0), packet->port);
	xsmcSet(xsResult, xsID_port, xsVar(0));

	xsVar(0) = xsString(packet->address);
	xsmcSet(xsResult, xsID_address, xsVar(0));

	udp->packets = packet->next;
	c_free(packet);
}

void xs_udp_write(xsMachine *the)
{
	UDP udp = xsmcGetHostDataValidate(xsThis, (void *)&xsUDPHooks);
	void *buffer;
	xsUnsignedValue byteLength;
	int port;
	struct sockaddr_in dest_addr = {0};

	if (!udp->skt) {
		xsTrace("write to closed socket\n");
		return;
	}

	dest_addr.sin_family = AF_INET;
	dest_addr.sin_addr.s_addr = inet_addr(xsmcToString(xsArg(0)));
	port = xsmcToInteger(xsArg(1));
	dest_addr.sin_port = htons(port);

	xsmcGetBufferReadable(xsArg(2), &buffer, &byteLength);
	int result = sendto(udp->skt, buffer, byteLength, 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
	if (result < 0)
		xsUnknownError("sendto failed");
}

void udpHold(UDP udp)
{
	udp->useCount += 1;
}

void udpRelease(UDP udp)
{
	udp->useCount -= 1;
	if (udp->useCount <= 0)
		xs_udp_destructor(udp);
}

void xs_udp_mark(xsMachine* the, void* it, xsMarkRoot markRoot)
{
	UDP udp = it;

	if (udp->onReadable)
		(*markRoot)(the, udp->onReadable);
}

void socketCallback(CFSocketRef s, CFSocketCallBackType cbType, CFDataRef addr, const void *dataIn, void *info)
{
	UDP udp = info;
	struct sockaddr_in srcAddr;
	socklen_t srcAddrLen = sizeof(srcAddr);
	int byteLength, packets;
	uint8_t data[2048];
	UDPPacket packet;

	if ((-1 == udp->skt) || udp->done || !(cbType & kCFSocketReadCallBack))
		return;		// closed socket

	byteLength = recvfrom(udp->skt, data, sizeof(data), MSG_DONTWAIT, (struct sockaddr *)&srcAddr, &srcAddrLen);
	if (byteLength <= 0) return;

	packet = malloc(sizeof(UDPPacketRecord) + byteLength);
	if (!packet) return;

	packet->next = NULL;
	packet->port = ntohs(srcAddr.sin_port);
	inet_ntop(srcAddr.sin_family, &srcAddr.sin_addr, packet->address, sizeof(packet->address));
	packet->byteLength = byteLength;
	memmove(packet->data, data, byteLength);
	
	packets = 1;
	if (!udp->packets)
		udp->packets = packet;
	else {
		UDPPacket walker = udp->packets;
		while (walker->next) {
			packets += 1;
			walker = walker->next;
		}
		walker->next = packet;
		packets += 1;
	}
	
	// if more packets than max, prune oldest packets
	while (packets > kMaxPackets) {
		void *next = udp->packets->next;
		free(udp->packets);
		udp->packets = next;
		packets -= 1;
	}

	if (udp->onReadable) {
		udpHold(udp);

		xsBeginHost(udp->the);
			xsmcSetInteger(xsResult, packets);
			xsCallFunction1(xsReference(udp->onReadable), udp->obj, xsResult);
		xsEndHost(the);

		udpRelease(udp);
	}
}
