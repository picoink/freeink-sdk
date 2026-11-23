/* SPDX-License-Identifier: Apache-2.0
 * Adapted from PicoInk's POSIX JPEG cache encoder. Host-only; never advertises
 * hardware acceleration. Uses the same baseline gray format as the S31 path. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <jpeglib.h>
#include <jerror.h>
#include <setjmp.h>
typedef struct { struct jpeg_error_mgr error; jmp_buf jump; } EncodeError;
static void failed(j_common_ptr c) { longjmp(((EncodeError*)c->err)->jump,1); }
static void init_output(j_compress_ptr c) { (void)c; }
static boolean full_output(j_compress_ptr c) { ERREXIT(c,JERR_BUFFER_SIZE); return FALSE; }
static void end_output(j_compress_ptr c) { (void)c; }
bool freeink_host_encode_gray(const uint8_t* gray,uint16_t w,uint16_t h,unsigned quality,
                              uint8_t* out,size_t capacity,size_t* written) {
    *written=0;
    struct jpeg_compress_struct c={0}; EncodeError error;
    c.err=jpeg_std_error(&error.error); error.error.error_exit=failed;
    if (setjmp(error.jump)) { jpeg_destroy_compress(&c); return false; }
    jpeg_create_compress(&c);
    struct jpeg_destination_mgr dest={.next_output_byte=out,.free_in_buffer=capacity,
        .init_destination=init_output,.empty_output_buffer=full_output,.term_destination=end_output};
    c.dest=&dest; c.image_width=w; c.image_height=h; c.input_components=1; c.in_color_space=JCS_GRAYSCALE;
    jpeg_set_defaults(&c); jpeg_set_quality(&c,(int)quality,TRUE);
    jpeg_start_compress(&c,TRUE);
    while (c.next_scanline<c.image_height) {
        JSAMPROW row=(JSAMPROW)(gray+(size_t)c.next_scanline*w); jpeg_write_scanlines(&c,&row,1);
    }
    jpeg_finish_compress(&c); *written=capacity-dest.free_in_buffer;
    jpeg_destroy_compress(&c); return true;
}
