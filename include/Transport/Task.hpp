#pragma once

#include <memory>
#include <thread>

class Task
{
protected:
	bool killed = false;
	std::unique_ptr<std::thread> ThreadHandle = nullptr;
public:
	Task();
	virtual ~Task();

	void Start();

	bool IsKilled()
	{
		return killed;
	}

	void Kill()
	{
		killed = true;
	}

protected:
	virtual void ThreadEntryPoint();
};