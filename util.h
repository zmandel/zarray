#pragma once
// The following ifdef block is the standard way of creating macros which make exporting
// from a DLL simpler. All files within this DLL are compiled with the ESTADISTICOS_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see
// ESTADISTICOS_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef UTIL_EXPORTS
#define UTIL_API __declspec(dllexport)
#else
#define UTIL_API __declspec(dllimport)
#endif

#include "..\comun\targetver.h"
#include "..\comun\comun.h"
#include <exception>
#define MULTITHREAD_IMPORTEXPORT UTIL_API
#include "..\comun\multithread.h"
#include <float.h>
#include <math.h>
#include <cstdio>

#ifndef COMUN_H_INCLUIDO
#error Incluir comun.h primero
#endif


#undef  assert
#ifndef  COMPILACION_PARA_DISTRIBUCION_EXTERNA //aqui se pueden prender/apagar los asserts
	#define USE_ASSERTS
#endif

void  UTIL_API myAssert(const wchar_t* message, const wchar_t*file, const unsigned line);
#ifdef  USE_ASSERTS
#define assert(_Expression) (void)( (!!(_Expression)) || (myAssert(_CRT_WIDE(#_Expression), _CRT_WIDE(__FILE__), __LINE__), 0) )	//DEBUG
#define sideassert(_Expression) assert(_Expression)
#else
#define assert(_Expression)     ((void)0)	//RELEASE
#define sideassert(_Expression) _Expression //RELEASE
#endif  /* USE_ASSERTS */

#ifdef USE_ASSERTS
extern UTIL_API BOOL g_fExcepcionBotada; //solo se usa para apagar algunos asserts
#endif

#ifdef USAR_BIGFLOAT
#include "mapm-495\M_APM.H"
#endif

extern UTIL_API WCHAR g_directorio[_MAX_PATH];
extern UTIL_API CProgreso *g_progreso;
UTIL_API void SetProgresoGlobal(CProgreso *progreso);
UTIL_API void SetDirGlobal(WCHAR *directorio);
UTIL_API void GetAssertLogDirectorio(WCHAR *szPath);

typedef enum
	{
	excepcion_desconocida=0,
	excepcion_calc_cancelado,
	excepcion_licencia,
	excepcion_malformato,
	excepcion_archivoMuyGrande,
	} TIPO_EXCEPCION;

//no se exporta, porque debes usar ThrowException si deseas botar una de estas
class CExcepcion_motor : public std::exception
	{
public:
	CExcepcion_motor(TIPO_EXCEPCION te) throw(); //throw() significa que garantiza que no bota excepcion
	virtual ~CExcepcion_motor() throw() //throw() significa que garantiza que no bota excepcion
		{
		}
	TIPO_EXCEPCION TeGet() {return m_te;}
	private:
	TIPO_EXCEPCION m_te;
	};


//CThreadEjecutor
//
//clase base para ejecutar un thread
//debes derivar e implementar EjecutarCodigo.
//cualquier data que necesites la agregas como protected.
//
//NOTA: es obligatorio usar try/catch inmediatamente despues de declarar este objecto, Iniciar debe estar dentro del try/catch,
//      y llamar EsperaTerminar en el bloque del try y en el del catch. Lo siento:(
//
class UTIL_API CThreadEjecutor
	{
friend DWORD WINAPI IniciadorThread(LPVOID lpParam );
public:
	typedef enum {estiloIdealProcessor, estiloAffinity, estiloNinguno} EST;
	typedef enum {esteThread, nuevoThread} OPT;
	typedef enum {opcionNormal=0,opcionIdle,opcionPausa} OPCIONTHREADS;
	virtual void EjecutarCodigo()=0;	//aqui implementas tu codigo que desear se ejecute. Es la unica clase que debes implementar.
	CThreadEjecutor();
	~CThreadEjecutor();

	void Iniciar(OPT opt, int iProcesador=-1); //para comienzar el codigo. opt==nuevoThread crea un nuevo thread. Sino lo ejecuta en el thread actual. iProcesador>=0 para afinity.
	
	//EsperaTerminar. obligatorio llamar siempre. espera a que termine de ejecutar el codigo. 
	//puede botar excepcion si fThrow. Si se llama con fTrow=FALSE, debe llamarse luego con fTrow=TRUE para recibir su error.
	//solo debes usar fThrow=FALSE si manejas multiples threads y debes esperara que todas terminen antes de botar exception
	void EsperaTerminar(BOOL fThrow=TRUE);		
	
	static DWORD CProcessors();			//retorna el numero the procesadores en la maquina. Util para ver cuantos threads crear para un calculo intensivo en paralelo.
	static DWORD CProcessorsReal();		//numero real de procesadores en la maquina. NO usar (usar CProcessors), a menos q realmente necesites saberlo.
	static void Init(int cThreadsForzar, EST est); //debe llamarse desde el main thread, antes que el codigo empieze a usar los metodos. Pasar cThreadsForzar=0 para q los calcule. 
	static void SetMainThread();	//esto se usa para codigo que no es seguro en multithread. Debe llamarse solo 1 vez.
	static BOOL FEnThreadMain();	//TRUE solo si es llamado desde el thread main
	static OPCIONTHREADS OtGet() {return s_ot;}
	static void SetOt(OPCIONTHREADS ot);
private:
	void SetEstilo();
	void RestoreEstilo();
	static void InitCThreads(int cThreadsForzar);
	HANDLE m_hThread;
	int m_iThread;		//el thread	dentro del s_rgThreads global
	int m_iProcesador; //-1 si no hay affinity. Si no, el procesador a usar
	BOOL m_fException;
	BOOL m_fYaHizoThrow;
	TIPO_EXCEPCION m_te;	//tipo de excepcion, si es de las nuestras
	BOOL m_fBadAlloc;		//TRUE si es la excepcion std::badAlloc
	BOOL m_fYaEspero;		//usado	 para verificar que se llamo' a EsperaTerminar antes del destructor. muy importante.
	DWORD m_iaffinityOldIProc;	//para poder restaurar el affinity si se usa "esteThread" (estiloIdealProcessor)
	DWORD m_affinityOldAff;	//para poder restaurar el affinity si se usa "esteThread" (estiloAffinity)
	DWORD m_iaffinityOld;	//para poder restaurar el affinity si se usa "esteThread" (estiloIdealProcessor)
	DWORD m_maskProcessorUse; //el bit dentro del dword indica el procesador que se esta usando. 0 si no se usa un affinity
	static DWORD s_cThreadsUse;
	static DWORD s_cProcessorsReal;
	static BOOL s_fForzarCProcessors;
	static DWORD s_processoraffinity; //bit array de los procesadores libres
	static DWORD s_idThreadMain;
	static EST s_est;
	static LOCKDATA s_lock; //sincroniza s_processoraffinity y cambiar threadpriority (s_rgThreads)
	static OPCIONTHREADS s_ot;
	static void InsertThread(HANDLE hThread, int *piThread);
	static void RemoveThread(int iThread);
	static HANDLE *s_rgThreads;
	static int s_cSizergThreads; //tamanio del array en s_rgThreads. 
	static int s_cThreads;		//cuenta de threads activas de s_rgThreads
	};

#ifdef EXTRA_STATUS
class UTIL_API CTiempoEnFuncion
	{
	public:
	CTiempoEnFuncion(char *szFuncion);
	~CTiempoEnFuncion();
	void Imprime();
	static LONG CIndentCur() {return s_cIndent;}
	private:
	double DGet();
	BOOL m_fImprimio;
	DWORD m_dwTickInicio;
	char m_szFuncion[255];
	static LONG s_cIndent;
	};
#define TIEMPO_EN_FUNCION(sz) CTiempoEnFuncion tiempo(sz)
#define TIEMPO_EN_FUNCION_IMPRIME() tiempo.Imprime()
#else
#define TIEMPO_EN_FUNCION(sz)
#define TIEMPO_EN_FUNCION_IMPRIME()
#endif

UTIL_API void ThrowException(TIPO_EXCEPCION te);
UTIL_API void SetCancelado(BOOL fCancelado=TRUE);	//llamar para indicar que el usuario cancelo' los calculos
UTIL_API BOOL FCanceladoGet();	//TRUE iff usuario cancelo calculos
UTIL_API BOOL FInitUtil(int cThreadsForzar,CThreadEjecutor::EST, BOOL fSubirPrioridad);	//debe llamarse solo una vez, antes de usar cualquier API de util
extern UTIL_API LOCKDATA g_lockZArrayFlush; //un poco feo tenerlo aqui, zarray lo usa para coordinar llamadas a FlushFile en multithread.

#ifdef EXTRA_STATUS
extern UTIL_API LONG g_cZArrayMaps;
#endif
extern UTIL_API MUTEXLOCKDATA g_lockPausa;

inline void CheckStatusFloatingPoint()
	{
	unsigned int floatingStatus=_status87();
	if (floatingStatus&(EM_OVERFLOW|EM_ZERODIVIDE|EM_INVALID))
		{
		//esto detecta algunas excepciones de floating point. En realidad para q detecte todas,
		//hay q cambiar una opcion de compilacion, q solo la tenemos ahora en DEBUG.
		//con esto, en RELEASE agarramos algunas aunque sea.
		ThrowException(excepcion_malformato);
		}
	}

inline BOOL FSimilarDouble(const double &d1, const double &d2)
	{
	double diff=d1-d2;
	return (fabs(diff)<eps);
	}

UTIL_API char *commaprint(unsigned long n, char *retbuf, int cch);	//imprime 100000 como 100,000 etc. cch es la cuenta de chars en el buffer (incluyendo el zero al final)
UTIL_API int read_int(FILE*);
UTIL_API void write_int(FILE *,int, DWORD *pcb=NULL); //pcb se incrementa con sizeof(int) si no es null
UTIL_API double read_double(FILE*);
UTIL_API void write_double(FILE *,const double&, DWORD *pcb=NULL); //pcb se incrementa con sizeof(double) si no es null

class UTIL_API COpenFile
	{
public:
	COpenFile();
	~COpenFile();
	FILE *FileOpen(const WCHAR *szFile, const WCHAR *mode, BOOL fPermiteNoExiste=FALSE);
	void SetPermiteAutoClose(BOOL fAutoClose); //permite no tener que llamar "Close". El default es TRUE para readOnly y FALSE para readWrite. Ver nota en implementacion.
	void Close(); //obligatorio llamar en mode "write". Sino, se borrara' tu archivo. No es obligatorio llamar si solo haces "read".
private:
	FILE *m_pfile;
	BOOL m_fAutoClose;
	WCHAR m_szName[_MAX_PATH];
	};

//CNotificadorFlush
//
//OJO: deben serializarse los threads con un lock en el caso de multithread!
//
class UTIL_API CNotificadorFlush
	{
	public:
	CNotificadorFlush();
	~CNotificadorFlush();
	LONG cVeces() {return s_cVeces;}
	private:
	static LONG s_cVeces;
	};