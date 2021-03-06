/*
Copyright (C) 2015 - 2015 Evan Teran
                          evan.teran@gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "PlatformProcess.h"
#include "PlatformRegion.h"
#include "PlatformThread.h"
#include "edb.h"
#include <processthreadsapi.h>

namespace DebuggerCorePlugin {
namespace {
typedef struct _LSA_UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR  Buffer;
} LSA_UNICODE_STRING, *PLSA_UNICODE_STRING, UNICODE_STRING, *PUNICODE_STRING;

typedef struct _PEB_LDR_DATA {
	BYTE       Reserved1[8];
	PVOID      Reserved2[3];
	LIST_ENTRY InMemoryOrderModuleList;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _RTL_USER_PROCESS_PARAMETERS {
	BYTE           Reserved1[16];
	PVOID          Reserved2[10];
	UNICODE_STRING ImagePathName;
	UNICODE_STRING CommandLine;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

typedef VOID (NTAPI *PPS_POST_PROCESS_INIT_ROUTINE)(VOID);

#ifdef Q_OS_WIN64
typedef struct _PEB {
	BYTE Reserved1[2];
	BYTE BeingDebugged;
	BYTE Reserved2[21];
	PPEB_LDR_DATA LoaderData;
	PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
	BYTE Reserved3[520];
	PPS_POST_PROCESS_INIT_ROUTINE PostProcessInitRoutine;
	BYTE Reserved4[136];
	ULONG SessionId;
} PEB, *PPEB;
#else
typedef struct _PEB {
	BYTE                          Reserved1[2];
	BYTE                          BeingDebugged;
	BYTE                          Reserved2[1];
	PVOID                         Reserved3[2];
	PPEB_LDR_DATA                 Ldr;
	PRTL_USER_PROCESS_PARAMETERS  ProcessParameters;
	BYTE                          Reserved4[104];
	PVOID                         Reserved5[52];
	PPS_POST_PROCESS_INIT_ROUTINE PostProcessInitRoutine;
	BYTE                          Reserved6[128];
	PVOID                         Reserved7[1];
	ULONG                         SessionId;
} PEB, *PPEB;
#endif

typedef struct _PROCESS_BASIC_INFORMATION {
	PVOID Reserved1;
	PPEB PebBaseAddress;
	PVOID Reserved2[2];
	ULONG_PTR UniqueProcessId;
	PVOID Reserved3;
} PROCESS_BASIC_INFORMATION;

typedef enum _PROCESSINFOCLASS {
	ProcessBasicInformation = 0,
	ProcessDebugPort        = 7,
	ProcessWow64Information = 26,
	ProcessImageFileName    = 27
} PROCESSINFOCLASS;

bool getProcessEntry(edb::pid_t pid, PROCESSENTRY32 *entry) {
	bool ret = false;
	if(HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)) {

		PROCESSENTRY32 peInfo;
		peInfo.dwSize = sizeof(peInfo); // this line is REQUIRED

		if(Process32First(hSnapshot, &peInfo)) {
			do {
				if(peInfo.th32ProcessID == pid) {
					*entry = peInfo;
					ret    = true;
					break;
				}
			} while(Process32Next(hSnapshot, &peInfo));
		}

		CloseHandle(hSnapshot);
	}

	return ret;
}

}


/**
 * @brief PlatformProcess::PlatformProcess
 * @param core
 * @param pid
 */
PlatformProcess::PlatformProcess(DebuggerCore *core, edb::pid_t pid) : core_(core) {
	pid_    = pid;
	handle_ = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid_);
	getProcessEntry(pid, &processEntry_);

	if (handle_) {
		HANDLE hToken;
		if (OpenProcessToken(handle_, TOKEN_QUERY, &hToken)) {

			DWORD needed;
			GetTokenInformation(hToken, TokenOwner, nullptr, 0, &needed);

			if (auto owner = static_cast<TOKEN_OWNER *>(std::malloc(needed))) {
				if (GetTokenInformation(hToken, TokenOwner, owner, needed, &needed)) {
					WCHAR user[MAX_PATH];
					WCHAR domain[MAX_PATH];
					DWORD user_sz = MAX_PATH;
					DWORD domain_sz = MAX_PATH;
					SID_NAME_USE snu;

					if (LookupAccountSid(nullptr, owner->Owner, user, &user_sz, domain, &domain_sz, &snu) && snu == SidTypeUser) {
						user_ = QString::fromWCharArray(user);
					}
				}
				std::free(owner);
			}

			CloseHandle(hToken);
		}
	}
}

/**
 * @brief PlatformProcess::PlatformProcess
 * @param core
 * @param pe
 */
PlatformProcess::PlatformProcess(DebuggerCore *core, const PROCESSENTRY32 &pe) : PlatformProcess(core, pe.th32ProcessID) {
	pid_    = pe.th32ProcessID;
	handle_ = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid_);

	if (handle_) {
		HANDLE hToken;
		if (OpenProcessToken(handle_, TOKEN_QUERY, &hToken)) {

			DWORD needed;
			GetTokenInformation(hToken, TokenOwner, nullptr, 0, &needed);

			if (auto owner = static_cast<TOKEN_OWNER *>(std::malloc(needed))) {
				if (GetTokenInformation(hToken, TokenOwner, owner, needed, &needed)) {
					WCHAR user[MAX_PATH];
					WCHAR domain[MAX_PATH];
					DWORD user_sz = MAX_PATH;
					DWORD domain_sz = MAX_PATH;
					SID_NAME_USE snu;

					if (LookupAccountSid(nullptr, owner->Owner, user, &user_sz, domain, &domain_sz, &snu) && snu == SidTypeUser) {
						user_ = QString::fromWCharArray(user);
					}
				}
				std::free(owner);
			}

			CloseHandle(hToken);
		}
	}
}

/**
 * @brief PlatformProcess::PlatformProcess
 * @param core
 * @param handle
 */
PlatformProcess::PlatformProcess(DebuggerCore *core, HANDLE handle) : core_(core), handle_(handle) {

	pid_ = GetProcessId(handle_);
	getProcessEntry(pid_, &processEntry_);
}

/**
 * @brief PlatformProcess::~PlatformProcess
 */
PlatformProcess::~PlatformProcess()  {
	if(handle_) {
		CloseHandle(handle_);
	}
}

/**
 * @brief PlatformProcess::isWow64
 * @return
 */
bool PlatformProcess::isWow64() const {
	BOOL wow64 = FALSE;
	using LPFN_ISWOW64PROCESS = BOOL(WINAPI *) (HANDLE, PBOOL);
	auto fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process");
	if (fnIsWow64Process && fnIsWow64Process(handle_, &wow64) && wow64) {
		return true;
	}
	return false;
}

/**
 * @brief PlatformProcess::start_time
 * @return
 */
QDateTime PlatformProcess::start_time() const {
	Q_ASSERT(handle_);

	FILETIME create;
	FILETIME exit;
	FILETIME kernel;
	FILETIME user;

	if(GetProcessTimes(handle_, &create, &exit, &kernel, &user)) {

		ULARGE_INTEGER createTime;
		createTime.LowPart  = create.dwLowDateTime;
		createTime.HighPart = create.dwHighDateTime;
		return QDateTime::fromMSecsSinceEpoch(createTime.QuadPart / 10000);
	}

	return QDateTime();
}

/**
 * @brief PlatformProcess::pid
 * @return
 */
edb::pid_t PlatformProcess::pid() const {
	return pid_;
}

/**
 * @brief PlatformProcess::name
 * @return
 */
QString PlatformProcess::name() const {

	QString name = QString::fromWCharArray(processEntry_.szExeFile);
	if(isWow64()) {
		name += " *32";
	}
	return name;

}

/**
 * @brief PlatformProcess::user
 * @return
 */
QString PlatformProcess::user() const {
	return user_;
}

/**
 * @brief PlatformProcess::parent
 * @return
 */
std::shared_ptr<IProcess> PlatformProcess::parent() const  {
	edb::pid_t parent_pid = core_->parent_pid(pid_);
	return std::make_shared<PlatformProcess>(core_, parent_pid);
}

/**
 * @brief PlatformProcess::uid
 * @return
 */
edb::uid_t PlatformProcess::uid() const {
	Q_ASSERT(handle_);

	HANDLE token;
	TOKEN_USER user;
	DWORD length;

	// TODO(eteran): is this on the right track?
	if(OpenProcessToken(handle_, 0, &token)) {
		if(GetTokenInformation(token, TokenUser, &user, sizeof(user), &length)) {
		}
	}

	return 0;
}

/**
 * @brief PlatformProcess::patches
 * @return
 */
QMap<edb::address_t, Patch> PlatformProcess::patches() const {
	return patches_;
}

/**
 * @brief PlatformProcess::write_bytes
 * @param address
 * @param buf
 * @param len
 * @return
 */
std::size_t PlatformProcess::write_bytes(edb::address_t address, const void *buf, size_t len) {
	Q_ASSERT(buf);

	if(handle_) {
		if(len == 0) {
			return 0;
		}

		SIZE_T bytes_written = 0;
		if(WriteProcessMemory(handle_, reinterpret_cast<LPVOID>(address.toUint()), buf, len, &bytes_written)) {
			return bytes_written;
		}
	}
	return 0;
}

/**
 * @brief PlatformProcess::read_bytes
 * @param address
 * @param buf
 * @param len
 * @return
 */
std::size_t PlatformProcess::read_bytes(edb::address_t address, void *buf, size_t len) const {
	Q_ASSERT(buf);

	if(handle_) {
		if(len == 0) {
			return 0;
		}

		memset(buf, 0xff, len);
		SIZE_T bytes_read = 0;
		if(ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(address.toUint()), buf, len, &bytes_read)) {
			// TODO(eteran): implement breakpoint stuff
#if 0
			for(const std::shared_ptr<IBreakpoint> &bp: breakpoints_) {

				if(bp->address() >= address && bp->address() < address + bytes_read) {
					reinterpret_cast<quint8 *>(buf)[bp->address() - address] = bp->original_bytes()[0];
				}
			}
#endif
			return bytes_read;
		}
	}
	return 0;
}

/**
 * @brief PlatformProcess::read_pages
 * @param address
 * @param buf
 * @param count
 * @return
 */
std::size_t PlatformProcess::read_pages(edb::address_t address, void *buf, size_t count) const {
	Q_ASSERT(address % core_->page_size() == 0);
	return read_bytes(address, buf, core_->page_size() * count);
}

/**
 * @brief PlatformProcess::pause
 * @return
 */
Status PlatformProcess::pause()  {
	Q_ASSERT(handle_);
	if(DebugBreakProcess(handle_)) {
		return Status::Ok;
	}

	// TODO(eteran): use GetLastError/FormatMessage
	return Status("Failed to pause");
}

/**
 * @brief PlatformProcess::regions
 * @return
 */
QList<std::shared_ptr<IRegion>> PlatformProcess::regions() const {
	QList<std::shared_ptr<IRegion>> regions;

	if(handle_) {
		    edb::address_t addr = 0;
			auto last_base    = reinterpret_cast<LPVOID>(-1);

			Q_FOREVER {
				MEMORY_BASIC_INFORMATION info;
				VirtualQueryEx(handle_, reinterpret_cast<LPVOID>(addr.toUint()), &info, sizeof(info));

				if(last_base == info.BaseAddress) {
					break;
				}

				last_base = info.BaseAddress;

				if(info.State == MEM_COMMIT) {

					const auto start   = edb::address_t::fromZeroExtended(info.BaseAddress);
					const auto end     = edb::address_t::fromZeroExtended(info.BaseAddress) + info.RegionSize;
					const auto base    = edb::address_t::fromZeroExtended(info.AllocationBase);
					const QString name = QString();
					const IRegion::permissions_t permissions = info.Protect; // let std::shared_ptr<IRegion> handle permissions and modifiers

					if(info.Type == MEM_IMAGE) {
						// set region.name to the module name
					}
					// get stack addresses, PEB, TEB, etc. and set name accordingly

					regions.push_back(std::make_shared<PlatformRegion>(start, end, base, name, permissions));
				}

				addr += info.RegionSize;
			}
	}

	return regions;
}

/**
 * @brief PlatformProcess::executable
 * @return
 */
QString PlatformProcess::executable() const  {
	Q_ASSERT(handle_);

	// These functions don't work immediately after CreateProcess but only
	// after basic initialization, usually after the system breakpoint
	// The same applies to psapi/toolhelp, maybe using NtQueryXxxxxx is the way to go

	using QueryFullProcessImageNameWPtr = BOOL (WINAPI *)(
	    HANDLE hProcess,
	    DWORD dwFlags,
	    LPWSTR lpExeName,
	    PDWORD lpdwSize
	);

	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	auto QueryFullProcessImageNameWFunc = (QueryFullProcessImageNameWPtr)GetProcAddress(kernel32, "QueryFullProcessImageNameW");

	wchar_t name[MAX_PATH] = L"";

	if(QueryFullProcessImageNameWFunc/* && LOBYTE(GetVersion()) >= 6*/) { // Vista and up

		DWORD size = _countof(name);
		if(QueryFullProcessImageNameWFunc(handle_, 0, name, &size)) {
			return  QString::fromWCharArray(name);
		}
	}

	return {};
}

/**
 * @brief PlatformProcess::arguments
 * @return
 */
QList<QByteArray> PlatformProcess::arguments() const  {

	Q_ASSERT(handle_);

	QList<QByteArray> ret;
	if(handle_) {

		using ZwQueryInformationProcessPtr = NTSTATUS (*WINAPI)(
		  HANDLE ProcessHandle,
		  PROCESSINFOCLASS ProcessInformationClass,
		  PVOID ProcessInformation,
		  ULONG ProcessInformationLength,
		  PULONG ReturnLength
		);

		HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		auto ZwQueryInformationProcessFunc = (ZwQueryInformationProcessPtr)GetProcAddress(ntdll, "NtQueryInformationProcess");
		PROCESS_BASIC_INFORMATION ProcessInfo;

		if(ZwQueryInformationProcessFunc) {
			    ULONG l;
				NTSTATUS r = ZwQueryInformationProcessFunc(handle_, ProcessBasicInformation, &ProcessInfo, sizeof(PROCESS_BASIC_INFORMATION), &l);
		}
	}
	return ret;
}

/**
 * @brief loaded_modules
 * @return
 */
QList<Module> PlatformProcess::loaded_modules() const {
	QList<Module> ret;
	HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid_);
	if(hModuleSnap != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 me32;
		me32.dwSize = sizeof(me32);

		if(Module32First(hModuleSnap, &me32)) {
			do {
				Module module;
				module.base_address = edb::address_t::fromZeroExtended(me32.modBaseAddr);
				module.name         = QString::fromWCharArray(me32.szModule);
				ret.push_back(module);
			} while(Module32Next(hModuleSnap, &me32));
		}
	}
	CloseHandle(hModuleSnap);
	return ret;
}

/**
 * @brief PlatformProcess::threads
 * @return
 */
QList<std::shared_ptr<IThread>> PlatformProcess::threads() const {

	Q_ASSERT(core_->process_.get() == this);

	QList<std::shared_ptr<IThread>> threadList;

	for(auto &thread : core_->threads_) {
		threadList.push_back(thread);
	}

	return threadList;
}

/**
 * @brief PlatformProcess::current_thread
 * @return
 */
std::shared_ptr<IThread> PlatformProcess::current_thread() const {

	Q_ASSERT(core_->process_.get() == this);

	auto it = core_->threads_.find(core_->active_thread_);
	if(it != core_->threads_.end()) {
		return it.value();
	}
	return nullptr;
}

void PlatformProcess::set_current_thread(IThread& thread) {
	core_->active_thread_ = static_cast<PlatformThread*>(&thread)->tid();
	edb::v1::update_ui();
}

Status PlatformProcess::step(edb::EVENT_STATUS status) {
	// TODO: assert that we are paused
	Q_ASSERT(core_->process_.get() == this);

	if(status != edb::DEBUG_STOP) {
		if(std::shared_ptr<IThread> thread = current_thread()) {
			return thread->step(status);
		}
	}
	return Status::Ok;
}

bool PlatformProcess::isPaused() const {
	for(auto &thread : threads()) {
		if(!thread->isPaused()) {
			return false;
		}
	}

	return true;
}

}
