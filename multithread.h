//multithread.h
//
//
#pragma once
#ifndef MULTITHREAD_IMPORTEXPORT
#error debes definir esto antes! probablemente debes incluir util.h en vez de esto
#endif

//LOCKDATA
//
//para CRITICAL_SECTIONs
//se usa para la clase CDataLock. se comparte entre multiples threads, y debe llamarse InitializeOnce
//solo una vez, antes de que CDataLock la use.
//
class MULTITHREAD_IMPORTEXPORT LOCKDATA
	{
	friend class CDataLock;
public:
	LOCKDATA();
	~LOCKDATA();
	void InitializeOnce(); //llamar solo una vez durante la vida del DLL
	void FinalRelease();	//llamar cuando el DLL termina.
	BOOL FInited() {return m_fInited;}
	inline BOOL FLocked() {return (m_cLocked>0);}
private:
	void DoLock(); //no usar directamente. usar CDataLock
	void DoUnlock(); //no usar directamente. usar CDataLock
	CRITICAL_SECTION m_cs;
	int m_fInited;
	int m_cLocked;
	};

//CDataLock
//
//se usa en el stack para asegurar que el CRITICAL_SECTION (de LOCKDATA) esta' asegurado en el scope del CDataLock.
//este metodo es el mas seguro, ya que asegura que siempre va a haber un release del LOCKDATA, en caso que haya un "return" temprano en el scope, o
//tambien en caso que haya una excepcion durante el scope. Se toma ventaja de la propiedades del destructor para que se encargue de esto, lo cual
//es una gran ventaja en c++. Esto previene que accidentalmente uno se olvide de hacer release del CRITICAL_SECTION
//
class MULTITHREAD_IMPORTEXPORT CDataLock
	{
public:
	CDataLock(LOCKDATA *pld);
	~CDataLock();
	void UnlockNow(); //se usa en casos especiales, en general debes dejar que el destructor haga el Unlock.

private:
	BOOL m_fLocked;
	LOCKDATA *m_pld;
	};

//MUTEXLOCKDATA
//
//para MUTEX
//se usa para la clase CMutexDataLock. se comparte entre multiples threads, y debe llamarse InitializeOnce
//solo una vez, antes de que CMutexDataLock la use.
//
class MULTITHREAD_IMPORTEXPORT MUTEXLOCKDATA
	{
	friend class CMutexDataLock;
public:
	MUTEXLOCKDATA();
	~MUTEXLOCKDATA();
	void InitializeOnce(); //llamar solo una vez durante la vida del DLL
	void FinalRelease();	//llamar cuando el DLL termina.
	BOOL FInited() {return m_fInited;}
	inline BOOL FLocked() {return (m_cLocked>0);}
	void DoLock(); //en general, no usar directamente. usar CMutexDataLock
	void DoUnlock(); //en general, no usar directamente. usar CMutexDataLock
private:
	HANDLE  m_hMutex;
	int m_fInited;
	int m_cLocked;
	};

//CMutexDataLock
//
//como CDataLock, pero para un MUTEXLOCKDATA
class MULTITHREAD_IMPORTEXPORT CMutexDataLock
	{
public:
	CMutexDataLock(MUTEXLOCKDATA *pld);
	~CMutexDataLock();

private:
	BOOL m_fLocked;
	MUTEXLOCKDATA *m_pld;
	};