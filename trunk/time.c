/*
 * Copyright (c) 2007, The xFTPd Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 *     * Neither the name of the xFTPd Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *     * Redistributions of this project or parts of this project in any form
 *       must retain the following aknowledgment:
 *       "This product includes software developed by the xFTPd Project.
 *        http://www.xftpd.com/ - http://www.xftpd.org/"
 *
 * THIS SOFTWARE IS PROVIDED BY THE xFTPd PROJECT ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE xFTPd PROJECT BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <windows.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <time.h>

#include "time.h"

/* return the current time in milliseconds */
unsigned long long int time_now() {
	struct __timeb64 timeptr;

	_ftime64(&timeptr);

	/* convert in milliseconds */
	return (timeptr.time * 1000) + timeptr.millitm;
}

/* return the number of milliseconds elapsed since 'start' */
unsigned long long int timer(unsigned long long int start) {
	return (time_now() - start);
}

/* time is represented in milliseconds.
	you should time_now() as input for this parameter */
void time_stamp_to_formated(unsigned long long int time, unsigned short *day,
					 unsigned short *month, unsigned short *year,
					unsigned short *hour, unsigned short *minute) {
	__time64_t t;
	struct tm *lt;

	t = (time / 1000);

	lt = _gmtime64(&t);
	if(!lt) return;

	if(day) *day = lt->tm_mday;
	if(month) *month = lt->tm_mon+1;
	if(year) *year = lt->tm_year+1900;
	if(hour) *hour = lt->tm_hour;
	if(minute) *minute = lt->tm_min;

	return;
}

#define MILLISECONDS_IN_ONE_YEAR (1000ULL * 60 * 60 * 24 * 365)

unsigned int is_this_year(unsigned long long int timestamp) {
	return (timer(timestamp) < MILLISECONDS_IN_ONE_YEAR);
}
