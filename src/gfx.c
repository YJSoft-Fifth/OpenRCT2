/*****************************************************************************
 * Copyright (c) 2014 Ted John, Peter Hill
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * This file is part of OpenRCT2.
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include "addresses.h"
#include "gfx.h"
#include "rct2.h"
#include "string_ids.h"
#include "window.h"
#include "osinterface.h"

typedef struct {
	uint32 num_entries;
	uint32 total_size;
} rct_g1_header;

void *_g1Buffer = NULL;

// HACK These were originally passed back through registers
int gLastDrawStringX;
int gLastDrawStringY;

uint8 _screenDirtyBlocks[5120];

static void gfx_draw_dirty_blocks(int x, int y, int columns, int rows);

/**
 * 
 *  rct2: 0x00678998
 */
int gfx_load_g1()
{
	FILE *file;
	rct_g1_header header;
	unsigned int i;

	rct_g1_element *g1Elements = RCT2_ADDRESS(RCT2_ADDRESS_G1_ELEMENTS, rct_g1_element);

	file = fopen(get_file_path(PATH_ID_G1), "rb");
	if (file != NULL) {
		if (fread(&header, 8, 1, file) == 1) {
			// number of elements is stored in g1.dat, but because the entry headers are static, this can't be variable until
			// made into a dynamic array
			header.num_entries = 29294;

			// Read element headers
			fread(g1Elements, header.num_entries * sizeof(rct_g1_element), 1, file);

			// Read element data
			_g1Buffer = rct2_malloc(header.total_size);
			fread(_g1Buffer, header.total_size, 1, file);

			fclose(file);

			// Fix entry data offsets
			for (i = 0; i < header.num_entries; i++)
				g1Elements[i].offset += (int)_g1Buffer;

			// Successful
			return 1;
		}
		fclose(file);
	}

	// Unsuccessful
	RCT2_ERROR("Unable to load g1.dat");
	return 0;
}

/**
 * Clears the screen with the specified colour.
 *  rct2: 0x00678A9F
 */
void gfx_clear(rct_drawpixelinfo *dpi, int colour)
{
	int y, w, h;
	char* ptr;

	w = dpi->width >> dpi->var_0F;
	h = dpi->height >> dpi->var_0F;

	ptr = dpi->bits;
	for (y = 0; y < h; y++) {
		memset(ptr, colour, w);
		ptr += w + dpi->pitch;
	}
}

void gfx_draw_pixel(rct_drawpixelinfo *dpi, int x, int y, int colour)
{
	gfx_fill_rect(dpi, x, y, x, y, colour);
}

/*
* Draws a horizontal line of specified colour to a buffer.
* rct2: 0x68474C
*/
void gfx_draw_line_on_buffer(rct_drawpixelinfo *dpi, char colour, int y, int x, int no_pixels)
{
	y -= dpi->y;

	//Check to make sure point is in the y range
	if (y < 0)return;
	if (y >= dpi->height)return;
	//Check to make sure we are drawing at least a pixel
	if (!no_pixels) return;

	no_pixels++;
	x -= dpi->x;

	//If x coord outside range leave
	if (x < 0){
		//Unless the number of pixels is enough to be in range
		no_pixels += x;
		if (no_pixels <= 0)return;
		//Resets starting point to 0 as we don't draw outside the range
		x = 0;
	}

	//Ensure that the end point of the line is within range
	if (x + no_pixels - dpi->width > 0){
		//If the end point has any pixels outside range
		//cut them off. If there are now no pixels return.
		no_pixels -= x + no_pixels - dpi->width;
		if (no_pixels <= 0)return;
	}

	char* bits_pointer;
	//Get the buffer we are drawing to and move to the first coordinate.
	bits_pointer = dpi->bits + y*(dpi->pitch + dpi->width) + x;

	//Draw the line to the specified colour
	for (; no_pixels > 0; --no_pixels, ++bits_pointer){
		*((uint8*)bits_pointer) = colour;
	}
}


/**
 * Draws a line on dpi if within dpi boundaries
 *  rct2: 0x00684466
 * dpi (edi)
 * x1 (ax)
 * y1 (bx)
 * x2 (cx)
 * y2 (dx)
 * colour (ebp)
 */
void gfx_draw_line(rct_drawpixelinfo *dpi, int x1, int y1, int x2, int y2, int colour)
{
	// Check to make sure the line is within the drawing area
	if ((x1 < dpi->x) && (x2 < dpi->x)){
		return;
	}

	if ((y1 < dpi->y) && (y2 < dpi->y)){
		return;
	}

	if ((x1 >(dpi->x + dpi->width)) && (x2 >(dpi->x + dpi->width))){
		return;
	}

	if ((y1 > (dpi->y + dpi->height)) && (y2 > (dpi->y + dpi->height))){
		return;
	}

	//Bresenhams algorithm

	//If vertical plot points upwards
	int steep = abs(y2 - y1) > abs(x2 - x1);
	if (steep){
		int temp_y2 = y2;
		int temp_x2 = x2;
		y2 = x1;
		x2 = y1;
		y1 = temp_x2;
		x1 = temp_y2;
	}

	//If line is right to left swap direction
	if (x1 > x2){
		int temp_y2 = y2;
		int temp_x2 = x2;
		y2 = y1;
		x2 = x1;
		y1 = temp_y2;
		x1 = temp_x2;
	}

	int delta_x = x2 - x1;
	int delta_y = abs(y2 - y1);
	int error = delta_x / 2;
	int y_step;
	int y = y1;

	//Direction of step
	if (y1 < y2)y_step = 1;
	else y_step = -1;

	for (int x = x1, x_start = x1, no_pixels = 1; x < x2; ++x,++no_pixels){
		//Vertical lines are drawn 1 pixel at a time
		if (steep)gfx_draw_line_on_buffer(dpi, colour, x, y, 1);

		error -= delta_y;
		if (error < 0){
			//Non vertical lines are drawn with as many pixels in a horizontal line as possible
			if (!steep)gfx_draw_line_on_buffer(dpi, colour, y, x_start, no_pixels);

			//Reset non vertical line vars
			x_start = x + 1;
			no_pixels = 1;
			y += y_step;
			error += delta_x;
		}

		//Catch the case of the last line
		if (x + 1 == x2 && !steep){
			gfx_draw_line_on_buffer(dpi, colour, y, x_start, no_pixels);
		}
	}
	return;
}

/**
 *
 *  rct2: 0x00678AD4
 * dpi (edi)
 * left (ax)
 * top (cx)
 * right (bx)
 * bottom (dx)
 * colour (ebp)
 */
void gfx_fill_rect(rct_drawpixelinfo *dpi, int left, int top, int right, int bottom, int colour)
{
	int left_, right_, top_, bottom_;
	rct_drawpixelinfo* dpi_;
	left_ = left;
	right_ = right;
	top_ = top;
	bottom_ = bottom;
	dpi_ = dpi;
  
	if ((left > right) || (top > bottom) || (dpi->x > right) || (left >= (dpi->x + dpi->width)) ||
		(bottom < dpi->y) || (top >= (dpi->y + dpi->height)))
		return;

	colour |= RCT2_GLOBAL(0x009ABD9C, uint32);

	if (!(colour & 0x1000000)) {
		if (!(colour & 0x8000000)) {
			left_ = left - dpi->x;
			if (left_ < 0)
				left_ = 0;

			right_ = right - dpi->x;
			right_++;
			if (right_ > dpi->width)
				right_ = dpi->width;

			right_ -= left_;
	
			top_ = top - dpi->y;
			if (top_ < 0)
				top_ = 0;

			bottom_ = bottom - dpi->y;
			bottom_++;

			if (bottom_ > dpi->height)
				bottom_ = dpi->height;

			bottom_ -= top_;

			if (!(colour & 0x2000000)) {
				if (!(colour & 0x4000000)) {
					uint8* pixel = (top_ * (dpi->width + dpi->pitch)) + left_ + dpi->bits;

					int length = dpi->width + dpi->pitch - right_;

					for (int i = 0; i < bottom_; ++i) {
						memset(pixel, (colour & 0xFF), right_);
						pixel += length + right_;
					}
				} else {
					// 00678B8A   00678E38
					char* esi;
					esi = (top_ * (dpi->width + dpi->pitch)) + left_ + dpi->bits;;

					int eax, ebp;
					eax = colour;
					ebp = dpi->width + dpi->pitch - right_;

					RCT2_GLOBAL(0x00EDF810, uint32) = ebp;
					RCT2_GLOBAL(0x009ABDB2, uint16) = bottom_;
					RCT2_GLOBAL(0x00EDF814, uint32) = right_;

					top_ = (top + dpi->y) & 0xf;
					right_ = (right + dpi_->x) &0xf;

					dpi_ = (rct_drawpixelinfo*)esi;

					esi = (char*)(eax >> 0x1C);
					esi = (char*)RCT2_GLOBAL(0x0097FEFC,uint32)[esi]; // or possibly uint8)[esi*4] ?

					for (; RCT2_GLOBAL(0x009ABDB2, uint16) > 0; RCT2_GLOBAL(0x009ABDB2, uint16)--) {
						// push    ebx
						// push    ecx
						ebp = *(esi + top_*2);
					     
						// mov     bp, [esi+top_*2];
						int ecx;
						ecx = RCT2_GLOBAL(0x00EDF814, uint32);

						for (int i = ecx; i >=0; --i) {
							if (!(ebp & (1 << right_)))
								dpi_->bits = (char*)(left_ & 0xFF);
		
							right_++;
							right_ = right_ & 0xF;
							dpi_++;
						}
						// pop     ecx
						// pop     ebx
						top_++;
						top_ = top_ &0xf;
						dpi_ += RCT2_GLOBAL(0x00EDF810, uint32);
					}
					return;
				}

			} else {
				// 00678B7E   00678C83
				if (dpi->pad_0E < 1) {
					// Location in screen buffer?
					uint8* pixel = top_ * (dpi->width + dpi->pitch) + left_ + dpi->bits;

					// Find colour in colour table?
					uint32 eax = RCT2_ADDRESS(0x0097FCBC, uint32)[(colour & 0xFF)];
					rct_g1_element* g1_element = &(RCT2_ADDRESS(RCT2_ADDRESS_G1_ELEMENTS, rct_g1_element)[eax]);

					int length = (dpi->width + dpi->pitch) - right_;

					// Fill the rectangle with the colours from the colour table
					for (int i = 0; i < bottom_; ++i) {
						for (int j = 0; j < right_; ++j) {
							*pixel = *((uint8*)(&g1_element->offset[*pixel]));
							pixel++;
						}
						pixel += length;
					}
				} else if (dpi->pad_0E > 1) {
					// 00678C8A    00678D57
					right_ = right;
				} else if (dpi->pad_0E == 1) {
					// 00678C88   00678CEE
					right = right;
				}
	
			}
		} else {
			// 00678B3A    00678EC9
			right_ = right;
      }
  } else {
    // 00678B2E    00678BE5
		// Cross hatching
		uint16 pattern = 0;

		left_ = left_ - dpi->x;
		if (left_ < 0) {
			pattern = pattern ^ left_;
			left_ = 0;
		}

		right_ = right_ - dpi->x;
		right_++;

		if (right_ > dpi->width)
			right_ = dpi-> width;
		
		right_ = right_ - left_;

		top_ = top - dpi->y;
		if (top_ < 0) {
			pattern = pattern ^ top_;
			top_ = 0;
		}

		bottom_ = bottom - dpi->y;
		bottom_++;

		if (bottom_ > dpi->height)
			bottom_ = dpi->height;

		bottom_ -= top_;

		uint8* pixel = (top_ * (dpi->width + dpi->pitch)) + left_ + dpi->bits;

		int length = dpi->width + dpi->pitch - right_;

		uint32 ecx;
		for (int i = 0; i < bottom_; ++i) {
			ecx = pattern;
			// Rotate right
			ecx = (ecx >> 1) | (ecx << (sizeof(ecx) * CHAR_BIT - 1));
			ecx = (ecx & 0xFFFF0000) | right_; 
			// Fill every other pixel with the colour
			for (; (ecx & 0xFFFF) > 0; ecx--) {
				ecx = ecx ^ 0x80000000;
				if ((int)ecx < 0) {
					*pixel = colour & 0xFF;
				}
				pixel++;
			}
			pattern = pattern ^ 1;
			pixel += length;
			
		}
	}

	// RCT2_CALLPROC_X(0x00678AD4, left, right, top, bottom, 0, dpi, colour);
}

/**
 *  Draw a rectangle, with optional border or fill
 *
 *  rct2: 0x006E6F81
 * dpi (edi)
 * left (ax)
 * top (cx)
 * right (bx)
 * bottom (dx)
 * colour (ebp)
 * flags (si)
 */
void gfx_fill_rect_inset(rct_drawpixelinfo* dpi, short left, short top, short right, short bottom, int colour, short flags)
{
	uint8 shadow, fill, hilight;

	// Flags
	int no_border, no_fill, pressed;

	no_border = 8;
	no_fill = 0x10;
	pressed = 0x20;

	if (colour & 0x180) {
		if (colour & 0x100) {
			colour = colour & 0x7F;
		} else {
			colour = RCT2_ADDRESS(0x009DEDF4,uint8)[colour];
		}

		colour = colour | 0x2000000; //Transparent

		if (flags & no_border) {
			gfx_fill_rect(dpi, left, top, bottom, right, colour);
		} else if (flags & pressed) {
			// Draw outline of box
			gfx_fill_rect(dpi, left, top, left, bottom, colour + 1);
			gfx_fill_rect(dpi, left, top, right, top, colour + 1);
			gfx_fill_rect(dpi, right, top, right, bottom, colour + 2);
			gfx_fill_rect(dpi, left, bottom, right, bottom, colour + 2);

			if (!(flags & no_fill)) {
				gfx_fill_rect(dpi, left+1, top+1, right-1, bottom-1, colour);
			}
		} else {
			// Draw outline of box
			gfx_fill_rect(dpi, left, top, left, bottom, colour + 2);
			gfx_fill_rect(dpi, left, top, right, top, colour + 2);
			gfx_fill_rect(dpi, right, top, right, bottom, colour + 1);
			gfx_fill_rect(dpi, left, bottom, right, bottom, colour + 1);

			if (!(flags & no_fill)) {
				gfx_fill_rect(dpi, left+1, top+1, right-1, bottom-1, colour);
			}
		}
	} else {
		if (flags & 0x80) {
			shadow	= RCT2_ADDRESS(0x0141FC46, uint8)[colour * 8];
			fill	= RCT2_ADDRESS(0x0141FC48, uint8)[colour * 8];
			hilight	= RCT2_ADDRESS(0x0141FC4A, uint8)[colour * 8];
		} else {
			shadow	= RCT2_ADDRESS(0x0141FC47, uint8)[colour * 8];
			fill	= RCT2_ADDRESS(0x0141FC49, uint8)[colour * 8];
			hilight	= RCT2_ADDRESS(0x0141FC4B, uint8)[colour * 8];
		}

		if (flags & no_border) {
			gfx_fill_rect(dpi, left, top, right, bottom, fill);
		} else if (flags & pressed) {
			// Draw outline of box
			gfx_fill_rect(dpi, left, top, left, bottom, shadow);
			gfx_fill_rect(dpi, left + 1, top, right, top, shadow);
			gfx_fill_rect(dpi, right, top + 1, right, bottom - 1, hilight);
			gfx_fill_rect(dpi, left + 1, bottom, right, bottom, hilight);

			if (!(flags & no_fill)) {
				if (!(flags & 0x40)) {
					if (flags & 0x04) {
						fill = RCT2_ADDRESS(0x0141FC49, uint8)[0];
					} else {
						fill = RCT2_ADDRESS(0x0141FC4A, uint8)[colour * 8];
					}
				}
				gfx_fill_rect(dpi, left+1, top+1, right-1, bottom-1, fill);
			}
		} else {
			// Draw outline of box
			gfx_fill_rect(dpi, left, top, left, bottom - 1, hilight);
			gfx_fill_rect(dpi, left + 1, top, right - 1, top, hilight);
			gfx_fill_rect(dpi, right, top, right, bottom - 1, shadow);
			gfx_fill_rect(dpi, left, bottom, right, bottom, shadow);

			if (!(flags & no_fill)) {
				if (flags & 0x04) {
					fill = RCT2_ADDRESS(0x0141FC49, uint8)[0];
				}
				gfx_fill_rect(dpi, left+1, top+1, right-1, bottom-1, fill);
			}
		}
	}
}

#define RCT2_Y_RELATED_GLOBAL_1 0x9E3D12 //uint16
#define RCT2_Y_END_POINT_GLOBAL 0x9ABDAC //sint16
#define RCT2_Y_START_POINT_GLOBAL 0xEDF808 //sint16
#define RCT2_X_RELATED_GLOBAL_1 0x9E3D10 //uint16
#define RCT2_X_END_POINT_GLOBAL 0x9ABDA8 //sint16
#define RCT2_X_START_POINT_GLOBAL 0xEDF80C //sint16
#define RCT2_DPI_LINE_LENGTH_GLOBAL 0x9ABDB0 //uint16 width+pitch

/*
* rct2: 0x67A690
* copies a sprite onto the buffer. There is no compression used on the sprite
* image.
*/
void gfx_bmp_sprite_to_buffer(uint8* palette_pointer, uint8* source_pointer, uint8* dest_pointer, rct_g1_element* source_image, rct_drawpixelinfo *dest_dpi, int height, int width, int image_type){
	//Requires use of palette?
	if (image_type & IMAGE_TYPE_USE_PALETTE){

		//Mix with another image?? and colour adjusted
		if (RCT2_GLOBAL(0x9E3CDC, uint32)){ //Not tested. I can't actually work out when this code runs.

			uint32 _ebp = RCT2_GLOBAL(0x9E3CDC, uint32);
			_ebp += source_pointer - source_image->offset;// RCT2_GLOBAL(0x9E3CE0, uint32);

			for (; height > 0; --height){
				for (int no_pixels = width; no_pixels > 0; --no_pixels){
					uint8 pixel = *source_pointer;
					source_pointer++;
					pixel = palette_pointer[pixel];
					pixel &= *((uint8*)_ebp);
					if (pixel){
						*dest_pointer = pixel;
					}
					dest_pointer++;
					_ebp++;
				}
				source_pointer += source_image->width - width;
				dest_pointer += dest_dpi->width + dest_dpi->pitch - width;
				_ebp += source_image->width - width;
			}
			return;
		}

		//Quicker?
		if (width == 4){
			
			for (; height > 0; --height){
				for (int no_pixels = 0; no_pixels < 4; ++no_pixels){
					uint8 pixel = source_pointer[no_pixels];
					pixel = palette_pointer[pixel];
					if (pixel){
						dest_pointer[no_pixels] = pixel;
					}
				}
				dest_pointer += dest_dpi->width + dest_dpi->pitch - width + 4;
				source_pointer += source_image->width - width + 4;
			}
			return;
		}

		//image colour adjusted?
		for (; height > 0; --height){
			for (int no_pixels = width; no_pixels > 0; --no_pixels){
				uint8 pixel = *source_pointer;
				source_pointer++;
				pixel = palette_pointer[pixel];
				if (pixel){
					*dest_pointer = pixel;
				}
				dest_pointer++;
			}
			source_pointer += source_image->width - width;
			dest_pointer += dest_dpi->width + dest_dpi->pitch - width;
		}
		return;
	}

	//Mix with background. It only uses source pointer for
	//telling if it needs to be drawn not for colour.
	if (image_type & IMAGE_TYPE_MIX_BACKGROUND){//Not tested
		for (; height > 0; --height){
			for (int no_pixels = width; no_pixels > 0; --no_pixels){
				uint8 pixel = *source_pointer;
				source_pointer++;
				if (pixel){
					pixel = *dest_pointer;
					pixel = palette_pointer[pixel];
					*dest_pointer = pixel;
				}
				dest_pointer++;
			}
			source_pointer += source_image->width - width;
			dest_pointer += dest_dpi->width + dest_dpi->pitch - width;
		}
		return;
	}

	//Basic bitmap no fancy stuff
	if (!(source_image->flags & G1_FLAG_BMP)){//Not tested
		for (; height > 0; --height){
			memcpy(dest_pointer, source_pointer, width);
			dest_pointer += dest_dpi->width + dest_dpi->pitch;
			source_pointer += source_image->width;
		}
		return;
	}

	if (RCT2_GLOBAL(0x9E3CDC, uint32) != 0){//Not tested. I can't actually work out when this code runs.
		uint32 _ebp = RCT2_GLOBAL(0x9E3CDC, uint32);
		_ebp += source_pointer - source_image->offset;

		for (; height > 0; --height){
			for (int no_pixels = width; no_pixels > 0; --no_pixels){
				uint8 pixel = *source_pointer;
				source_pointer++;
				pixel &= *((char*)_ebp);
				if (pixel){
					*dest_pointer = pixel;
				}
				dest_pointer++;
				_ebp++;
			}
			source_pointer += source_image->width - width;
			dest_pointer += dest_dpi->width + dest_dpi->pitch - width;
			_ebp += source_image->width - width;
		}
	}

	//Basic bitmap with no draw pixels
	for (int no_lines = height; no_lines > 0; --no_lines){
		for (int no_pixels = width; no_pixels > 0; --no_pixels){
			uint8 pixel = *source_pointer;
			source_pointer++;
			if (pixel){
				*dest_pointer = pixel;
			}
			dest_pointer++;
		}
		source_pointer += source_image->width - width;
		dest_pointer += dest_dpi->width + dest_dpi->pitch - width;
	}
	return;
}

/*
* rct2: 0x67AA18 transfers readied images onto buffers
* This function copies the sprite data onto the screen
* I think its only used for bitmaps onto buttons but i am not sure.
* There is still a small bug with this code when it is in the choose park view.
*/
void gfx_rle_sprite_to_buffer(uint8* source_bits_pointer, uint8* dest_bits_pointer, uint8* palette_pointer, rct_drawpixelinfo *dpi, int image_type, int source_y_start, int height, int source_x_start, int width){
	uint16 offset_to_first_line = ((uint16*)source_bits_pointer)[source_y_start];
	//This will now point to the first line
	uint8* next_source_pointer = source_bits_pointer + offset_to_first_line;
	uint8* next_dest_pointer = dest_bits_pointer;
	
	//For every line in the image
	for (; height; height--){

		uint8 last_data_line = 0;
		//For every data section in the line
		while (!last_data_line){
			uint8* source_pointer = next_source_pointer;
			uint8* dest_pointer = next_dest_pointer;

			int no_pixels = *source_pointer++;
			//gap_size is the number of non drawn pixels you require to
			//jump over on your destination
			uint8 gap_size = *source_pointer++;
			//The last bit in no_pixels tells you if you have reached the end of a line
			last_data_line = no_pixels & 0x80;
			//Clear the last data line bit so we have just the no_pixels
			no_pixels &= 0x7f;
			//Have our next source pointer point to the next data section
			next_source_pointer = source_pointer + no_pixels;

			//Calculates the start point of the image
			int x_start = gap_size - source_x_start;

			if (x_start > 0){
				//Since the start is positive
				//We need to move the drawing surface to the correct position
				dest_pointer += x_start;
			}
			else{
				//If the start is negative we require to remove part of the image.
				//This is done by moving the image pointer to the correct position.
				source_pointer -= x_start;
				//The no_pixels will be reduced in this operation
				no_pixels += x_start;
				//If there are no pixels there is nothing to draw this line
				if (no_pixels <= 0) continue;
				//Reset the start position to zero as we have taken into account all moves
				x_start = 0;
			}

			int x_end = x_start + no_pixels;
			//If the end position is further out than the whole image
			//end position then we need to shorten the line again
			if (x_end > width){
				//Shorten the line
				no_pixels -= x_end - width;
				//If there are no pixels there is nothing to draw.
				if (no_pixels <= 0) continue;
			}

			//Finally after all those checks, copy the image onto the drawing surface
			//If the image type is not a basic one we require to mix the pixels
			if (image_type & IMAGE_TYPE_USE_PALETTE){//In the .exe these are all unraveled loops
				for (; no_pixels > 0; --no_pixels, source_pointer++, dest_pointer++){
					uint8 al = *source_pointer;
					uint8 ah = *dest_pointer;
					if (image_type & IMAGE_TYPE_MIX_BACKGROUND)//Mix with background and image Not Tested
						al = palette_pointer[(al | ((int)ah) << 8) - 0x100];
					else //Adjust colours?
						al = palette_pointer[al];
					*dest_pointer = al;
				}
			}
			else if (image_type & IMAGE_TYPE_MIX_BACKGROUND){//In the .exe these are all unraveled loops
				//Doesnt use source pointer ??? mix with background only?
				//Not Tested
				for (; no_pixels > 0; --no_pixels, source_pointer++, dest_pointer++){
					uint8 pixel = *dest_pointer;
					pixel = palette_pointer[pixel];
					*dest_pointer = pixel;
				}
			}
			else
			{
				memcpy(dest_pointer, source_pointer, no_pixels);
			}
			
		}
		//Add a line to the drawing surface pointer
		next_dest_pointer += (int)dpi->width + (int)dpi->pitch;
	}
}

void gfx_draw_sprite_palette_set(rct_drawpixelinfo *dpi, int image_id, int x, int y, uint8* palette_pointer, int ebp, int eax);
/**
 *
 *  rct2: 0x0067A28E
 * image_id (ebx)
 * x (cx)
 * y (dx)
 */
void gfx_draw_sprite(rct_drawpixelinfo *dpi, int image_id, int x, int y)
{

	int eax = 0, ebx = image_id, ecx = x, edx = y, esi = 0, edi = (int)dpi, ebp = 0;
	int image_type = (image_id & 0xE0000000) >> 28;
	int image_sub_type = (image_id & 0x1C000000) >> 26;
	uint8* palette_pointer = NULL;
	uint8 palette[0x100];
	RCT2_GLOBAL(0x00EDF81C, uint32) = image_id & 0xE0000000;
	eax = (image_id >> 26) & 0x7;

	RCT2_GLOBAL(0x009E3CDC, uint32) = RCT2_GLOBAL(0x009E3CE4 + eax * 4, uint32);

	if (image_type && !(image_type & IMAGE_TYPE_UNKNOWN)) {

		if (!(image_type & IMAGE_TYPE_MIX_BACKGROUND)){
			eax = image_id;
			eax >>= 19;
			eax &= 0xFF;
			RCT2_GLOBAL(0x009E3CDC, uint32) = 0;
		}
		else{
			eax = image_id;
			eax >>= 19;
			eax &= 0x7F;
		}
		eax = RCT2_GLOBAL(eax * 4 + 0x97FCBC, uint32);

		palette_pointer = ((rct_g1_element*)RCT2_ADDRESS_G1_ELEMENTS)[eax].offset;
		RCT2_GLOBAL(0x9ABDA4, uint32) = palette_pointer;
	}
	else if (image_type && !(image_type & IMAGE_TYPE_USE_PALETTE)){
		//Has not been tested
		eax = image_id;
		RCT2_GLOBAL(0x9E3CDC, uint32) = 0;
		eax >>= 19;
		//push edx/y
		eax &= 0x1F;
		ebp = RCT2_GLOBAL(ebp * 4 + 0x97FCBC, uint32);
		eax = RCT2_GLOBAL(eax * 4 + 0x97FCBC, uint32);
		ebp <<= 0x4;
		eax <<= 0x4;
		ebp = RCT2_GLOBAL(ebp + RCT2_ADDRESS_G1_ELEMENTS, uint32);
		eax = RCT2_GLOBAL(eax + RCT2_ADDRESS_G1_ELEMENTS, uint32);
		edx = *((uint32*)(eax + 0xF3));
		esi = *((uint32*)(eax + 0xF7));
		RCT2_GLOBAL(0x9ABFFF, uint32) = edx;
		RCT2_GLOBAL(0x9AC003, uint32) = esi;
		edx = *((uint32*)(ebp + 0xF3));
		esi = *((uint32*)(ebp + 0xF7));
		esi = *((uint32*)(eax + 0xF7));

		edx = *((uint32*)(eax + 0xFB));
		esi = *((uint32*)(ebp + 0xFB));

		eax = image_id;
		RCT2_GLOBAL(0x9AC007, uint32) = edx;
		eax >>= 24;
		RCT2_GLOBAL(0x9ABF42, uint32) = esi;
		eax &= 0x1F;

		//image_id
		RCT2_GLOBAL(0xEDF81C, uint32) |= 0x20000000;
		image_type |= IMAGE_TYPE_USE_PALETTE;

		eax = RCT2_GLOBAL(eax * 4 + 0x97FCBC, uint32);
		eax <<= 4;
		eax = RCT2_GLOBAL(eax + RCT2_ADDRESS_G1_ELEMENTS, uint32);
		edx = *((uint32*)(eax + 0xF3));
		esi = *((uint32*)(eax + 0xF7));
		RCT2_GLOBAL(0x9ABFD6, uint32) = edx;
		RCT2_GLOBAL(0x9ABFDA, uint32) = esi;
		edx = *((uint32*)(eax + 0xFB));
		RCT2_GLOBAL(0x9ABDA4, uint32) = 0x9ABF0C;
		palette_pointer = (uint8*)0x9ABF0C;
		RCT2_GLOBAL(0x9ABFDE, uint32) = edx;
		edx = y;

	}
	else if (image_type){
		eax = image_id;
		RCT2_GLOBAL(0x9E3CDC, uint32) = 0;
		eax >>= 19;
		eax &= 0x1f;
		eax = RCT2_GLOBAL(eax * 4 + 0x97FCBC, uint32);
		eax <<= 4;
		rct_g1_element g1_palette = RCT2_GLOBAL(eax + RCT2_ADDRESS_G1_ELEMENTS, rct_g1_element);
		eax = RCT2_GLOBAL(eax + RCT2_ADDRESS_G1_ELEMENTS, uint32);
		memcpy(palette, g1_palette.offset, 0x100);
		//G1_element bits?
		ebp = *((uint32*)(eax + 0xF3));
		esi = *((uint32*)(eax + 0xF7));
		//*(uint32*)(palette+0xF3) = ebp;
		//*(uint32*)(palette+0xF7) = esi;
		RCT2_GLOBAL(0x9ABEFF, uint32) = ebp;
		RCT2_GLOBAL(0x9ABF03, uint32) = esi;
		ebp = *((uint32*)(eax + 0xFB));
		eax = ebx;
		//*(uint32*)(palette+0xFB) = ebp;
		RCT2_GLOBAL(0x9ABF07, uint32) = ebp;
		eax >>= 24;
		eax &= 0x1f;
		eax = RCT2_GLOBAL(eax * 4 + 0x97FCBC, uint32);
		eax <<= 4;
		eax = RCT2_GLOBAL(eax + RCT2_ADDRESS_G1_ELEMENTS, uint32);
		ebp = *((uint32*)(eax + 0xF3));
		esi = *((uint32*)(eax + 0xF7));
		*(uint32*)(palette+0xCA) = ebp;
		*(uint32*)(palette+0xCE) = esi;
		RCT2_GLOBAL(0x9ABED6, uint32) = ebp;
		RCT2_GLOBAL(0x9ABEDA, uint32) = esi;
		ebp = *((uint32*)(eax + 0xFB));

		RCT2_GLOBAL(0x9ABDA4, uint32) = 0x009ABE0C;
		palette_pointer = palette;// (uint8*)0x9ABE0C;
		*(uint32*)(palette+0xD2) = ebp;
		RCT2_GLOBAL(0x9ABEDE, uint32) = ebp;
	}

	gfx_draw_sprite_palette_set(dpi, image_id, x, y, palette_pointer, ebp, eax);
}

void gfx_draw_sprite_palette_set(rct_drawpixelinfo *dpi, int image_id, int x, int y, uint8* palette_pointer, int ebp, int eax){
	int image_element = 0x7FFFF&image_id;
	int image_type = (image_id & 0xE0000000) >> 28;

	rct_g1_element* g1_source = &((rct_g1_element*)RCT2_ADDRESS_G1_ELEMENTS)[image_element];

	if (dpi->pad_0E >= 1){ //These have not been tested
		//something to do with zooming
		if (dpi->pad_0E == 1){
			RCT2_CALLPROC_X(0x0067BD81, eax, (int)g1_source, x, y, 0,(int) dpi, ebp);
			return;
		}
		if (dpi->pad_0E == 2){
			RCT2_CALLPROC_X(0x0067DADA, eax, (int)g1_source, x, y, 0, (int)dpi, ebp);
			return;
		}
		RCT2_CALLPROC_X(0x0067FAAE, eax, (int)g1_source, x, y, 0, (int)dpi, ebp);
		return;
	}

	//This will be the height of the drawn image
	int height = g1_source->height;
	//This is the start y coordinate on the destination
	int dest_start_y = y - dpi->y + g1_source->y_offset;
	//This is the start y coordinate on the source
	int source_start_y = 0;

	
	if (dest_start_y < 0){
		//If the destination y is negative reduce the height of the
		//image as we will cut off the bottom
		height += dest_start_y;
		//If the image is no longer visible nothing to draw
		if (height <= 0){
			return;
		}
		//The source image will start a further up the image
		source_start_y -= dest_start_y;
		//The destination start is now reset to 0
		dest_start_y = 0;
	}

	int dest_end_y = dest_start_y + height;

	if (dest_end_y > dpi->height){
		//If the destination y is outside of the drawing
		//image reduce the height of the image
		height -= dest_end_y - dpi->height;
		//If the image no longer has anything to draw
		if (height <= 0)return;
	}
	
	//This will be the width of the drawn image
	int width = g1_source->width;
	//This is the source start x coordinate
	int source_start_x = 0;
	//This is the destination start x coordinate
	int dest_start_x = x - dpi->x + g1_source->x_offset;
	
	if (dest_start_x < 0){
		//If the destination is negative reduce the width
		//image will cut off the side
		width += dest_start_x;
		//If there is no image to draw
		if (width <= 0){
			return;
		}
		//The source start will also need to cut off the side
		source_start_x -= dest_start_x;
		//Reset the destination to 0
		dest_start_x = 0;
	}

	int dest_end_x = dest_start_x + width;

	if (dest_end_x > dpi->width){
		//If the destination x is outside of the drawing area
		//reduce the image width.
		width -= dest_end_x - dpi->width;
		//If there is no image to draw.
		if (width <= 0)return;
	}

	
	uint8* dest_pointer = (uint8*)dpi->bits;
	//Move the pointer to the start point of the destination
	dest_pointer += (dpi->width + dpi->pitch)*dest_start_y + dest_start_x;

	if (g1_source->flags & G1_FLAG_RLE_COMPRESSION){
		//We have to use a different method to move the source pointer for
		//rle encoded sprites so that will be handled within this function
		gfx_rle_sprite_to_buffer(g1_source->offset, dest_pointer, palette_pointer, dpi, image_type, source_start_y, height, source_start_x, width);
		return;
	}

	uint8* source_pointer = g1_source->offset;
	//Move the pointer to the start point of the source
	source_pointer += g1_source->width*source_start_y + source_start_x;

	if (!(g1_source->flags & 0x02)){
		gfx_bmp_sprite_to_buffer(palette_pointer, source_pointer, dest_pointer, g1_source, dpi, height, width, image_type);
		return;
	}
	//0x67A60A
	int total_no_pixels = g1_source->width*g1_source->height;
	source_pointer = g1_source->offset;
	uint8* new_source_pointer_start = malloc(total_no_pixels);
	uint8* new_source_pointer = new_source_pointer_start;// 0x9E3D28;
	int ebx, ecx;
	while (total_no_pixels>0){
		sint8 no_pixels = *source_pointer;
		if (no_pixels >= 0){
			source_pointer++;
			total_no_pixels -= no_pixels;
			memcpy((char*)new_source_pointer, (char*)source_pointer, no_pixels);
			new_source_pointer += no_pixels;
			source_pointer += no_pixels;
			continue;
		}
		ecx = no_pixels;
		no_pixels &= 0x7;
		ecx >>= 3;//SAR
		eax <<= 8;
		ecx = -ecx;//Odd
		eax = eax & 0xFF00 + *(source_pointer+1);
		total_no_pixels -= ecx;
		source_pointer += 2;
		ebx = new_source_pointer - eax;
		eax = source_pointer;
		source_pointer = ebx;
		ebx = eax;
		eax = 0;
		memcpy((char*)new_source_pointer, (char*)source_pointer, ecx);
		new_source_pointer += ecx;
		source_pointer += ecx;
		source_pointer = ebx;
	}
	source_pointer = new_source_pointer_start + g1_source->width*source_start_y + source_start_x;
	gfx_bmp_sprite_to_buffer(palette_pointer, source_pointer, dest_pointer, g1_source, dpi, height, width, image_type);
	free(new_source_pointer_start);
	return;
}

/**
 *
 *  rct2: 0x00683854
 * a1 (ebx)
 * product (cl)
 */
void gfx_transpose_palette(int pal, unsigned char product)
{
	int eax, ebx, ebp;
	uint8* esi, *edi;

	ebx = pal * 16;
	esi = (uint8*)(*((int*)(RCT2_ADDRESS_G1_ELEMENTS + ebx)));
	ebp = *((short*)(0x009EBD2C + ebx));
	eax = *((short*)(0x009EBD30 + ebx)) * 4;
	edi = (uint8*)0x01424680 + eax;

	for (; ebp > 0; ebp--) {
		edi[0] = (esi[0] * product) >> 8;
		edi[1] = (esi[1] * product) >> 8;
		edi[2] = (esi[2] * product) >> 8;
		esi += 3;
		edi += 4;
	}
	osinterface_update_palette((char*)0x01424680, 10, 236);
}

/**
 * Draws i formatted text string centred at i specified position.
 *  rct2: 0x006C1D6C
 * dpi (edi)
 * format (bx)
 * x (cx)
 * y (dx)
 * colour (al)
 * args (esi)
 */
void gfx_draw_string_centred(rct_drawpixelinfo *dpi, int format, int x, int y, int colour, void *args)
{
	RCT2_CALLPROC_X(0x006C1D6C, colour, format, x, y, (int)args, (int)dpi, 0);
}

/**
 *
 *  rct2: 0x006ED7E5
 */
void gfx_invalidate_screen()
{
	int width = RCT2_GLOBAL(RCT2_ADDRESS_SCREEN_WIDTH, sint16);
	int height = RCT2_GLOBAL(RCT2_ADDRESS_SCREEN_HEIGHT, sint16);
	gfx_set_dirty_blocks(0, 0, width, height);
}

/**
 * 
 *  rct2: 0x006E732D
 * left (ax)
 * top (bx)
 * right (dx)
 * bottom (bp)
 */
void gfx_set_dirty_blocks(int left, int top, int right, int bottom)
{
	int x, y;
	uint8 *screenDirtyBlocks = RCT2_ADDRESS(0x00EDE408, uint8);

	left = max(left, 0);
	top = max(top, 0);
	right = min(right, RCT2_GLOBAL(RCT2_ADDRESS_SCREEN_WIDTH, sint16));
	bottom = min(bottom, RCT2_GLOBAL(RCT2_ADDRESS_SCREEN_HEIGHT, sint16));

	if (left >= right)
		return;
	if (top >= bottom)
		return;

	right--;
	bottom--;

	left >>= RCT2_GLOBAL(0x009ABDF0, sint8);
	right >>= RCT2_GLOBAL(0x009ABDF0, sint8);
	top >>= RCT2_GLOBAL(0x009ABDF1, sint8);
	bottom >>= RCT2_GLOBAL(0x009ABDF1, sint8);

	for (y = top; y <= bottom; y++)
		for (x = left; x <= right; x++)
			screenDirtyBlocks[y * RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_COLUMNS, sint32) + x] = 0xFF;
}

/**
 *
 *  rct2: 0x006E73BE
 */
void gfx_draw_all_dirty_blocks()
{
	int x, y, xx, yy, columns, rows;
	uint8 *screenDirtyBlocks = RCT2_ADDRESS(0x00EDE408, uint8);

	for (x = 0; x < RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_COLUMNS, sint32); x++) {
		for (y = 0; y < RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_ROWS, sint32); y++) {
			if (screenDirtyBlocks[y * RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_COLUMNS, sint32) + x] == 0)
				continue;

			// Determine columns
			for (xx = x; xx < RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_COLUMNS, sint32); xx++)
				if (screenDirtyBlocks[y * RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_COLUMNS, sint32) + xx] == 0)
					break;
			columns = xx - x;

			// Check rows
			for (yy = y; yy < RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_ROWS, sint32); yy++)
				for (xx = x; xx < x + columns; xx++)
					if (screenDirtyBlocks[yy * RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_COLUMNS, sint32) + xx] == 0)
						goto endRowCheck;
			
		endRowCheck:
			rows = yy - y;
			gfx_draw_dirty_blocks(x, y, columns, rows);
		}
	}
}

static void gfx_draw_dirty_blocks(int x, int y, int columns, int rows)
{
	int left, top, right, bottom;
	uint8 *screenDirtyBlocks = RCT2_ADDRESS(0x00EDE408, uint8);

	// Unset dirty blocks
	for (top = y; top < y + rows; top++)
		for (left = x; left < x + columns; left++)
			screenDirtyBlocks[top * RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_COLUMNS, sint32) + left] = 0;

	// Determine region in pixels
	left = max(0, x * RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_WIDTH, sint16));
	top = max(0, y * RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_HEIGHT, sint16));
	right = min(RCT2_GLOBAL(RCT2_ADDRESS_SCREEN_WIDTH, sint16), left + (columns * RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_WIDTH, sint16)));
	bottom = min(RCT2_GLOBAL(RCT2_ADDRESS_SCREEN_HEIGHT, sint16), top + (rows * RCT2_GLOBAL(RCT2_ADDRESS_DIRTY_BLOCK_HEIGHT, sint16)));
	if (right <= left || bottom <= top)
		return;

	// Draw region
	gfx_redraw_screen_rect(left, top, right, bottom);
}

/**
 * 
 *  rct2: 0x006E7499 
 * left (ax)
 * top (bx)
 * right (dx)
 * bottom (bp)
 */
void gfx_redraw_screen_rect(short left, short top, short right, short bottom)
{
	rct_window* w;
	rct_drawpixelinfo *screenDPI = RCT2_ADDRESS(RCT2_ADDRESS_SCREEN_DPI, rct_drawpixelinfo);
	rct_drawpixelinfo *windowDPI = RCT2_ADDRESS(RCT2_ADDRESS_WINDOW_DPI, rct_drawpixelinfo);

	// Unsure what this does
	RCT2_CALLPROC_X(0x00683326, left, top, right - 1, bottom - 1, 0, 0, 0);

	windowDPI->bits = screenDPI->bits + left + ((screenDPI->width + screenDPI->pitch) * top);
	windowDPI->x = left;
	windowDPI->y = top;
	windowDPI->width = right - left;
	windowDPI->height = bottom - top;
	windowDPI->pitch = screenDPI->width + screenDPI->pitch + left - right;

	for (w = RCT2_ADDRESS(RCT2_ADDRESS_WINDOW_LIST, rct_window); w < RCT2_GLOBAL(RCT2_ADDRESS_NEW_WINDOW_PTR, rct_window*); w++) {
		if (w->flags & WF_TRANSPARENT)
			continue;
		if (right <= w->x || bottom <= w->y)
			continue;
		if (left >= w->x + w->width || top >= w->y + w->height)
			continue;
		window_draw(w, left, top, right, bottom);
	}
}

/**
 * 
 *  rct2: 0x006C2321
 * buffer (esi)
 */
int gfx_get_string_width(char *buffer)
{
	int eax, ebx, ecx, edx, esi, edi, ebp;

	esi = (int)buffer;
	RCT2_CALLFUNC_X(0x006C2321, &eax, &ebx, &ecx, &edx, &esi, &edi, &ebp);

	return ecx & 0xFFFF;
}

/**
 * Draws i formatted text string left aligned at i specified position but clips
 * the text with an elipsis if the text width exceeds the specified width.
 *  rct2: 0x006C1B83
 * dpi (edi)
 * format (bx)
 * args (esi)
 * colour (al)
 * x (cx)
 * y (dx)
 * width (bp)
 */
void gfx_draw_string_left_clipped(rct_drawpixelinfo* dpi, int format, void* args, int colour, int x, int y, int width)
{
	RCT2_CALLPROC_X(0x006C1B83, colour, format, x, y, (int)args, (int)dpi, width);

	//char* buffer;

	//buffer = (char*)0x0141ED68;
	//format_string(buffer, format, args);
	//rctmem->current_font_sprite_base = 224;
	//clip_text(buffer, width);
	//gfx_draw_string(dpi, buffer, colour, x, y);
}

/**
 * Draws i formatted text string centred at i specified position but clips the
 * text with an elipsis if the text width exceeds the specified width.
 *  rct2: 0x006C1BBA
 * dpi (edi)
 * format (bx)
 * args (esi)
 * colour (al)
 * x (cx)
 * y (dx)
 * width (bp)
 */
void gfx_draw_string_centred_clipped(rct_drawpixelinfo *dpi, int format, void *args, int colour, int x, int y, int width)
{
	RCT2_CALLPROC_X(0x006C1BBA, colour, format, x, y, (int)args, (int)dpi, width);

	//char* buffer;
	//short text_width;

	//buffer = (char*)0x0141ED68;
	//format_string(buffer, format, args);
	//rctmem->current_font_sprite_base = 224;
	//text_width = clip_text(buffer, width);

	//// Draw the text centred
	//x -= (text_width - 1) / 2;
	//gfx_draw_string(dpi, buffer, colour, x, y);
}


/**
 * Draws i formatted text string right aligned.
 *  rct2: 0x006C1BFC
 * dpi (edi)
 * format (bx)
 * args (esi)
 * colour (al)
 * x (cx)
 * y (dx)
 */
void gfx_draw_string_right(rct_drawpixelinfo* dpi, int format, void* args, int colour, int x, int y)
{
	char* buffer;
	short text_width;

	buffer = (char*)0x0141ED68;
	format_string(buffer, format, args);

	// Measure text width
	text_width = gfx_get_string_width(buffer);

	// Draw the text right aligned
	x -= text_width;
	gfx_draw_string(dpi, buffer, colour, x, y);
}

/**
 * 
 *  rct2: 0x006C1E53
 * dpi (edi)
 * args (esi)
 * x (cx)
 * y (dx)
 * width (bp)
 * colour (al)
 * format (ebx)
 */
int gfx_draw_string_centred_wrapped(rct_drawpixelinfo *dpi, void *args, int x, int y, int width, int format, int colour)
{
	int eax, ebx, ecx, edx, esi, edi, ebp;

	eax = colour;
	ebx = format;
	ecx = x;
	edx = y;
	esi = (int)args;
	edi = (int)dpi;
	ebp = width;
	RCT2_CALLFUNC_X(0x006C1E53, &eax, &ebx, &ecx, &edx, &esi, &edi, &ebp);

	return (sint16)(edx & 0xFFFF) - y;
}

/**
 * 
 *  rct2: 0x006C2105
 * dpi (edi)
 * args (esi)
 * x (cx)
 * y (dx)
 * width (bp)
 * format (bx)
 * colour (al)
 */
int gfx_draw_string_left_wrapped(rct_drawpixelinfo *dpi, void *args, int x, int y, int width, int format, int colour)
{
	int eax, ebx, ecx, edx, esi, edi, ebp;

	eax = colour;
	ebx = format;
	ecx = x;
	edx = y;
	esi = (int)args;
	edi = (int)dpi;
	ebp = width;
	RCT2_CALLFUNC_X(0x006C2105, &eax, &ebx, &ecx, &edx, &esi, &edi, &ebp);

	return (sint16)(edx & 0xFFFF) - y;
}

/**
 * Draws i formatted text string.
 *  rct2: 0x006C1B2F
 * dpi (edi)
 * format (bx)
 * args (esi)
 * colour (al)
 * x (cx)
 * y (dx)
 */
void gfx_draw_string_left(rct_drawpixelinfo *dpi, int format, void *args, int colour, int x, int y)
{
	char* buffer;

	buffer = (char*)0x0141ED68;
	format_string(buffer, format, args);
	gfx_draw_string(dpi, buffer, colour, x, y);
}

/**
 * 
 *  rct2: 0x00682702
 * dpi (edi)
 * format (esi)
 * colour (al)
 * x (cx)
 * y (dx)
 */
void gfx_draw_string(rct_drawpixelinfo *dpi, char *format, int colour, int x, int y)
{
	int eax, ebx, ecx, edx, esi, edi, ebp;

	eax = colour;
	ebx = 0;
	ecx = x;
	edx = y;
	esi = (int)format;
	edi = (int)dpi;
	ebp = 0;
	RCT2_CALLFUNC_X(0x00682702, &eax, &ebx, &ecx, &edx, &esi, &edi, &ebp);

	gLastDrawStringX = ecx;
	gLastDrawStringY = edx;
}
