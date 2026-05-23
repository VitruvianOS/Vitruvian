/*
 * Copyright (c) 2003-4 Kian Duffy <myob@users.sourceforge.net>
 * Parts Copyright (C) 1998,99 Kazuho Okui and Takashi Murai.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files or portions
 * thereof (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice
 *    in the  binary, as well as this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided with
 *    the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#define CASE_IGNORE 0
#define CASE_FUNCTION 1
#define CASE_UP_ARROW 2
#define CASE_DOWN_ARROW 3
#define CASE_RIGHT_ARROW 4
#define CASE_LEFT_ARROW 5
#define CASE_RET_ENTR 6

/* VOS: physical key codes are raw Linux evdev codes */
#define F1_KEY  59		/* KEY_F1 */
#define F2_KEY  60		/* KEY_F2 */
#define F3_KEY  61		/* KEY_F3 */
#define F4_KEY  62		/* KEY_F4 */
#define F5_KEY  63		/* KEY_F5 */
#define F6_KEY  64		/* KEY_F6 */
#define F7_KEY  65		/* KEY_F7 */
#define F8_KEY  66		/* KEY_F8 */
#define F9_KEY  67		/* KEY_F9 */
#define F10_KEY 68		/* KEY_F10 */
#define F11_KEY 87		/* KEY_F11 */
#define F12_KEY 88		/* KEY_F12 */

#define RETURN_KEY  28		/* KEY_ENTER */
#define ENTER_KEY   96		/* KEY_KPENTER */

#define LEFT_ARROW_KEY  105	/* KEY_LEFT */
#define RIGHT_ARROW_KEY 106	/* KEY_RIGHT */
#define UP_ARROW_KEY    103	/* KEY_UP */
#define DOWN_ARROW_KEY  108	/* KEY_DOWN */

#define HOME_KEY      102	/* KEY_HOME */
#define INSERT_KEY    110	/* KEY_INSERT */
#define END_KEY       107	/* KEY_END */
#define PAGE_UP_KEY   104	/* KEY_PAGEUP */
#define PAGE_DOWN_KEY 109	/* KEY_PAGEDOWN */


#define LEFT_ARROW_KEY_CODE "\033OD"
#define RIGHT_ARROW_KEY_CODE "\033OC"
#define UP_ARROW_KEY_CODE "\033OA"
#define DOWN_ARROW_KEY_CODE "\033OB"

#define SHIFT_LEFT_ARROW_KEY_CODE "\033O2D"
#define SHIFT_RIGHT_ARROW_KEY_CODE "\033O2C"
#define SHIFT_UP_ARROW_KEY_CODE "\033O2A"
#define SHIFT_DOWN_ARROW_KEY_CODE "\033O2B"

#define CTRL_LEFT_ARROW_KEY_CODE "\033O5D"
#define CTRL_RIGHT_ARROW_KEY_CODE "\033O5C"
#define CTRL_UP_ARROW_KEY_CODE "\033O5A"
#define CTRL_DOWN_ARROW_KEY_CODE "\033O5B"

#define DELETE_KEY_CODE		"\033[3~"
#define BACKSPACE_KEY_CODE	"\177"

#define HOME_KEY_CODE "\033OH"
#define INSERT_KEY_CODE "\033[2~"
#define END_KEY_CODE "\033OF"
#define PAGE_UP_KEY_CODE "\033[5~"
#define PAGE_DOWN_KEY_CODE "\033[6~"

#define SHIFT_HOME_KEY_CODE "\033O2H"
#define SHIFT_END_KEY_CODE "\033O2F"

#define BEGIN_BRACKETED_PASTE_CODE "\033[200~"
#define END_BRACKETED_PASTE_CODE "\033[201~"

//#define IS_DOWN_KEY(x) (info.key_states[(x) / 8] & key_state_table[(x) % 8])
#define IS_DOWN_KEY(x) \
(info.key_states[(x) >> 3] & (1 << (7 - ((x) % 8))))
