﻿#pragma once

#include "IConcurrentPool.h"

namespace S4Framework
{
	class ConcurrentJobManager : public IConcurrentPool
	{
	public:
		explicit ConcurrentJobManager( std::size_t size = MAX_LOGIC_THREAD );
		~ConcurrentJobManager();
	};
}