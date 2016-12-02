// util.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "util.h"
#include <iostream>
#include "..\comun\comun.h"
using namespace std;
#include "..\comun\multithread.cpp"
#include <float.h>
#include <math.h>

BOOL g_fCancelado=FALSE;
UTIL_API WCHAR g_directorio[_MAX_PATH]={0}; //debe iniciarse con zero por lo menos en [0], para saber si fue inicicializado luego
UTIL_API CProgreso *g_progreso=NULL;

#ifdef USE_ASSERTS
UTIL_API BOOL g_fExcepcionBotada=FALSE;
#endif

UTIL_API LOCKDATA g_lockZArrayFlush; //zarray lo usa para coordinar llamadas a FlushFile en multithread.
#ifdef EXTRA_STATUS
UTIL_API LONG g_cZArrayMaps=0;
#endif

//
//g_lockPausa
//
//para pausa, se usa un mutex y no un critical_section.
//esto es porque en windows, un wait infinito en un critical_section podria retornar con status de "posible lock".
//el la practica en realidad eso nunca deberia suceder, porque windows define el periodo maximo de espera de un critical_section
//en 30 dias, pero es un registry setting y algun usuario podria cambiarlo. Entonces, usamos un mutex, que realmente esperara'
//una eternidad si se lo pides. Ya que la pausa puede mostrar GUI y esperar a que el usuario responda, usamos mutex.
//para otras cosas q esperamos que retornen relativamente rapido de un wait, usamos critical_section, ya que ademas es mas eficiente.
UTIL_API MUTEXLOCKDATA g_lockPausa; //permite pausar los threads (assert y gui lo usa)

const char *g_szExcepcionDesconocida = "excepcion no especificada";
const char *g_szExcepcionFormato= "excepcion de formato";
const char *g_szExcepcionCancelado = "calculos cancelados";

void SetSSE2_Estado()
	{
#ifdef DEBUG
	_set_SSE2_enable(0); //esto permite agarrar mas excepciones de float 
#else
	#ifdef HAYS_ZERO
	_set_SSE2_enable(0);
	#else
	_set_SSE2_enable(1);
	#endif
#endif
	}

void NuestroUnexpected() 
	{
	//nota: la implementacion de microsoft nunca llama unexpected(), pero esto esta' a futuro. en caso se llame,
	//llamara' aca.
	assert(FALSE);
	ThrowException(excepcion_desconocida);
	}

CExcepcion_motor::CExcepcion_motor(TIPO_EXCEPCION te) throw() //throw() significa que garantiza que no bota excepcion
		: exception(te==excepcion_calc_cancelado? g_szExcepcionCancelado : (te==excepcion_malformato?g_szExcepcionFormato: g_szExcepcionDesconocida), 1) //el 1 hace que no haga allocations
		{
		m_te=te;
		}

#define ASSERT_FILE L"assertLog.txt"
UTIL_API void GetAssertLogDirectorio(WCHAR *szPath)
	{
	WCHAR logDoc[_MAX_PATH];
	WCHAR logDrive[_MAX_PATH];
	WCHAR logDir[_MAX_PATH];
	if (g_directorio[0]==0)
		{
		assert(FALSE); //no se ha seteado aun el diretorio , porque?
		GetModuleFileName(NULL, logDoc, countof(logDoc));
		_wsplitpath(logDoc, logDrive, logDir, NULL, NULL);
		_wmakepath(logDoc, logDrive, logDir, ASSERT_FILE,NULL);
		}
	else
		{
		wcscpy(logDoc, g_directorio);
		wcscat(logDoc, ASSERT_FILE);
		}
	wcscpy(szPath, logDoc);
	}

#ifdef USE_ASSERTS
__declspec(thread) BOOL tg_fEnAssert=FALSE; //OJO: variable es TLS (per thread)

class CRestauraBool
	{
	public:
		CRestauraBool(BOOL *pb) {m_pb=pb;}
		~CRestauraBool() {*m_pb=FALSE;}
	private:
	BOOL *m_pb;
	};

void  UTIL_API myAssert(const WCHAR* message, const WCHAR*file, const unsigned line)
	{
	if (tg_fEnAssert) //OJO esto es per-thread, no global.
		{
		//proteje en caso que algo aqui genere un assert, entrariamos en un recursivo infinito.
		//nota: esto puede pasar por lo menos en dos situaciones:
		// 1) que esta funcion cause un assert, o
		// 2) que esta funcion, llamada desde el thread de un GUI, al momento de mostrar un GUI genere que otro codigo (como paint)
		//	  sea llamado, y que este codigo paint tenga un assert.
		return;
		}
	CRestauraBool restaura(&tg_fEnAssert);
	tg_fEnAssert=TRUE;
	CMutexDataLock lock(&g_lockPausa); //obliga a hacer cola si varios threads tienen asserts simultaneos. Protege el archivo log, y la ventana de assert que muestra el GUI
	WCHAR logDoc[_MAX_PATH];
	WCHAR logMensaje[5000];
	GetAssertLogDirectorio(logDoc);
	WCHAR archivo[_MAX_PATH];
	int cch= wcslen(file);
	int i=cch-1;
	int cSlash=2;

	while (cSlash>0 && i>0)
		{
		i--;
		if (file[i]=='\\')
			cSlash--;
		}
	if (i>0)
		i++; //saltar ultimo slash
	wcscpy(archivo, file+i);
	wsprintf(logMensaje, L"Error!! %s : %s@%i\n", message, archivo, line);

#ifndef COMPILACION_PARA_DISTRIBUCION_EXTERNA
	MessageBeep(MB_ICONEXCLAMATION);
#endif

	BLOCK
		{
		FILE *fileLog= _wfopen(logDoc, L"a+t");
		if (fileLog)
			{
			SYSTEMTIME systemTime;
			WCHAR dateFormat[100];
			WCHAR timeFormat[100];
			GetLocalTime(&systemTime);
			if ((GetDateFormat(LOCALE_USER_DEFAULT, 0, &systemTime, L"d/M/yy", dateFormat, countof(dateFormat))) &&
			        (GetTimeFormat(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &systemTime, L"hh:mmtt", timeFormat, countof(timeFormat))))
				{
				fwprintf(fileLog, L"%s %s: ", dateFormat, timeFormat);
				}
			fwprintf(fileLog, L"%s", logMensaje);
			fclose(fileLog);
			}
		}
#ifndef COMPILACION_PARA_DISTRIBUCION_EXTERNA
	BLOCK
		{
		if (IDCANCEL==MessageBox(NULL, logMensaje, L"Assert Fallo. Cancel para entrar al debugger.", MB_OKCANCEL))
			{
			__asm int 3	//HACER "Step Out" (funcion que llamo' a esta) para ver la linea exacta del error (assert)
			}
		}
#else
#ifdef DEBUG
	__asm int 3
#endif //DEBUG
#endif //COMPILACION_PARA_DISTRIBUCION_EXTERNA

	//el destructor de restaura setea tg_fEnAssert=FALSE;
	}

#else //not USE_ASSERTS
void  UTIL_API myAssert(const WCHAR* message, const WCHAR*file, const unsigned line)
	{
	}
#endif

void PrenderExcepcionesDeFloat()
	{
	//NOTA: en release no queremos prender excepciones. los instrinsics de math no usan SSE2 si se prenden excepciones.
	//nosotros chequeamos manualmente excepciones en puntos claves del codigo con CheckStatusFloatingPoint
	_clear87(); //Always call _clearfp before setting the control word
	unsigned int fp=0;
	unsigned int cw = _controlfp(0, 0); //Get the default control word
	//unsigned int cwPrecisionOld = (cw&_MCW_PC);
#ifdef DEBUG
	const unsigned int flags=(EM_OVERFLOW|EM_ZERODIVIDE|EM_INVALID);
	cw &=~flags;
	cw|=(EM_DENORMAL|EM_UNDERFLOW);
#else
	cw|=(EM_DENORMAL|EM_UNDERFLOW|EM_OVERFLOW|EM_ZERODIVIDE|EM_INVALID);
#endif

	unsigned int cwOriginal = _controlfp(cw, MCW_EM); //Set it.
	//unsigned int cwPrecision = _controlfp(_PC_64 , _MCW_PC);
	}

DWORD CThreadEjecutor::s_cThreadsUse=0; //0 == no inicializado
DWORD CThreadEjecutor::s_cProcessorsReal=0; //0 == no inicializado
DWORD CThreadEjecutor::s_processoraffinity=0; //0 == no inicializado
DWORD CThreadEjecutor::s_idThreadMain=0;	//NOTA: se asume que 0 es invalido, para poder hacer asserts. Pero, no depender de ello en la logica del codigo, podria ser valido en windows. 
BOOL CThreadEjecutor::s_fForzarCProcessors=FALSE;
LOCKDATA CThreadEjecutor::s_lock; //sincroniza s_processoraffinity
CThreadEjecutor::EST CThreadEjecutor::s_est=CThreadEjecutor::estiloNinguno; //estilo de thread scheduling
CThreadEjecutor::OPCIONTHREADS CThreadEjecutor::s_ot=CThreadEjecutor::opcionNormal; //estilo de thread scheduling
HANDLE *CThreadEjecutor::s_rgThreads=NULL;
int CThreadEjecutor::s_cThreads=0;		//cuenta de threads de s_rgThreads
int CThreadEjecutor::s_cSizergThreads=0; //tamanio del array en s_rgThreads

void CThreadEjecutor::InsertThread(HANDLE hThread, int *piThread)
	{
	CDataLock datalock(&s_lock); //protege s_rgThreads, s_cThreads
	SetThreadPriority(hThread, s_ot==opcionIdle? THREAD_PRIORITY_IDLE: THREAD_PRIORITY_NORMAL);
	s_cThreads++;
	if (s_cSizergThreads<s_cThreads)
		{
		assert(s_cSizergThreads+1==s_cThreads);
		int cSizeNuevo=s_cSizergThreads+32; //crece de 32 en 32 para que sea mas eficiente
		HANDLE *rgNuevo= new HANDLE[cSizeNuevo];
		int i;
		//copiar el array antiguo al nuevo
		for (i=0; i<s_cSizergThreads; i++)
			{
			rgNuevo[i]=s_rgThreads[i];
			}
		for ( ; i<cSizeNuevo; i++)
			{
			rgNuevo[i]=NULL;
			}
		delete [] s_rgThreads;
		s_rgThreads=rgNuevo;
		s_cSizergThreads=cSizeNuevo;
		}
	BLOCK
		{
		int i=s_cThreads-1; //caso mas comun, thread se inserta al final del array activo
		assert(*piThread==-1); //invalido
		if (s_rgThreads[i]!=NULL)
			{
			//caso menos comun. actualmente puede suceder si estamos creando un pool de threads, y uno de los threads
			//termina mientras seguimos creando el pool. en ese caso, s_cThreads habra retrocedido, y este slot
			//ya estara' tomado, asi que debemos buscar el slot vacio.
			for (i=0; i<s_cSizergThreads; i++)
				{
				if (s_rgThreads[i]==NULL)
					{
					break;
					}
				}
			}
		assert(i<s_cSizergThreads);
		assert(s_rgThreads[i]==NULL);
		s_rgThreads[i]=hThread;
		*piThread=i;
		}

#ifdef DEBUG
	BLOCK
		{
		int j;
		int cActive=0;
		for (j=0; j<s_cSizergThreads; j++)
			{
			if (s_rgThreads[j]!=NULL)
				cActive++;
			}
		assert(cActive==s_cThreads);
		}
#endif
	}

void CThreadEjecutor::RemoveThread(int iThread)
	{
	CDataLock datalock(&s_lock); //protege s_rgThreads, s_cThreads
	assert(s_rgThreads[iThread]!=NULL);
	s_rgThreads[iThread]=NULL;
	s_cThreads--;
	if (s_cThreads==0)
		{
		s_cSizergThreads=0;
		delete [] s_rgThreads;
		s_rgThreads=NULL;
		}
	}

void CThreadEjecutor::SetOt(OPCIONTHREADS ot)
	{
	assert(ot==opcionNormal || ot==opcionIdle); //pausa se hace de otra forma
	HANDLE hThreadThis=GetCurrentThread(); //ojo: pseudo-handle. no puede ser comparado con los handles del array
	int priorityOld=GetThreadPriority(hThreadThis);
	CDataLock datalock(&s_lock); //protege s_rgThreads, s_cThreads
	if (s_ot==ot)
		return;
	s_ot=ot;
	//cambiamos la prioridad, para que esto suceda sin pelear con los threads por tiempo, asi no se queda un thread con prioridad distinta a otro por mucho tiempo
	SetThreadPriority(hThreadThis, THREAD_PRIORITY_ABOVE_NORMAL);
	assert(s_cSizergThreads>=s_cThreads);
	int i;
	int priority = (ot==opcionIdle? THREAD_PRIORITY_IDLE: THREAD_PRIORITY_NORMAL);
	assert(priority != THREAD_PRIORITY_ABOVE_NORMAL); //si deseamos esto, hay q cambiar el codigo de restauracion de threads
	int cProcesadas=0;
	for (i=0; i<s_cSizergThreads;i++)
		{
		HANDLE hThread=s_rgThreads[i];
		if (hThread!=NULL)
			{
			SetThreadPriority(hThread, priority);
			cProcesadas++;
			if (cProcesadas==s_cThreads)
				{
				//el resto son NULL
#ifdef DEBUG
				int j;
				for (j=i+1; j<s_cSizergThreads; j++)
					{
					assert(s_rgThreads[j]==NULL);
					}
#endif
				break;
				}
			}
		}
	if (THREAD_PRIORITY_ABOVE_NORMAL==GetThreadPriority(hThreadThis))
		{
		//no se cambio' la prioridad de este thread en el loop, asi que restaurar
		SetThreadPriority(hThreadThis, priorityOld);
		}		
	}

CThreadEjecutor::CThreadEjecutor()
	{
	m_iThread=-1; //valor invalido
	m_hThread=NULL;
	m_fException=FALSE;
	m_fYaHizoThrow=FALSE;
	m_fYaEspero=FALSE;
	m_te=excepcion_desconocida;
	m_fBadAlloc=FALSE;
	m_iProcesador=-1;
	m_iaffinityOldIProc=-1;
	m_affinityOldAff=0;
	m_maskProcessorUse=0;
	}

void CThreadEjecutor::SetEstilo()
	{
	int iProcesador=m_iProcesador; //internamente solo se usa para saber is es -1 o no
	if (iProcesador==-1 || s_est==estiloNinguno)
		return;
	m_iaffinityOldIProc=-1; //no asignada
	m_affinityOldAff=0; //no asignada
	assert(s_cProcessorsReal<=32);
	BOOL fEstiloIdealProcessor=(s_est==estiloIdealProcessor);
	assert(fEstiloIdealProcessor || s_est==estiloAffinity);
	if (iProcesador>=0 && iProcesador<(int)s_cProcessorsReal)
		{
		assert(iProcesador<32); //limite de windows son 32 procesadores, y se guardan en un array de 32 bits
		assert(sizeof(DWORD)==32/8); //cuando compilemos en 64 bits, esto debe cambiar
		DWORD iideal=0;
		CDataLock datalock(&s_lock); //protege s_processoraffinity
		DWORD paActual=s_processoraffinity;
		if (paActual==0)
			{
			//ya fueron tomados todos.
			return;
			}
		DWORD maskUse=1;
		do
			{
			if (paActual%2 == 1)
				break;
			iideal++;
			paActual=paActual>>1;
			maskUse=maskUse<<1;
			}
		while (paActual>0);
		
		if (iideal>=32)
			{
			assert(FALSE);
			return;
			}

		assert(maskUse>0);
		BOOL fOK=FALSE;

		if (fEstiloIdealProcessor)
			{
			m_iaffinityOldIProc=SetThreadIdealProcessor(GetCurrentThread(), iideal);
			fOK=(m_iaffinityOldIProc!=-1);
			}
		else
			{
			m_affinityOldAff=SetThreadAffinityMask(GetCurrentThread(), maskUse);
			fOK=(m_affinityOldAff>0);
			}

		if (fOK)
			{
			s_processoraffinity=(s_processoraffinity& (~maskUse));
			m_maskProcessorUse=maskUse;
			}
		else
			{
			assert(FALSE);
			}
		}
	}

void CThreadEjecutor::RestoreEstilo()
	{
	BOOL fRestorar=FALSE;

	if (m_iaffinityOldIProc!=-1)
		{
		assert(m_maskProcessorUse>0);
		assert(m_affinityOldAff==0);
		DWORD ret=SetThreadIdealProcessor(GetCurrentThread(),m_iaffinityOldIProc);
		assert(ret != -1);
		fRestorar=TRUE;
		}
	else if (m_affinityOldAff>0)
		{
		assert(m_maskProcessorUse>0);
		assert(m_iaffinityOldIProc==-1);
		DWORD ret=SetThreadAffinityMask(GetCurrentThread(), m_affinityOldAff);
		assert(ret >0);
		fRestorar=TRUE;
		}

	m_iaffinityOldIProc=-1;
	m_affinityOldAff=0;

	if (fRestorar)
		{
		CDataLock datalock(&s_lock); //protege s_processoraffinity
		s_processoraffinity|=m_maskProcessorUse; //libera este procesador
		}
	}

DWORD WINAPI IniciadorThread(LPVOID lpParam )
	{
	set_unexpected(NuestroUnexpected);
	PrenderExcepcionesDeFloat();
	SetSSE2_Estado();
	CThreadEjecutor *pth=(CThreadEjecutor*)lpParam;
	assert(lpParam);
	assert(pth->m_iThread>=0);
	pth->SetEstilo();
	try
		{
		assert(pth->m_fException);
		pth->EjecutarCodigo();
		pth->m_fException=FALSE;
		}
	catch (CExcepcion_motor &ex)
		{
		assert(pth->m_fException);
		pth->m_te=ex.TeGet();
		}
	catch (std::bad_alloc &)
		{
		assert(pth->m_fException);
		assert(FALSE);	//para hacer debugging mas facil. puede suceder en realidad, sin te quedaste sin RAM o algo asi
		pth->m_fBadAlloc=TRUE;
		}
	catch (...)
		{
		assert(pth->m_fException);
		assert(FALSE);	//esto si que nunca deberia de llegar aca. podria ser un crash, debuggear bien que paso', podriamos tener memoria corrupta en este punto.
		pth->m_te=excepcion_desconocida; //no nos queda otra, no podemos guardarla si no sabemos que es
		}
	pth->RestoreEstilo();
	CThreadEjecutor::RemoveThread(pth->m_iThread);
	if (pth->m_fException)
		SetCancelado(); //hacer que otros threads fallen tambien
	return 1;
	}

DWORD CThreadEjecutor::CProcessorsReal()
	{
	assert(s_cProcessorsReal>0);
	return s_cProcessorsReal;
	}

DWORD CThreadEjecutor::CProcessors()
	{
	assert(s_cThreadsUse>0 && s_cProcessorsReal>0);	//si no, no se inicializo la clase. esto debe hacerse desde el main thread antes que nada
	DWORD cReturn=s_cThreadsUse;
	if (s_fForzarCProcessors)
		return cReturn;
#ifdef DEBUG
#ifndef USAR_BIGFLOAT
	if (cReturn==1)
		cReturn=2;
#endif
#endif
	return cReturn;
	}

void CThreadEjecutor::Iniciar(OPT opt, int iProcesador/*=-1*/)
	{
	//NOTA: al modificar esta funcion, pensar en EsperaTerminar, que podria ser llamado sin haber llamado Iniciar (es valido).
	assert(m_hThread==NULL);
	DWORD idThread;
	m_fException=TRUE; //culpable hasta probado inocente.
	m_iProcesador=iProcesador;
	if (opt==esteThread)
		{
		SetEstilo();
		try
			{
			//simular lo que hace IniciadorThread
			EjecutarCodigo();
			m_fException=FALSE;
			}
		//NOTA: podriamos no hacer catch ya que estamos en esteThread. Pero lo hacemos para que sea consistente con el comportamiento del caso otroThread.
		catch (CExcepcion_motor &ex)
			{
			assert(m_fException);
			m_te=ex.TeGet();
			}
		catch (std::bad_alloc &)
			{
			assert(m_fException);
			assert(FALSE);	//para hacer debugging mas facil. puede suceder en realidad, sin te quedaste sin RAM o algo asi
			m_fBadAlloc=TRUE;
			}
		catch (...)
			{
			assert(m_fException);
			assert(FALSE);	//esto si que nunca deberia de llegar aca. podria ser un crash, debuggear bien que paso', podriamos tener memoria corrupta en este punto.
			m_te=excepcion_desconocida; //no nos queda otra, no podemos guardarla si no sabemos que es
			}
		RestoreEstilo();
		}
	else
		{
		m_hThread=CreateThread(NULL, 0, IniciadorThread, this, CREATE_SUSPENDED, &idThread);
		if (m_hThread==NULL)
			{
			assert(FALSE);	//no deberia de suceder
			ThrowException(excepcion_desconocida);
			}
		CThreadEjecutor::InsertThread(m_hThread, &m_iThread);
		ResumeThread(m_hThread);
		}
	}

void CThreadEjecutor::EsperaTerminar(BOOL fThrow)
	{
	//ojo: puede ser llamado inclusive si nunca se llamo' a Iniciar.
	if (m_hThread!=NULL)
		{
		WaitForSingleObject(m_hThread, INFINITE);
		CloseHandle(m_hThread);
		m_hThread=NULL;
		}
	m_fYaEspero=TRUE; //NOTA: ya podria ser TRUE si se llamo con fThrow=FALSE. Esta bien ya que ahora debemos hacer el throw en el caso de exception
	if (fThrow && m_fException && !m_fYaHizoThrow)
		{
		m_fYaHizoThrow=TRUE; //esto es necesario para el caso que se llama nuevamente a EsperaTerminar desde un catch.
		//intentamos botar la misma excepcion que recibimos.
		if (m_fBadAlloc)
			throw std::bad_alloc();
		else
			ThrowException(m_te);
		}
	}

CThreadEjecutor::~CThreadEjecutor()
	{
	//super importante! debiste llamar EsperaTerminar desde tu codigo, ambos en el try y en el catch
	assert(m_fYaEspero);
	assert(m_hThread==NULL);
	}

typedef BOOL (WINAPI *LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

DWORD CountSetBits(ULONG_PTR bitMask)
	{
	DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
	DWORD bitSetCount = 0;
	ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
	DWORD i;

	for (i = 0; i <= LSHIFT; ++i)
		{
		bitSetCount += ((bitMask & bitTest)?1:0);
		bitTest/=2;
		}

	return bitSetCount;
	}

void CThreadEjecutor::Init(int cThreadsForzar, CThreadEjecutor::EST est)
	{
	assert(s_est==estiloNinguno);
	s_est=est;
	s_lock.InitializeOnce();
#ifdef USAR_BIGFLOAT
	cThreadsForzar=1;	//USAR_BIGFLOAT no es multithread (se puede hacer, pero por ahora no lo es)
#endif
	InitCThreads(cThreadsForzar);
	}

void CThreadEjecutor::SetMainThread()
	{
	assert(s_idThreadMain==0); //osea, aun no incializamos
	s_idThreadMain=GetCurrentThreadId();
	assert(s_idThreadMain != 0); //esperemos que zero no sea valido. pero igual el codigo no depende de esto, es solo para asserts
	}

BOOL CThreadEjecutor::FEnThreadMain()
	{
	assert(s_idThreadMain!=0); //deberia haberse inicializado
	assert(s_cThreadsUse!=0); //se usa esto como test q se ha llamado Init.
	return (GetCurrentThreadId()==s_idThreadMain);
	}

void CThreadEjecutor::InitCThreads(int cThreadsForzar)
	{
	if (s_cThreadsUse!=0)
		{
		assert(FALSE); //ya fue inicializado
		//no se puede volver a inicializar, porque esta funcion no es multithread
		return;
		}

	s_cThreadsUse=1; //valor default, en caso algo falle luego
	s_cProcessorsReal=1;

	DWORD processAffinityMask=0;
	DWORD systemAffinityMask=0;

	if (!GetProcessAffinityMask(GetCurrentProcess(), &processAffinityMask, &systemAffinityMask))
		return;
	s_cProcessorsReal=CountSetBits(processAffinityMask);
	s_processoraffinity=processAffinityMask;
	if (cThreadsForzar>0)
		{
		s_fForzarCProcessors=TRUE;
		s_cThreadsUse=cThreadsForzar;
		return;
		}
	s_cThreadsUse=s_cProcessorsReal;
	}

UTIL_API void ThrowException(TIPO_EXCEPCION te /*=excepcion_desconocida*/)
	{
	assert(te==excepcion_calc_cancelado || te==excepcion_licencia || te==excepcion_archivoMuyGrande); //las unicas que realmente esperamos recibir, que no son error de programacion
#ifdef USE_ASSERTS
	g_fExcepcionBotada=TRUE; //solo se usa para apagar algunos asserts
#endif
	throw CExcepcion_motor(te);
	}

//llamar para indicar que el usuario cancelo' los calculos
UTIL_API void SetCancelado(BOOL fCancelado/*=TRUE*/)
	{
	if (fCancelado)
		{
		//si estaba en idle, despertar los threads
		CThreadEjecutor::SetOt(CThreadEjecutor::opcionNormal);
		}
	g_fCancelado=fCancelado;
	}

//TRUE iff usuario cancelo calculos
UTIL_API BOOL FCanceladoGet()
	{
	return g_fCancelado;
	}

UTIL_API void SetProgresoGlobal(CProgreso *progreso)
	{
	g_progreso=progreso;
	}

UTIL_API void SetDirGlobal(WCHAR *directorio)
	{
	wcscpy(g_directorio, directorio);
	int c = wcslen(directorio);
	if (directorio[c-1] != '\\')
		wcscat(g_directorio, L"\\"); //siempre termina en "\"
	}

void NuestroInvalidParameterHandler(const wchar_t* /*expression*/,
   const wchar_t* /*function*/, 
   const wchar_t* /*file*/, 
   unsigned int /*line*/, 
   uintptr_t /*pReserved*/)
	{
	assert(FALSE);
	ThrowException(excepcion_malformato);
	}


BOOL FInitUtil(int cThreadsForzar, CThreadEjecutor::EST est, BOOL fSubirPrioridad)
	{
	PrenderExcepcionesDeFloat();
	g_lockPausa.InitializeOnce();
	g_lockZArrayFlush.InitializeOnce();
	SetSSE2_Estado();
#ifdef USAR_BIGFLOAT
	m_apm_cpp_precision(PRECISION_BIGFLOAT);
#endif
	set_unexpected(NuestroUnexpected);
	_set_invalid_parameter_handler(NuestroInvalidParameterHandler);
	if (fSubirPrioridad)
		{
		//ojo: esto NO debe ser el default. hace que otros programas no corran bien.
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS); //permite hacer timings mas precisos
		}
	CThreadEjecutor::Init(cThreadsForzar, est); //debe hacerse antes que todo
	return TRUE;
	}

#ifdef EXTRA_STATUS
LONG CTiempoEnFuncion::s_cIndent=0;
CTiempoEnFuncion::CTiempoEnFuncion(char *szFuncion)
	{
	m_dwTickInicio=GetTickCount();
	m_szFuncion[0]=0;
	lstrcpynA(m_szFuncion, szFuncion, countof(m_szFuncion));
	m_fImprimio=FALSE;
	char szBuff[500];
	sprintf_s(szBuff, "-> %s", m_szFuncion);
	g_progreso->SetEtapa(szBuff,-1,TRUE);
	InterlockedIncrement(&s_cIndent);
	}

void CTiempoEnFuncion::Imprime()
	{
	if (m_fImprimio)
		return;
	InterlockedDecrement(&s_cIndent);
	m_fImprimio=TRUE;
	double diff= DGet();
	char szBuff[500];
	sprintf_s(szBuff, "<- %s. (%.2f s).", m_szFuncion, diff);
	g_progreso->SetEtapa(szBuff,-1,TRUE);
	}

CTiempoEnFuncion::~CTiempoEnFuncion()
	{
	Imprime();
	}

double CTiempoEnFuncion::DGet()
	{
	DWORD dwTickFin=GetTickCount();
	double diff= ((double)(dwTickFin-m_dwTickInicio))/1000;
	return diff;
	}
#endif //EXTRA_STATUS

//commaprint
//
//imprime 100000 como 100,000 etc. cch es la cuenta de chars en el buffer (incluyendo el zero al final)
char *commaprint(unsigned long n, char *retbuf, int cch)
	{
	assert(cch>=15);
	static int comma = ',';
	char *p = &retbuf[cch-1];
	int i = 0;
	*p = '\0';

	do
		{
		if(i%3 == 0 && i != 0)
		*--p = comma;
		*--p = '0' + n%10;
		n /= 10;
		i++;
		}
	while(n != 0);
	return p;
	}

//archivos binarios
int read_int(FILE *file)
	{
	static int s_cVeces=0;
	s_cVeces++;
	if ((s_cVeces%256)==0)
		Posible_Exception;
	int res=0;
	if (1!=fread(&res,sizeof(int),1,file))
		{
		assert(FALSE);
		ThrowException(excepcion_malformato);
		}
	return res;
	}
void write_int(FILE *file,int val, DWORD *pcb /*=NULL*/)
	{
	static int s_cVeces=0;
	s_cVeces++;
	if ((s_cVeces%256)==0)
		Posible_Exception;
	if (1!=fwrite(&val,sizeof(int),1,file))
		{
		assert(FALSE);
		ThrowException(excepcion_malformato);
		}
	if (pcb)
		*pcb+=sizeof(int);
	}
double read_double(FILE *file)
	{
	static int s_cVeces=0;
	s_cVeces++;
	if ((s_cVeces%256)==0)
		Posible_Exception;
	double res=0;
	if (1!=fread(&res,sizeof(double),1,file))
		{
		assert(FALSE);
		ThrowException(excepcion_malformato);
		}
	return res;
	}
void write_double(FILE *file,const double &val, DWORD *pcb /*=NULL*/)
	{
	static int s_cVeces=0;
	s_cVeces++;
	if ((s_cVeces%256)==0)
		Posible_Exception;
	if (1!=fwrite(&val,sizeof(double),1,file))
		{
		assert(FALSE);
		ThrowException(excepcion_malformato);
		}
	if (pcb)
		*pcb+=sizeof(double);
	}

COpenFile::COpenFile()
	{
	m_pfile=NULL;
	m_fAutoClose=FALSE;
	m_szName[0]=0;
	}


//SetPermiteAutoClose
//
//Nota: si pasas TRUE, pierdes la ventaja que el archivo se auto-borra en caso de excepcion.
//Para archivos readWrite, el default es que no permite autoclose. Para la mayoria de los casos eso es lo correcto.
void COpenFile::SetPermiteAutoClose(BOOL fAutoClose)
	{
	m_fAutoClose=fAutoClose;
	}

COpenFile::~COpenFile()
	{
	if (m_pfile)
		{
		BOOL fAutoClose=m_fAutoClose; //en caso Close decida resetearla
		Close();
		if (!fAutoClose)
			{
			//si este assert te sale, probablemente olvidaste de llamar Close()
			assert(FCanceladoGet()); //podria ser valido si llegamos aca por alguna otra excepcion, pero esto ayuda en el debugger.
			assert(m_szName[0] != 0);
			DeleteFile(m_szName);		//borrar el archivo, ya que su escritura fue incompleta
			}
		}
	}

void COpenFile::Close()
	{
	if (m_pfile)
		{
		int ret=fclose(m_pfile);
		assert(ret==0);	//el stream no debe haberse cerrado aun. Si sucede, quiza el codigo llamo' fclose erroneamente antes
		m_pfile=NULL;
		}
	}

FILE* COpenFile::FileOpen(const WCHAR *szFile, const WCHAR *mode, BOOL fPermiteNoExiste/*=FALSE*/)
	{
	Close();
	FILE *pfile=NULL;
	errno_t err = _wfopen_s(&pfile, szFile, mode);
	if (err!=0 || pfile==NULL)
		{
		if (fPermiteNoExiste && pfile==NULL)
			return NULL;
		assert(false); //archivo no existe, o error
		if (pfile)
			fclose(pfile);
		ThrowException(excepcion_malformato);
		}
	assert(wcslen(szFile)+1 <= countof(m_szName));
	wcscpy(m_szName, szFile);
	if (!m_fAutoClose)
		m_fAutoClose= (NULL==wcschr(mode, 'w'));
	m_pfile = pfile;
	return pfile;
	}

LONG CNotificadorFlush::s_cVeces=0;

CNotificadorFlush::CNotificadorFlush()
	{
	//NOTA: asume que caller se encarga de que se serializen los threads que llaman al constructor/destructor.
	s_cVeces++; //multithread: esta dentro de un lock, asi que se puede hacer esto
	g_progreso->NotificaInicioFlush(s_cVeces);
	}

CNotificadorFlush::~CNotificadorFlush()
	{
	g_progreso->NotificaFinFlush(s_cVeces);
	}