/*
* Copyright (c) 2021  Moddable Tech, Inc.
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

#include "xsmc.h"
#include "xsHost.h"
#if mxNoFunctionLength
	#include "mc.xs.h"			// for xsID_ values
#else
	#include <stdbool.h>

	#define xsID_read (xsID("read"))
	#define xsID_String (xsID("String"))
	#define xsID_Uint8Array (xsID("Uint8Array"))
	#define xsID_written (xsID("written"))
#endif

#if !mxNoFunctionLength
void xs_textencoder(xsMachine *the)
{
	xsmcGet(xsResult, xsTarget, xsID("prototype"));
	xsResult = xsNewHostInstance(xsResult);
}
#endif

/*
	null character maps to 0xF4, 0x90, 0x80, 0x80
*/

void xs_textencoder_encode(xsMachine *the)
{
	uint8_t *src, *dst;
	int length = 0;
	uint8_t hasNull = 0;

	xsArg(0) = xsCall1(xsGlobal, xsID_String, xsArg(0));
	src = (uint8_t *)xsmcToString(xsArg(0));

	while (true) {
		uint8_t c = c_read8(src++);
		if (!c) break;

		length += 1;
		if ((0xF4 == c) && (0x90 == c_read8(src)) && (0x80 == c_read8(src + 1)) && (0x80 == c_read8(src + 2))) {
			src += 3;
			hasNull = 1;
		}
	}

	xsmcSetArrayBuffer(xsResult, NULL, length);
	src = (uint8_t *)xsmcToString(xsArg(0));
	dst = xsmcToArrayBuffer(xsResult);
	if (hasNull) {
		while (true) {
			uint8_t c = c_read8(src++);
			if (!c) break;

			if ((0xF4 == c) && (0x90 == c_read8(src)) && (0x80 == c_read8(src + 1)) && (0x80 == c_read8(src + 2))) {
				*dst++ = 0;
				src += 3;
			}
			else
				*dst++ = c;
		}
	}
	else
		c_memcpy(dst, src, length);

	xsResult = xsNew1(xsGlobal, xsID_Uint8Array, xsResult);
}

void xs_textencoder_encodeInto(xsMachine *the)
{
	uint8_t *src, *dst;
	xsUnsignedValue dstTotal, dstRemaining;
	int read = 0;

	if (!xsmcIsInstanceOf(xsArg(1), xsTypedArrayPrototype))		//@@ limit to Uint8Array
		xsUnknownError("Uint8Array only");

	xsArg(0) = xsCall1(xsGlobal, xsID_String, xsArg(0));
	xsmcGetBufferWritable(xsArg(1), (void **)&dst, &dstTotal);
	dstRemaining = dstTotal; 
	src = (uint8_t *)xsmcToString(xsArg(0));

	while (dstRemaining) {
		uint8_t first = c_read8(src++);
		if (!first) break;

		if (first < 0x80) {
			*dst++ = first;
			dstRemaining -= 1;
		}
		else if (0xC0 == (first & 0xE0)) {
			if (dstRemaining < 2)
				break;

			*dst++ = first;
			*dst++ = c_read8(src++);
			
			dstRemaining -= 2;
		}
		else if (0xE0 == (first & 0xF0)) {
			if (dstRemaining < 3)
				break;

			*dst++ = first;
			*dst++ = c_read8(src++);
			*dst++ = c_read8(src++);
			
			dstRemaining -= 3;
		}
		else if (0xF0 == (first & 0xF0)) {
			if ((0xF4 == first) && (0x90 == c_read8(src)) && (0x80 == c_read8(src + 1)) && (0x80 == c_read8(src + 2))) {
				*dst++ = 0;
				dstRemaining -= 1;
				src += 3;
				read += 1;
				continue;
			}

			if (dstRemaining < 4)
				break;

			*dst++ = first;
			*dst++ = c_read8(src++);
			*dst++ = c_read8(src++);
			*dst++ = c_read8(src++);
			
			dstRemaining -= 4;
		}
		else
			fxAbort(the, xsFatalCheckExit);
		
		read += 1;
	}

	xsmcSetNewObject(xsResult);

	xsmcVars(1);
	xsmcSetInteger(xsVar(0), read);
	xsmcSet(xsResult, xsID_read, xsVar(0));

	xsmcSetInteger(xsVar(0), dstTotal - dstRemaining);
	xsmcSet(xsResult, xsID_written, xsVar(0));
}

#if !mxNoFunctionLength
void modInstallTextEncoder(xsMachine *the)
{
	#define kPrototype (0)
	#define kConstructor (1)
	#define kScratch (2)

	xsBeginHost(the);
	xsmcVars(3);

	xsmcSetNewObject(xsVar(kPrototype));
	xsVar(kConstructor) = xsNewHostConstructor(xs_textencoder, 1, xsVar(kPrototype));
	xsmcSet(xsGlobal, xsID("TextEncoder"), xsVar(kConstructor));
	xsVar(kScratch) = xsNewHostFunction(xs_textencoder_encode, 1);
	xsmcSet(xsVar(kPrototype), xsID("encode"), xsVar(kScratch));
	xsVar(kScratch) = xsNewHostFunction(xs_textencoder_encodeInto, 2);
	xsmcSet(xsVar(kPrototype), xsID("encodeInto"), xsVar(kScratch));

	xsEndHost(the);
}
#endif
