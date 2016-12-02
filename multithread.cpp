//multithread.cpp
//
//

LOCKDATA::LOCKDATA()
	{
	memset(&m_cs, 0, sizeof(m_cs));
	m_fInited=FALSE;
	m_cLocked=0;
	}

LOCKDATA::~LOCKDATA()
	{
	FinalRelease();
	}

void LOCKDATA::InitializeOnce()
	{
	assert(!m_fInited);
	InitializeCriticalSectionAndSpinCount(&m_cs,0x80000400);
	m_fInited=TRUE;
	}


//FinalRelease
//
//release el criticalsection.
void LOCKDATA::FinalRelease()
	{
	if (m_fInited)
		{
		m_fInited=FALSE;
		//no hacemos assert(m_cLocked==0) ya que en este punto del codigo no se pueden hacer asserts. (podriamos estar borrando el criticalsection del assert)
		//lo que si hacemos para protegernos un poco, es m_fInited=FALSE al comienzo, para reducir la posibilidad de otro thread use CDataLock
		DeleteCriticalSection(&m_cs);
		memset(&m_cs, 0, sizeof(m_cs));
		}
	}

//no usar directamente. usar CMutexDataLock
void LOCKDATA::DoLock()
	{
	if (m_fInited) //por seguridad
		{
		EnterCriticalSection(&m_cs);
		m_cLocked++;
		}
	}

//no usar directamente. usar CMutexDataLock
void LOCKDATA::DoUnlock()
	{
	if (m_fInited)
		{
		m_cLocked--; //OJO: puede no ser zero, si en el callstack hay varios CMutexDataLocks de este MUTEXLOCKDATA
		LeaveCriticalSection(&m_cs);
		}
	}


CDataLock::CDataLock(LOCKDATA *pld)
	{
	assert(pld->m_fInited);
	if (pld->m_fInited) //por seguridad
		{
		pld->DoLock();
		m_fLocked=TRUE;
		m_pld=pld;
		}
	else
		{
		m_pld=NULL;
		m_fLocked=FALSE;
		}
	}

void CDataLock::UnlockNow()
	{
	//NOTA: ver nota en LOCKDATAL::FinalRelease. Probamos m_pld->m_fInited por seguridad.
	if (m_fLocked && m_pld->m_fInited)
		{
		m_pld->DoUnlock();
		m_fLocked=FALSE;
		}
	}

CDataLock::~CDataLock()
	{
	UnlockNow();
	}


MUTEXLOCKDATA::MUTEXLOCKDATA()
	{
	m_hMutex=NULL;
	m_fInited=FALSE;
	m_cLocked=0;
	}

MUTEXLOCKDATA::~MUTEXLOCKDATA()
	{
	FinalRelease();
	}

void MUTEXLOCKDATA::InitializeOnce()
	{
	assert(!m_fInited);
	m_hMutex=CreateMutex(NULL, FALSE, NULL);
	if (m_hMutex==NULL)
		throw std::bad_alloc();
	m_fInited=TRUE;
	}


//FinalRelease
//
//release el criticalsection.
void MUTEXLOCKDATA::FinalRelease()
	{
	if (m_fInited)
		{
		m_fInited=FALSE;
		//no hacemos assert(m_cLocked==0) ya que en este punto del codigo no se pueden hacer asserts.
		//lo que si hacemos para protegernos un poco, es m_fInited=FALSE al comienzo, para reducir la posibilidad de otro thread use CDataLock
		CloseHandle(m_hMutex);
		m_hMutex=NULL;
		}
	}

//en general, no usar directamente. usar CMutexDataLock
void MUTEXLOCKDATA::DoLock()
	{
	if (m_fInited) //por seguridad
		{
		DWORD dwRet=WaitForSingleObject(m_hMutex, INFINITE);
		assert(WAIT_OBJECT_0==dwRet);
		m_cLocked++;
		}
	}

//en general, no usar directamente. usar CMutexDataLock
void MUTEXLOCKDATA::DoUnlock()
	{
	if (m_fInited)
		{
		m_cLocked--; //OJO: puede no ser zero, si en el callstack hay varios CMutexDataLocks de este MUTEXLOCKDATA
		ReleaseMutex(m_hMutex);
		}
	}

CMutexDataLock::CMutexDataLock(MUTEXLOCKDATA *pld)
	{
	assert(pld->m_fInited);
	if (pld->m_fInited) //por seguridad
		{
		pld->DoLock();
		m_fLocked=TRUE;
		m_pld=pld;
		}
	else
		{
		m_pld=NULL;
		m_fLocked=FALSE;
		}
	}

CMutexDataLock::~CMutexDataLock()
	{
	//NOTA: ver nota en MUTEXLOCKDATA::FinalRelease. Probamos m_pld->m_fInited por seguridad.
	if (m_fLocked && m_pld->m_fInited)
		{
		m_pld->DoUnlock();
		}
	}