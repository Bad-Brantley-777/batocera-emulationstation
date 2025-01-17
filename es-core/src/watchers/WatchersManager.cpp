#include "WatchersManager.h"
#include "Log.h"
#include <chrono>
#include <SDL.h>

std::mutex											WatchersManager::mNotificationLock;
std::list<IWatcherNotify*>							WatchersManager::mNotification;
std::mutex											WatchersManager::mWatchersLock;
std::list<WatchersManager::WatcherInfo*>			WatchersManager::mWatchers;

WatchersManager* WatchersManager::mInstance = nullptr;

WatchersManager* WatchersManager::getInstance()
{
	if (mInstance == nullptr)
		mInstance = new WatchersManager();

	return mInstance;
}

WatchersManager::WatchersManager()
{
	LOG(LogDebug) << "WatchersManager : Starting";

	mRunning = true;
	mThread = new std::thread(&WatchersManager::run, this);
}

void WatchersManager::ResetComponent(IWatcher* instance)
{
	std::unique_lock<std::mutex> lock(mWatchersLock);

	for (auto comp : mWatchers)
	{
		if (comp->component == instance)
		{
			comp->nextCheckTime = 0;
			return;
		}
	}
}

void WatchersManager::RegisterComponent(IWatcher* instance)
{
	std::unique_lock<std::mutex> lock(mWatchersLock);

	WatcherInfo* info = new WatcherInfo();
	info->component = instance;
	info->nextCheckTime = SDL_GetTicks() + instance->initialUpdateTime();
	mWatchers.push_back(info);
}

void WatchersManager::UnregisterComponent(IWatcher* instance)
{
	std::unique_lock<std::mutex> lock(mWatchersLock);

	for (auto it = mWatchers.cbegin(); it != mWatchers.cend(); it++)
	{
		WatcherInfo* info = *it;
		if (info->component == instance)
		{
			mWatchers.erase(it);
			delete info;
			return;
		}
	}
}

void WatchersManager::RegisterNotify(IWatcherNotify* instance)
{
	std::unique_lock<std::mutex> lock(mNotificationLock);
	mNotification.push_back(instance);
}

void WatchersManager::UnregisterNotify(IWatcherNotify* instance)
{
	std::unique_lock<std::mutex> lock(mNotificationLock);

	for (auto it = mNotification.cbegin(); it != mNotification.cend(); it++)
	{
		if ((*it) == instance)
		{
			mNotification.erase(it);
			return;
		}
	}
}

void WatchersManager::NotifyComponentChanged(IWatcher* component)
{
	std::unique_lock<std::mutex> lock(mNotificationLock);

	for (IWatcherNotify* n : mNotification)
		n->OnWatcherChanged(component);
}

WatchersManager::~WatchersManager()
{
	if (mThread == nullptr)
		return;
	
	LOG(LogDebug) << "WatchersManager : Exit";

	mRunning = false;
	mEvent.notify_all();

	mThread->join();
	delete mThread;
	mThread = nullptr;	

	for (auto comp : mWatchers)
		UnregisterComponent(comp->component);

	for (auto notif : mNotification)
		UnregisterNotify(notif);
}

void WatchersManager::run()
{
	while (mRunning)
	{
		// ThreadLock
		{			
			std::unique_lock<std::mutex> lock(mThreadLock);
			mEvent.wait_for(lock, std::chrono::seconds(1));
			
			if (!mRunning)
				return;
		}

		// Notification Lock
		{
			std::unique_lock<std::mutex> lock(mWatchersLock);

			int ticks = SDL_GetTicks();
			for (auto item : mWatchers)
			{
				if (!mRunning)
					return;

				if (!item->component->enabled())
					continue;

				if (ticks < item->nextCheckTime)
					continue;

				if (item->component->check())
					NotifyComponentChanged(item->component);

				item->nextCheckTime = SDL_GetTicks() + item->component->updateTime();
			}
		}		
	}
}
