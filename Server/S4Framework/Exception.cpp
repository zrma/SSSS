﻿#include "stdafx.h"
#include "ThreadLocal.h"
#include "Exception.h"
#include "Log.h"
#include <fstream>

#pragma warning(disable: 4091)
#include <DbgHelp.h>
#include <TlHelp32.h>
#include <strsafe.h>
#include "StackWalker.h"
#include <minwinbase.h>

constexpr auto MAX_BUFF_SIZE = 1024;

namespace S4Framework
{
	void MakeDump(EXCEPTION_POINTERS* e)
	{
		TCHAR tszFileName[MAX_BUFF_SIZE] = { 0 };
		SYSTEMTIME stTime;
		ZeroMemory(&stTime, sizeof(stTime));
		GetSystemTime(&stTime);
		StringCbPrintf(tszFileName,
			_countof(tszFileName),
			_T("%s_%4d%02d%02d_%02d%02d%02d.dmp"),
			_T("ServerDump"),
			stTime.wYear,
			stTime.wMonth,
			stTime.wDay,
			stTime.wHour,
			stTime.wMinute,
			stTime.wSecond);

		auto hFile = CreateFile(tszFileName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
			return;

		MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
		exceptionInfo.ThreadId = GetCurrentThreadId();
		exceptionInfo.ExceptionPointers = e;
		exceptionInfo.ClientPointers = FALSE;

		MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			hFile,
			MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithFullMemory),
			e ? &exceptionInfo : nullptr,
			nullptr,
			nullptr);

		if (hFile)
		{
			CloseHandle(hFile);
			hFile = nullptr;
		}

	}


	LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
	{
		if (IsDebuggerPresent())
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		THREADENTRY32 te32;
		const auto myThreadId = GetCurrentThreadId();
		const auto myProcessId = GetCurrentProcessId();

		std::vector<HANDLE> hThreadVector;

		const auto hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (hThreadSnap != INVALID_HANDLE_VALUE)
		{
			te32.dwSize = sizeof(THREADENTRY32);

			if (Thread32First(hThreadSnap, &te32))
			{
				do
				{
					/// 내 프로세스 내의 스레드중 나 자신 스레드만 빼고 멈추게..
					if (te32.th32OwnerProcessID == myProcessId && te32.th32ThreadID != myThreadId)
					{
						HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te32.th32ThreadID);
						if (hThread)
						{
							SuspendThread(hThread);
						}

						hThreadVector.push_back(hThread);
					}

				} while (Thread32Next(hThreadSnap, &te32));

			}

			CloseHandle(hThreadSnap);
		}

		std::ofstream historyOut("S4ServerException_Log.txt", std::ofstream::out);

		/// 콜히스토리 남기고
		historyOut << "========== WorkerThread Call History ==========" << std::endl << std::endl;
		auto* history = reinterpret_cast<ThreadCallHistory*>(InterlockedPopEntrySList(&GThreadCallHistory));
		while (history)
		{
			history->DumpOut(historyOut);
			history = reinterpret_cast<ThreadCallHistory*>(InterlockedPopEntrySList(&GThreadCallHistory));
		}
		
		/// 콜성능 남기고
		historyOut << "========== WorkerThread Call Performance ==========" << std::endl << std::endl;
		auto* record = reinterpret_cast<ThreadCallElapsedRecord*>(InterlockedPopEntrySList(&GThreadCallElapsedRecord));
		while (record)
		{
			record->DumpOut(historyOut);
			record = reinterpret_cast<ThreadCallElapsedRecord*>(InterlockedPopEntrySList(&GThreadCallElapsedRecord));
		}
		
		/// 콜스택도 남기고
		historyOut << "========== Exception Call Stack ==========" << std::endl << std::endl;
		auto stackWalker = StackWalker( myProcessId, OpenProcess( PROCESS_ALL_ACCESS, TRUE, myProcessId) );
		stackWalker.LoadModules();
		stackWalker.SetOutputStream(&historyOut);
		stackWalker.ShowCallstack();
		
		for ( const auto& it : hThreadVector )
		{
			historyOut << std::endl << "===== Thread Call Stack [Thread:" << GetThreadId(it) << "]" << std::endl;
			stackWalker.ShowCallstack(it);

		}

		/// 이벤트 로그 남기고
		EventLogDumpOut(historyOut);

		historyOut.flush();
		historyOut.close();

		/// 마지막으로 dump file 남기자.
		MakeDump(exceptionInfo);

		ExitProcess(1);
		/// 여기서 쫑

		return EXCEPTION_EXECUTE_HANDLER;
	}
}