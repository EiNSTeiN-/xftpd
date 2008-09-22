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
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "main.h"
#include "config.h"
#include "socket.h"
#include "logging.h"
#include "crypto.h"
#include "ftpd.h"
#include "vfs.h"
#include "slaves.h"
#include "events.h"
#include "luainit.h"
#include "scripts.h"
#include "irccore.h"
#include "users.h"
#include "timeout.h"
#include "mirror.h"
#include "site.h"
#include "time.h"
#include "timeout.h"
#include "signal.h"
#include "stats.h"
#include "site.h"
#include "nuke.h"
#include "service.h"
#include "slaveselection.h"
#include "secure.h"


unsigned long long int init_time = 0;
unsigned long long int startup_time = 0;
unsigned long long int main_cycle_time = 0;

struct main_ctx main_ctx;

void main_fatal() {

	MAIN_DBG("Fatal error at top level!");
	main_ctx.living = 0;

	return;
}

void main_reload() {

	MAIN_DBG("FTP is reloading ...");
	main_ctx.reload = 1;

	return;
}

void set_current_path() {
	char full_path[MAX_PATH];
	char *ptr;

	GetModuleFileName(NULL, full_path, sizeof(full_path));
	ptr = strchr(full_path, '\\');
	if(ptr) {
		while(strchr(ptr+1, '\\')) ptr = strchr(ptr+1, '\\');
		*(ptr+1) = 0;
	}
	_chdir(full_path);

	return;
}

#ifdef MASTER_WIN32_SERVICE
int win32_service_main() {
#else
int main(int argc, char* argv[]) {
#endif
	unsigned long long int time;

	MAIN_DBG("Loading ...");

#ifdef FTPD_STOP_WORKING_TIMESTAMP
	//MAIN_DBG("Now: %I64u. Stop: %I64u.", (time_now() / 1000), FTPD_STOP_WORKING_TIMESTAMP);
	if((time_now() / 1000) > FTPD_STOP_WORKING_TIMESTAMP) {
		MAIN_DBG("This version of xFTPd is no longer up to date.");
		MAIN_DBG("Please visit www.xftpd.com to get a new version.");
	}
#endif

	set_current_path();

#ifdef MASTER_SILENT_CRASH
	SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#endif

	main_ctx.living = 1;
	main_ctx.reload = 0;

	init_time = time_now();

	if(!socket_init()) {
		MAIN_DBG("Could not initialize \"socket\" module");
		return 1;
	}
	if(!crypto_init()) {
		MAIN_DBG("Could not initialize \"crypt\" module");
		return 1;
	}
	if(!secure_init()) {
		MAIN_DBG("Could not initialize \"secure\" module");
		return 1;
	}
	if(!slaves_init()) {
		MAIN_DBG("Could not initialize \"slaves\" module");
		return 1;
	}
	if(!vfs_init()) {
		MAIN_DBG("Could not initialize \"vfs\" module");
		return 1;
	}
	if(!users_init()) {
		MAIN_DBG("Could not initialize \"users\" module");
		return 1;
	}
	if(!ftpd_init()) {
		MAIN_DBG("Could not initialize \"ftpd\" module");
		return 1;
	}
	if(!events_init()) {
		MAIN_DBG("Could not initialize \"event\" module");
		return 1;
	}
	if(!slaveselection_init()) {
		MAIN_DBG("Could not initialize \"event\" module");
		return 1;
	}
	if(!timeout_init()) {
		MAIN_DBG("Could not initialize \"timeout\" module");
		return 1;
	}
	if(!mirror_init()) {
		MAIN_DBG("Could not initialize \"mirror\" module");
		return 1;
	}
	if(!site_init()) {
		MAIN_DBG("Could not initialize \"site\" module");
		return 1;
	}
	if(!nuke_init()) {
		MAIN_DBG("Could not initialize \"nuke\" module");
		return 1;
	}

	if(!luainit_init()) {
		MAIN_DBG("Could not initialize \"lua\" module");
		return 1;
	}
	if(!irccore_init()) {
		MAIN_DBG("Could not initialize \"irccore\" module");
		return 1;
	}
	if(!scripts_init()) {
		MAIN_DBG("Could not initialize \"scripts\" module");
		return 1;
	}

	init_time = timer(init_time);
	startup_time = time_now();
	
	MAIN_DBG("Initialization completed in %I64u miliseconds", init_time);


	/*
		Core loop of the ftpd.
		This is where the magic happens.
	*/
	do {
		
		time = time_now();
		
		probe_stats();
		
		timeout_poll();
		
		socket_poll();
		
		secure_poll();
		
		slaves_dump_fileslog();
		
		config_poll();
		
		collection_cleanup_iterators();
		
		luainit_garbagecollect();
		
		Sleep(MASTER_SLEEP_TIME);
		
		if(obj_balance) {
			MAIN_DBG("WARNING!!! Object dereferencing is not balanced!");
		}
		
		if(main_ctx.reload) {
			
			MAIN_DBG("Reloading ...");
			event_onPreReload();
			
			if(!ftpd_reload()) {
				MAIN_DBG("Could not reload \"ftpd\" module");
				return 0;
			}
			
			/* clean all irc hooks */
			if(!irccore_reload()) {
				MAIN_DBG("Could not reload \"irccore\" module");
				return 0;
			}
			
			/* clean all site hooks */
			if(!site_reload()) {
				MAIN_DBG("Could not reload \"site\" module");
				return 0;
			}
			
			/* clean all timeouts */
			timeout_clear();
			
			/* clean all events */
			events_clear();
			
			/* make a brand new lua context. */
			if(!luainit_reload()) {
				MAIN_DBG("Could not reload \"lua\" module");
				return 0;
			}
			
			/* reload all scripts */
			scripts_loadall();
			
			nuke_reload();
			
			/* fire the onReload event */
			event_onReload();
			MAIN_DBG("Reload is complete.");
			
			main_ctx.reload = 0;
		}
		
		main_cycle_time = timer(time);
		
	} while(main_ctx.living);
	
	MAIN_DBG("Main loop exited. Runtime: %I64u miliseconds", timer(startup_time));
	
	scripts_free();
	irccore_free();
	luainit_free();
	
	nuke_free();
	site_free();
	mirror_free();
	timeout_free();
	slaveselection_free();
	events_free();
	ftpd_free();
	users_free();
	vfs_free();
	slaves_free();
	secure_free();
	crypto_free();
	socket_free();
	
	return 0;
}

#ifdef MASTER_WIN32_SERVICE
int main(int argc, char* argv[]) {
	char *name;
	char *displayname;
	char *desc;

	set_current_path();

	name = config_raw_read(MASTER_CONFIG_FILE, "master.service.name", "xFTPd-master");
	displayname = config_raw_read(MASTER_CONFIG_FILE, "master.service.displayname", "xFTPd-master");
	desc = config_raw_read(MASTER_CONFIG_FILE, "master.service.description", "xFTPd-master");

	// if we're a service, it means someone *else* will get the result
	if(service_start_and_call(name, displayname, desc, (func)&win32_service_main)) {
		return 1;
	}

	return 0;
}
#endif

