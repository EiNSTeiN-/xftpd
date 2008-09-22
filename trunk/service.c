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

#include "service.h"

SERVICE_STATUS_HANDLE	service_status_handle;
SERVICE_STATUS			service_status;
func					service_function_to_start;

unsigned int service_is_registered(char *name) {
	SC_HANDLE scm_handle, service_handle;

	scm_handle = OpenSCManager(NULL,NULL,SC_MANAGER_ALL_ACCESS);
	if(!scm_handle) return 0;

	service_handle=OpenService(scm_handle, name, SERVICE_ALL_ACCESS);
	if (service_handle) CloseServiceHandle(service_handle);

	CloseServiceHandle(scm_handle);

	return (service_handle != 0);
}


void __stdcall service_handler(unsigned long control_code) {

	switch (control_code) {
	case (SERVICE_CONTROL_SHUTDOWN||SERVICE_CONTROL_STOP):
		service_status.dwWin32ExitCode=NO_ERROR;
		service_status.dwCheckPoint=0;
		service_status.dwWaitHint=0;
		service_status.dwCurrentState=SERVICE_STOPPED;
	}
	SetServiceStatus(service_status_handle,&service_status);

	return;
}

void service_uninstall(char *name) {
	SC_HANDLE scm_handle, service_handle;
	SERVICE_STATUS service_status;
	SC_LOCK service_lock;

	scm_handle = OpenSCManager(NULL,NULL,SC_MANAGER_ALL_ACCESS);
	if (scm_handle!=0) {
		service_lock = LockServiceDatabase(scm_handle);
		service_handle = OpenService(scm_handle, name, SERVICE_ALL_ACCESS);
		if (service_handle) {
			QueryServiceStatus(service_handle,&service_status);
			if(service_status.dwCurrentState == SERVICE_RUNNING)
				ControlService(service_handle, SERVICE_CONTROL_STOP, &service_status);
			DeleteService(service_handle);
			CloseServiceHandle(service_handle);
		}
		CloseServiceHandle(scm_handle);
		UnlockServiceDatabase(service_lock);
	}

	return;
}


unsigned int MyChangeServiceConfig2(SC_HANDLE service_handle,DWORD info_level,PVOID info) {
	fChangeServiceConfig2A MyChangeServiceConfig2A;
	FARPROC proc_address;
	HMODULE lib_module;
	unsigned int ret = 0;

	lib_module = LoadLibrary("advapi32.dll");
	proc_address = GetProcAddress(lib_module,"MyChangeServiceConfig2A");
	if(proc_address) {
		MyChangeServiceConfig2A = (fChangeServiceConfig2A)proc_address;
		ret = MyChangeServiceConfig2A(service_handle, info_level, info);
	}

	return ret;
}

unsigned int service_install_and_start(char *service_name, char* service_disp, char* service_desc, func service_to_start) {
	SC_HANDLE scm_handle, service_handle;
	char full_path[MAX_PATH];
	unsigned int ret = 0;
	char *arguments[2];

	GetModuleFileName(NULL, full_path, sizeof(full_path)-1);

//#ifdef TESTING_TIME
	/*printf("Full path to this exe: %s\n",full_path);
	printf("service_name: %s\n",service_name);
	printf("service_disp: %s\n",service_disp);
	printf("service_desc: %s\n",service_desc);*/
//#endif

	service_uninstall(service_name);

	scm_handle = OpenSCManager(NULL,NULL,SC_MANAGER_ALL_ACCESS);

	if(!scm_handle) return 0;

	service_handle = CreateService(
		scm_handle,
		service_name,
		service_disp,
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START,
        SERVICE_ERROR_IGNORE,
		full_path,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	);

	service_function_to_start = service_to_start;
	arguments[0] = service_name;

	if(service_handle) {
		MyChangeServiceConfig2(service_handle,1,service_desc);
		ret = StartService(service_handle, 1, (LPCTSTR*)((char **)&arguments));
		CloseServiceHandle(service_handle);
	}
	CloseServiceHandle(scm_handle);

	return ret;
}

void __stdcall service_main(unsigned int argc, char* argv[]) {

	service_status_handle = RegisterServiceCtrlHandler(argv[0],(LPHANDLER_FUNCTION)&service_handler);

	if (service_status_handle) {

		memset(&service_status,0,sizeof(service_status));
		service_status.dwServiceType =				SERVICE_WIN32;
		service_status.dwControlsAccepted =			(SERVICE_ACCEPT_SHUTDOWN|SERVICE_ACCEPT_STOP);
		service_status.dwWin32ExitCode =			NO_ERROR;
		service_status.dwServiceSpecificExitCode =	0;
		service_status.dwCheckPoint =				0;
		service_status.dwWaitHint =					0;
		service_status.dwCurrentState =				SERVICE_RUNNING;
		SetServiceStatus(service_status_handle,&service_status);

		(*service_function_to_start)();

		if(service_status.dwCurrentState==SERVICE_RUNNING) {
			service_status.dwCurrentState=SERVICE_STOPPED;
			SetServiceStatus(service_status_handle,&service_status);
		}
	}

	return;
}

unsigned int service_start_and_call(char *service_name, char* service_disp, char* service_desc, func service_to_start) {
	SERVICE_TABLE_ENTRY DispatchTable[2];

	service_function_to_start = service_to_start;

	if(service_is_registered(service_name)) {
		DispatchTable[0].lpServiceName = service_name;
		DispatchTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)&service_main;
		DispatchTable[1].lpServiceName = NULL;
		DispatchTable[1].lpServiceProc = NULL;
		if (StartServiceCtrlDispatcher(DispatchTable)) return 1;
		else return service_install_and_start(service_name,service_disp,service_desc,service_to_start);
	} else 
		return service_install_and_start(service_name,service_disp,service_desc,service_to_start);

	return 0;
}
