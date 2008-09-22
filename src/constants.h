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

#ifndef __CONSTANTS_H
#define __CONSTANTS_H

/**********************************************/
/**********************************************/
/*********** compilation options */

// also check debug.h for debug options

/*
	Uncomment the following line to disable tolua error checking
	It save some space on executable size and may make a code
	that will run a bit faster, but it remove many very useful 
	sanity checks.
*/
//#define TOLUA_RELEASE

#ifdef WIN32
/* Uncomment these lines to compile each modules as services */
//#define MASTER_WIN32_SERVICE
#define SLAVE_WIN32_SERVICE
#define PROXY_WIN32_SERVICE

/* Uncomment these lines to disable the windows' crash message box */
//#define MASTER_SILENT_CRASH
#define SLAVE_SILENT_CRASH
#define PROXY_SILENT_CRASH
#endif

/*
	Defines sleep time in ms for each executables.
	This is the time the executable will spend in the sleep()
	function at each cycle.
*/
#define MASTER_SLEEP_TIME		10
#define SLAVE_SLEEP_TIME		50
#define PROXY_SLEEP_TIME		10

/* compile with irc client activated */
#define MASTER_WITH_IRC_CLIENT

/* Defines whether to grow the file sizes as they are being transfered or not */
#define GROW_FILE_SIZES_ON_TRANSFER

/* The official version number */
#define VERSION_NUMBER	"0.4"

#define SLAVE_PLATFORM_CURRENT			SLAVE_PLATFORM_WIN32

/* The slave's revision number */
#define SLAVE_UPDATE_FILENAME_PATTERN	"xftpd_update%I64u.exe"

/*
	Revision log:
		>= 14	ssl certificate is sent when a slave connect
*/
#define SLAVE_REVISION_NUMBER			19LLU // 0.4 = 19

/**********************************************/
/**********************************************/
/*********** default values & config */

#define MASTER_DEFAULT_CERTIFICATE_FILENAME		"xFTPd.pem"
#define MASTER_DEFAULT_PRIVATE_KEY_FILENAME		"xFTPd.key"

/* */
#define MASTER_CONFIG_FILE		"xFTPd.conf"
#define SLAVE_CONFIG_FILE		"slave.conf"
#define PROXY_CONFIG_FILE		"proxy.conf"

#define SCRIPTS_PATH			"./scripts"
#define SKINS_PATH				"./skins"

#define SECTIONS_FOLDER			"./sections"
#define SLAVES_FOLDER			"./slaves"
#define USERS_FOLDER			"./users"

/*
	maximum time elapsed between each time
	the config files are written to disk
*/
//#define SECTIONS_CONFIG_TIMEOUT	(25 * 1000) /* 25 seconds */
//#define SLAVES_CONFIG_TIMEOUT	(25 * 1000) /* 25 seconds */
//#define USERS_CONFIG_TIMEOUT	(25 * 1000) /* 25 seconds */

#define CONFIG_SAVETIME		(20 * 1000) /* 20 seconds */

#define NUKELOG_FILE			"xftpd.nukelog"

#define SLAVE_UP_BUFFER_SIZE		(1024 * 1024)
#define SLAVE_DN_BUFFER_SIZE		(65535)

#define SLAVE_DELETE_INCOMPLETE_UPLOADS		1

/* timeout for master/slave data arrival */
#define SLAVE_MASTER_TIMEOUT		(60 * 1000) /* 60 seconds */

/* this is the rate at wich the stats will be probed.
	it's also used as a keepalive for the master */
#define FTPD_STATS_PROBE_TIME		(5 * 1000) /* 5 seconds (in milliseconds) */

/* size of the socket buffer between the master and slave */
#define SLAVE_MASTER_SOCKET_SIZE	(256 * 1024) /* 256 kb */

/* timeout for data connection */
#define DATA_CONNECTION_TIMEOUT		(10 * 1000) /* 10 seconds */

/* timeout for irc connection */
#define IRC_DATA_TIMEOUT		(30 * 60 * 1000) /* 20 minutes */

/* irc socket buffer size */
#define IRC_SOCKET_SIZE			(16 * 1024) /* 16 kb */

/* timeout for clients connection */
#define FTPD_CLIENT_TIMEOUT		(5 * 60 * 1000) /* 5 minutes */

/*
	time after wich a transfer fails when no
	stats has been received from the slave on
	the master side.
*/
#define FTPD_XFER_TIMEOUT		(60 * 1000) /* 60 seconds */

/* ftpd clients buffer size */
#define FTPD_CLIENT_SOCKET_SIZE		(16 * 1024) /* 16 kb */

/* socket size for the directory listing */
#define FTPD_CLIENT_DATA_SOCKET_SIZE	(256 * 1024) /* 256 kb */

/* maximum time the master will wait for an asynch response to be received */
#define MASTER_ASYNCH_TIMEOUT		(60 * 1000) /* 1 minute */

/* slave's speed correction */
#define SPEEDCHECK_TIME				(10 * 1000) /* 5 seconds */
#define SPEEDCHECK_THRESHOLD		(5) /* 5% change */
#define SPEEDCHECK_MINIMAL			(5 * 1024) /* 5 kb/s */

/*
	Maximum time the proxy will wait for an
	incoming connection to be established.
*/
#define PROXY_LISTEN_TIMEOUT		(5 * 60 * 1000) /* 5 mins. */

/* ftpd config */
//#define IO_SOCKET_TIMEOUT		5000
#define COMMUNICATION_KEY_LEN		1024
#define CHALLENGE_LEN			128

#define SLAVES_USE_ENCRYPTION		1
#define SLAVES_USE_COMPRESSION		1
#define SLAVES_COMPRESSION_THRESHOLD	500


#define SLAVES_PORT			20
#define FTPD_PORT			21
#define FTPD_BUFFER_SIZE		65535

#define FTPD_LOW_DATA_PORT		40000
#define FTPD_HIGH_DATA_PORT		49000

#define FTPD_ASYNCH_TIMEOUT		(30 * 1000) /* 30 sec (in milliseconds) */

/* time between each fileslog saving */
#define FTPD_FILESLOG_TIME		(5 * 60 * 1000) /* 5 minutes */

/*
	The fileslog saving will be dropped if it goes above this value
	It should also be less than any timeout*/
#define FTPD_FILESLOG_THRESHOLD	(2 * 1000) /* 2 seconds */

#define EVENTS_REFTABLE			"events_reftable"
#define SLAVESELECTION_REFTABLE	"slaveselection_reftable"
#define TIMER_REFTABLE			"timer_reftable"
#define MIRRORS_REFTABLE		"mirrors_reftable"
#define SITE_REFTABLE			"site_reftable"
#define IRCCORE_REFTABLE		"irccore_reftable"
#define SCRIPTS_REFTABLE		"scripts_reftable"

#define FUNC_INLINE __inline__

#endif /* __CONSTANTS_H */
