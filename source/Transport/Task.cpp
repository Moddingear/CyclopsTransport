#include "Transport/Task.hpp"
#include <cassert>
#include <iostream>

Task::Task()
{
}

Task::~Task()
{
	killed = true;
	if (ThreadHandle)
	{
		ThreadHandle->join();
		ThreadHandle.reset();
	}
}

void Task::Start()
{
	assert(!killed);
    assert(!ThreadHandle);
    ThreadHandle = std::make_unique<std::thread>(&Task::ThreadEntryPoint, this);
}

void Task::ThreadEntryPoint()
{
	std::cerr << "Warning : Base Task ThreadEntryPoint running" << std::endl;
    killed = true;
}