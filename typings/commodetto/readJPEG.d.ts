/*
* Copyright (c) 2020-2022 Moddable Tech, Inc.
*
*   This file is part of the Moddable SDK Tools.
*
*   The Moddable SDK Tools is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   The Moddable SDK Tools is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with the Moddable SDK Tools.  If not, see <http://www.gnu.org/licenses/>.
*
*/

import Bitmap from "commodetto/Bitmap"

declare module "commodetto/readJPEG" {
  type Block = Bitmap & {
    x: number,
    y: number
  }

  class JPEG {
    constructor(buffer: (BufferLike), options?: {pixelFormat: number})

    read(): Block
    push(buffer: BufferLike): void

    readonly ready: boolean
  }
}

export {JPEG as default};
