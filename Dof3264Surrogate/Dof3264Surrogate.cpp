// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// 32-bit surrogate local server process for creating the DOF COM object from
// the 64-bit version of PinballY.
// 
// DOF is distributed with a 32-bit COM object, which means that 64-bit PinballY
// can't load it as a normal in-process server, because of the Windows rules
// against mixing 32-bit and 64-bit code in one process.  This surrogate is our
// solution.  64-bit PinballY launches this program at startup, passing us a 
// randomly generated GUID to use as a proxy class ID.  We register a class
// factory with COM using the random GUID, and then just sit and wait.  When
// the parent wants to create a DOF COM object, it calls CoCreateInstance() to
// create an object of the random proxy class.  Since we registered a class
// factory for that GUID, COM marshalls the CoCreateInstance() call across the
// process boundary to our class factory's CreateInstance() method.  Since
// we always run as a 32-bit process, we're able to create the DOF COM object
// the "normal" way, as an in-process server, by calling CoCreateInstance()
// using the DOF COM object's GUID.  We can then pass that object back to the
// caller - the COM marshalling mechanism - which will marshall it back to our
// parent process.  The COM marshaller is able to move the interface object
// across the 32/64 process boundary and deliver it to the 64-bit parent
// process.  Method calls on the interface in the 64-bit parent will likewise
// be marshalled automatically to the 32-bit implementation object that we
// created.
//
#include "stdafx.h"
#include <Windows.h>
#include <regex>
#include "../Utilities/Pointers.h"
#include "../Utilities/StringUtil.h"

static void errexit(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	char buf[1024];
	vsprintf_s(buf, msg, ap);
	va_end(ap);
	MessageBoxA(NULL, buf, "PinballY DOF Surrogate", MB_OK | MB_ICONERROR);
	exit(1);
}

static void usageExit()
{
	errexit("Greetings!  This program is part of PinballY.  It's not meant to be run separately."
		"PinballY will launch it automatically as needed.");
}

// DOF COM object declaration
class __declspec(uuid("{a23bfdbc-9a8a-46c0-8672-60f23d54ffb6}")) DirectOutputComObject;

// Main DOF interface
static IID IID_Dof = { 0x63dc1112, 0x571f, 0x4a49,{ 0xb2, 0xfd, 0xcf, 0x98, 0xc0, 0x2b, 0xf5, 0xd4 } };

// DOF 64-bit COM object proxy
class __declspec(uuid("{D744EE13-4C70-474D-8FB1-8295C350FB07}")) DOFProxy64;

// class factory object
class CFactory : public IClassFactory
{
public:
	CFactory(const CLSID &clsidProxyClass) : clsidProxyClass(clsidProxyClass), refcnt(1) { }

	virtual ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refcnt); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG ret = InterlockedDecrement(&refcnt);
		if (ret == 0) delete this;
		return ret;
	}

	virtual STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
	{
#define QIMATCH(I) \
        if (riid == __uuidof(I)) \
		{ \
			AddRef(); \
			*ppv = static_cast<I*>(this); \
			return S_OK; \
		}
		
		QIMATCH(IUnknown)
		QIMATCH(IClassFactory)
		return E_NOINTERFACE;
	}

	virtual STDMETHODIMP CreateInstance(LPUNKNOWN punkOuter, REFIID iid, void **ppv) override
	{
		// don't allow aggregation
		if (punkOuter != NULL)
			return CLASS_E_NOAGGREGATION;

		// create an instance of the DOF COM object and return it to the caller
		return CoCreateInstance(__uuidof(DirectOutputComObject), NULL, CLSCTX_INPROC_SERVER, IID_Dof, ppv);
	}

	virtual STDMETHODIMP LockServer(BOOL fLock) override
	{
		if (fLock)
			AddRef();
		else
			Release();
		return S_OK;
	}

protected:
	// CLSID for the proxy class.  This is the class ID that we'll register
	// with COM for our class factory.  The parent process can create an
	// instance of this proxy class to invoke our class constructor, which
	// will in turn create an instance of the 32-bit DOF COM object.
	CLSID clsidProxyClass;

	// COM reference count
	ULONG refcnt;
};

int main(int argc, char **argv)
{
	// initialize free-threaded COM
	HRESULT hr = S_OK;
	if (FAILED(hr = CoInitializeEx(NULL, COINIT_MULTITHREADED)))
		errexit("Error initializing COM (HRESULT %lx)", (long)hr);

	// uninitialize on exit
	class ComDeleter
	{
	public:
		~ComDeleter() { CoUninitialize(); }
	};
	ComDeleter comDeleter;

	// get the arguments
	long ppid = 0;
	CLSID clsidProxy = CLSID_NULL;
	for (int i = 1; i < argc; ++i)
	{
		std::match_results<const char*> match;
		if (std::regex_match(argv[i], match, std::regex("-parent_pid=(\\d+)")))
		{
			ppid = atol(match[1].str().c_str());
		}
		else if (std::regex_match(argv[i], match, std::regex("-clsid=(.+)")))
		{
			if (!ParseGuid(AnsiToTSTRING(match[1].str().c_str()).c_str(), clsidProxy))
				usageExit();
		}
		else
			usageExit();
	}

	// make sure we got the required arguments
	if (ppid == 0 || IsEqualCLSID(clsidProxy, CLSID_NULL))
		usageExit();

	// Generate the names of the "ready" and "done" event objects.  These
	// are based on the parent process PID.
	char readyEventName[128], doneEventName[128];
	sprintf_s(readyEventName, "PinballY.Dof6432Surrogate.%lx.Event.Ready", ppid);
	sprintf_s(doneEventName, "PinballY.Dof6432Surrogate.%lx.Event.Done", ppid);

	// Open the event objects.  These are created in the parent process,
	// so even though we're nominally "creating" them, we're really opening
	// existing objects.
	HANDLE hEventReady = CreateEventA(NULL, FALSE, FALSE, readyEventName);
	HANDLE hEventDone = CreateEventA(NULL, FALSE, FALSE, doneEventName);

	// create our class factory
	RefPtr<CFactory> pFactory(new CFactory(clsidProxy));

	// register our class factory
	DWORD dwRegister = 0;
	if (FAILED(hr = CoRegisterClassObject(clsidProxy, pFactory, CLSCTX_SERVER, REGCLS_MULTIPLEUSE, &dwRegister)))
		errexit("Error registering DOF COM Object class factor (HRESULT %lx)", (long)hr);

	// signal that we're ready to begin accepting class construction requests
	SetEvent(hEventReady);

	// wait for the parent process to signal that it's done
	WaitForSingleObject(hEventDone, INFINITE);

	// revoke the class factory registration
	CoRevokeClassObject(dwRegister);

	// Successful completion.  Note that, in general, a local COM server
	// process such as this mustn't exit until all instances of objects 
	// created through its class factory have been destroyed, since the
	// objects are allocated within our memory space and run on our
	// threads.  But it's not straightforward for us to determine if the
	// DOF COM objects we've created have all been destroyed, since we 
	// don't actually implement them: they're implemented in the DOF DLL
	// that we've implicitly loaded as an in-process COM server. 
	// Fortunately, we don't have to worry about the general case.
	// We're a special case, in that we don't provide a class factory
	// for a public class ID; we only provide a factory for the private
	// class ID defined by our parent process for this single session.  
	// So we can be reasonably certain that the only other process that
	// has created objects through our class factory is our parent.  We
	// can thus delegate the determination that it's safe to exit to the
	// parent, which knows when it has finished releasing all of the
	// objects it created through our factory.  The parent just signaled
	// that it's time for us to shut down, so we assume that it knows
	// what it's talking about.
	return 0;
}
