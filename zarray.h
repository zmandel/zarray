#pragma once
#include "comun.h"
#include <stdio.h>

#ifndef COMUN_H_INCLUIDO
#error Incluir comun.h primero porque referencia la definicion DEBUG
#endif

#ifndef assert
#include "..\util\util.h"
#endif

//NOTA DE PROGRAMACION:
//
//este codigo funciona en hays, y en el proyecto TestUnitZarray, independiente de util etc.
//por eso hay varios #defines que permiten cambiar a que APIs externas llama.


//prender si se desea probar el zarray con memoria minima.
//Esto tambien puede encontrar algunos errores de mal uso de punteros de zarray.
//#define ZARRAY_MEMORIAMINIMA 


#ifndef NO_USAR_ZARRAY_MULTITHREADLOCK
#define USAR_ZARRAY_MULTITHREADLOCK
//usaremos g_lockZArrayFlush para sincronizar llamadas a FlushFileBuffers 
#endif

#pragma warning (disable:4996)
#pragma warning (disable:4244)

DWORD CbRamDisponible();

//estos enums no estan dentro del ZArray template, porque sino es raro al usarlos.
typedef enum {readWrite, readOnly} ZARRAY_TA;
typedef enum {crearSiempre, abrirSiExiste} ZARRAY_OC;
typedef enum {archivoCompartido, archivoNOCompartido} ZARRAY_AC;
typedef enum {optSecuencial, optRandom} ZARRAY_O;
typedef enum {cfDelete, cfDontDelete, cfFlushSiRAMBajo} ZARRAY_CF;
typedef enum {sizeConstante, sizeVariable} ZARRAY_SIZE;

//REVIEW ziggy: arregla esto, hacer un enum y manejarlo con un parametro extra en SetSizeVentana. Como esta no es limpio, y se rompe en el caso de ZArray<BYTE>
#define ct2DVentanaChica 1024			//ventana chica, util para acceso secuencial de array compartiendo memoria con otros componentes.
#define ct2DVentanaNormal 0xFFFFFFFE	//este tamanio de ventana usa un % del RAM disponible. se comporta mejor cuando otros componentes necesitan memoria
#define ct2DVentanaMaxima 0xFFFFFFFF	//este tamanio de ventana intenta agarrarse todo el RAM disponible
#define ibMOST_32BITS 0xFFFFFFFF
#define cbVENTANA_MAXIMA 0x80000000		//(ibMOST_32BITS+1)/2
#define cbMemDejarLibre 0xCCCCCCC		//200MB libres masomenos

//ZArrayBase
//
//permite manejar ciertos temas generales de zarray, como poder hacer listas de zarrays con diferentes <T>
//
class ZArrayBase
	{
public:
	ZArrayBase() {m_pzFlushAntesQueEste=NULL;}
	void SetFlushAntesQueEste(ZArrayBase *pzarray); //array que se debe hacer flush antes que este cuando se hace un flush implicito (no cuando llamas explicitamente FlushLoEscrito)
	virtual void FlushLoEscrito()=0;
#ifndef COMPILACION_PARA_DISTRIBUCION_EXTERNA
	static void PretendeBajoRam(BOOL fBajoRam) {s_fBajoRam=fBajoRam;}
#endif
#ifdef EXTRA_STATUS
	void FillNombre(char *sz);
#endif
protected:
	virtual void FlushLoEscritoInterno()=0;
	void FlushBase();
	void ResetMembers(BOOL fDesdeConstructor);
	static DWORD FBajoDeRAM();
	BOOL FFlushLista(int iFlush, int *piActual); //retorna FALSE cuando se hizo flush al primer elemento de la lista
	ZArrayBase *m_pzFlushAntesQueEste; //se hace const para evitar cambiar variables del zarray, eso causaria problemas en multithread.
	BOOL m_fEnFlush; //sirve para detectar el bug de referencias circulares de m_pzFlushAntesQueEste 
#ifndef COMPILACION_PARA_DISTRIBUCION_EXTERNA
	static BOOL s_fBajoRam; //se usa solo para testing
#endif
	WCHAR m_filename[_MAX_PATH]; //el archivo de disco
	};

#ifndef COMPILACION_PARA_DISTRIBUCION_EXTERNA
BOOL ZArrayBase::s_fBajoRam=FALSE;
#endif

void ZArrayBase::FlushBase()
	{
#ifdef USAR_ZARRAY_MULTITHREADLOCK
	CDataLock lock(&g_lockZArrayFlush);
#endif
#ifndef NO_NOTIFICADOR_FLUSH
	CNotificadorFlush notificadorFlush; //notifica a principal GUI si el flush demora mucho
#endif
#ifdef EXTRA_STATUS
	char szDebug[100];
	char szFile[_MAX_FNAME];
	FillNombre(szFile);
	sprintf(szDebug,"FLUSH ZARRAY %i (%s)", notificadorFlush.cVeces(),szFile);
	g_progreso->SetTextoDebug(szDebug);
#endif
	FlushLoEscritoInterno();
#ifdef EXTRA_STATUS
	g_progreso->SetTextoDebug("");
#endif

#ifndef COMPILACION_PARA_DISTRIBUCION_EXTERNA
	if (s_fBajoRam)
		Sleep(5000);	//hacer que demore el flush, asi se puede testear el texto que sale solo si demora el flush en principal
#endif
	}

#ifdef EXTRA_STATUS
void ZArrayBase::FillNombre(char *sz)
	{
	assert(m_filename[0]!=0);
	WCHAR szNombre[_MAX_FNAME];
	szNombre[0]=0;
	_wsplitpath(m_filename, NULL, NULL, szNombre, NULL);
	sz[0]=0;
	WideCharToMultiByte(CP_UTF8, 0, szNombre, -1, sz, _MAX_FNAME, NULL, NULL);
	}
#endif

void ZArrayBase::ResetMembers(BOOL fDesdeConstructor)
	{
	m_pzFlushAntesQueEste=NULL;
	m_fEnFlush=FALSE;
	m_filename[0]=0;
	}

void ZArrayBase::SetFlushAntesQueEste(ZArrayBase *pzarray)
	{
	m_pzFlushAntesQueEste=pzarray;
	}

BOOL ZArrayBase::FFlushLista(int iFlush, int *piActual)
	{
#ifdef USAR_ZARRAY_MULTITHREADLOCK
	CDataLock lock(&g_lockZArrayFlush);
#endif
	if (m_fEnFlush)
		{
		//la lista de flush es circular!
		assert(FALSE);
		//intenta sobrevivir
		FlushLoEscrito();
		return TRUE;
		}
	if (iFlush==(*piActual) || m_pzFlushAntesQueEste==NULL)
		{
		FlushLoEscrito();
		return (*piActual!=0);
		}
	(*piActual)++;
	m_fEnFlush=TRUE;
	BOOL fRet=m_pzFlushAntesQueEste->FFlushLista(iFlush, piActual);
	m_fEnFlush=FALSE;
	assert(fRet);
	return TRUE;
	}

DWORD ZArrayBase::FBajoDeRAM()
	{
#ifndef COMPILACION_PARA_DISTRIBUCION_EXTERNA
	if (s_fBajoRam)
		return TRUE;
#endif
	return (CbRamDisponible()<cbMemDejarLibre);
	}

//ZArray
//
//NOTA #1: actualmente NO se puede usar con objectos T que requieran de un constructor o destructor. Esto se puede cambiar si fuera necesario, pero no lo soporta por ahora.
//NOTA #2: muy importante: si haces &rg[x], el pointer valido solo hasta la proxima llamada de operator[]. Dependiendo del RAM disponible, puede haber un crash si no se cumple esta regla.
//
//convencion de codigo. Nombre de variables y funciones.
//Si el nombre empieza con 'c', es una cuenta. Si empieza con 'i' es un indice. Si empieza con 'p', es un pointer. Ejemplo: 'pb' es pointer a un BYTE.
//Si el nombre continua con 'b' son bytes, 't' son Ts. Por ejemplo 'cb' es 'cuenta de bytes' y 'ct' es 'cuenta de Ts'. 'ib' es 'indice en un array de bytes'
//Internamente, casi todos los calculos se hacen en BYTEs. Solo al momento de retornar variables de una funcion publica se convierte a 'T'.
template <typename T> class ZArray : public ZArrayBase
	{
public:
	T& operator[] (DWORD it2D);
	ZArray();
	ZArray(const ZArray<T> &);	//solo existe para hacer assert(FALSE)
	~ZArray();
	void OpenFile(	ZARRAY_SIZE sizeParam,
					const WCHAR *file,
					ZARRAY_TA tipoAcceso, ZARRAY_OC opcionCreacion, ZARRAY_O optimizacion,
					DWORD ct2DSize,				// 0 para calcularlo si abrirSiExiste
					DWORD cElementos2D=1,		//cantidad de elementos del subarray T[]. Default es 1 si no se usan subarrays (osea un subarray de tamanio 1T)
					DWORD cPartes=1,			//para abrir solo una parte del archivo. Especificar el # de partes aqui. Util para multithread.
					DWORD iParte=0,
					ZARRAY_AC ac=archivoNOCompartido,
					DWORD ct2DVentana=ct2DVentanaNormal,
					DWORD it2DMaximoUsuario=0,	//maximo indice permitido (inclusive). default 0 significa sin limite. Esto se usa solo para sizeVariable
					ZArray<T> *pzMaster=NULL);

	void CloseFile(ZARRAY_CF cf=cfDontDelete); //Para readWrite es OBLIGATORIO llamar, sino el archivo sera borrado en el destructor. Para readOnly es opcional.
	DWORD Ct2DSize() const;   //tamanio actual del array, en subarrays 'T's. Para readWrite, va variando a medida que crece. Para readOnly, el tamanio es constante.
	DWORD CbArchivoSize() const;   //tamanio actual del archivo, si se cierra el array en este punto.
	DWORD It2DPrimero(); //minimo indice teorico posible de operator[]
	DWORD It2DUltimo(); //maximo indice teorico posible de operator[]
	DWORD CElementos2D() {return m_cElementos2D;}
	void UsaVentanaMaxima();	//ayuda en performance si se va a acceder mucho el array, en especial en forma random y/o el cada iteracion se va a procesar muy rapido
	void UsaVentanaNormal();		//usar una ventana normal.
	void UsaVentanaChica();		//usar una ventana chica, util si el array se va a acceder sequencialmente y debe compartir memoria con otros componentes.
	void IntentaVentana(DWORD ct2DElementos, DWORD const it2DStart); //especial para optimizar, indica un tamanio de ventana exacto y donde empezar la ventana.
	void GetFileName(WCHAR *szFile);
	void SetRetro(BOOL fRetro); //Si vas a acceder el array de atras hacia adelante, pasas TRUE. Luego si vas a accederlo de adelante a atras debes llamar nuevamente con FALSE.
	virtual void FlushLoEscrito();
	BOOL FAchicoVentana() {return m_cVecesAchicaVentana>0;}
	BOOL FVentanaMaxima() {return m_fVentanaMaxima;}
	void SetSizeConstante();
	BOOL FTieneMaster() {return (m_pzMaster!=NULL);}
	DWORD It2DMasterEndVentana(); //para optimizaciones
	static DWORD ItMaximoPosibleTeorico() {return s_itUltimo;} //este se puede llamar antes de haber llamado OpenFile. OJO no toma en cuenta subarrays.
	
	//zsort: solo necesitas definir operador <= y operador != para poder usar zsort.
	void zsort(int first=0,int last=-1 /*incluye last*/, BOOL fSoloVerifica=FALSE, BOOL fNOVerifica=FALSE); //debes haber llamado InitSort antes (1 vez por array)
#ifdef EXTRA_STATUS
	DWORD CbVentanaStatus() {return m_cbVentana;}
#endif
	private:
	void partition(int &k, int &start, int &end);
	void zswap(int iz,int der);
	void quicksort(int start, int end);
	virtual void FlushLoEscritoInterno();
	BYTE *IntentaMapView(DWORD *pibStart, DWORD *pibEnd, DWORD ib, DWORD*pcbVentana); //wrapper de MapViewOfFile.
	void Trim();	//arregla el tamanio final del archivo. Solo se llama en casos especiales.
	void SetSizeVentana(DWORD ct2DElementos); //pasar ct2DVentanaMaxima/ct2DVentanaNormal como valores especiales. Nota: no garantiza q se usara tanto como se pide.
	void GetTempFileZArray(WCHAR *szTempFile) const;
	void CloseFileInterno(BOOL fForzarDelete, BOOL fFlush);
	void UndoMapGrowRemap(const DWORD ib, const DWORD ibLimiteDerecho=ibMOST_32BITS);
	void InitVentana(DWORD ct2DVentana);
	void AchicaVentana();
	void InitGranularity();
	DWORD GetMaxVentanaPosible(DWORD *pcbRam) const;
	void UndoMapping(BOOL fUndoViewOnly);
	void DoMapping(DWORD ib, DWORD ibDerechoMaximo=ibMOST_32BITS);			//llama a FDoMappingInterno, intentando hacer flush si falla
	BOOL FDoMappingInterno(DWORD ib, DWORD ibDerechoMaximo);	//retorna FALSE si no hay ram para hacer el mapping
	void ResetMembers(BOOL fDesdeConstructor);
	void ThrowError(TIPO_EXCEPCION te=excepcion_desconocida) const;
	DWORD CbSizeArchivoMost();
	DWORD m_cbGranularity;
	DWORD m_cbVentana;	//NOTA: no esta garantizado q esto sea multiplo de m_cbElemento2D	
	DWORD m_cbVentanaMinima;	
	HANDLE m_hfm;
	BYTE *m_pb;
	DWORD m_cbGrow;
	DWORD m_cbSize;		//indica tamanio del array, puede incluir elementos no inicializados al final.
	HANDLE m_hFile;
	BOOL m_fReadOnly;
	BOOL m_fSizeConstante;
	DWORD m_ibStartVentana;	//mapeamos desde indice iStartVentana hasta ibEndVentana (inclusive)
	DWORD m_ibEndVentana;
	DWORD m_it2DEndVentana;		//existe para performance. relacionado a m_ibEndVentana
	DWORD m_it2DInicioSeguro;	//existe para performance.
	DWORD m_cbAccesed;  //utilizado para cuando crece el array, indica el tamanio del array que ha sido utilizado (nota m_cbSize puede ser mayor)
	BOOL m_fTempFile;		//TRUE si el archivo es temporario
	DWORD m_cElementos2D; //cantidad de elementos para hacer arrays bi-dimensionales.
	DWORD m_it2DUltimoArchivo;	//ultimo indice posible del archivo. Nota: en caso cPartes>1, puede estar fuera del rango
	DWORD m_cbElemento2D;
	DWORD m_cVecesGrow;		//veces que hemos crecido el archivo sin crecer m_cbGrow
	DWORD m_cPartes;
	DWORD m_iParte;
	DWORD m_it2DPrimero; //minimo indice posible del array. Si no se hace por 'cPartes', siempre sera zero.
	DWORD m_it2DUltimo; //maximo indice posible del array inclusive. (depende de cPartes y de si el archivo es tamanio constante o variable)
	BOOL m_fCompartido;
	BOOL m_fRetro;		//TRUE si se va a acceder al array de atras hacia adelante (para optimizar ventana)
	BOOL m_fDeleteOnClose;
	DWORD m_cVecesAchicaVentana;
	DWORD m_cRefCompartido;	//pseudo-referencia: solo se usa para hacer asserts en casos de zarrays q tienen un pointer a otro zarray
	ZArray<T> *m_pzMaster;  //se usa cuando este zarray es un "slave" q comparte con un master zarray. Al master se le incrementa su m_cRefCompartido
	BOOL m_fUsandoMasterMap;	//si el mapping actual fue copiado del master
	BOOL m_fVentanaMaxima; //intenta usar lo maximo posible de ventana
	BOOL m_fCandidatoFlush; //TRUE is este array liberaria ram al hacerle flush
	DWORD m_cbSizeTrim;
	static DWORD s_itUltimo;  //maximo indice posible en el array (inclusive)
	};

template <typename T> void ZArray<T>::SetRetro(BOOL fRetro)
	{
	m_fRetro=fRetro;
	}

template <typename T> void ZArray<T>::GetFileName(WCHAR *szFile)
	{
	assert(m_filename[0]!=0 && _waccess(m_filename,0)==0);
	wcscpy(szFile, m_filename);
	}

//nota sobre s_itUltimo: el final de un array de 2^32bytes (indice ibMOST_32BITS) no puede ser utilizado, sino el tamanio del array seria > 32bits.
//por esto existe s_itUltimo, que es el ultimo casillero del array que puede ser utilizado. Como maximo se pierden 2 casilleros en los 4GB del array.
//nota #2: si sizeof(T) no es multiplo de 2^32, podria usar un casillero mas, pero no lo hacemos para poder generalizar mejor.
template <typename T> DWORD ZArray<T>::s_itUltimo = ((((DWORDLONG)ibMOST_32BITS)+1))/sizeof(T)-2;		//(ojo aritmetica 64bits);

template <typename T> DWORD ZArray<T>::CbSizeArchivoMost()
	{
	assert(m_it2DUltimoArchivo>0);
	assert(m_it2DUltimoArchivo!=ibMOST_32BITS);
	assert(m_cbElemento2D>0);
	return (m_it2DUltimoArchivo+1)*m_cbElemento2D;
	}


template <typename T> void ZArray<T>::ThrowError(TIPO_EXCEPCION te /*=excepcion_desconocida*/) const
	{
	assert(te==excepcion_archivoMuyGrande); //a unica que esperamos
	ThrowException(te);
	}

template <typename T> DWORD ZArray<T>::It2DPrimero()
	{
	assert(m_cPartes>=1); //inicializado
	assert(m_cPartes>1 || m_it2DPrimero==0);
	assert(m_it2DPrimero<ibMOST_32BITS); //si no, no esta inicializado
	return m_it2DPrimero;
	}

template <typename T> DWORD ZArray<T>::It2DUltimo()
	{
	assert(m_it2DUltimo!=ibMOST_32BITS);
	return m_it2DUltimo;
	}

template <typename T> void ZArray<T>::ResetMembers(BOOL fDesdeConstructor)
	{
	ZArrayBase::ResetMembers(fDesdeConstructor);
	assert(m_pzFlushAntesQueEste==NULL);
	m_fSizeConstante=TRUE;
	m_fReadOnly=TRUE;
	m_it2DInicioSeguro=0;
	m_it2DEndVentana=0;
	m_it2DPrimero=ibMOST_32BITS;	//asi podemos detectar que no esta inicializado
	m_hfm=NULL;
	m_pb=NULL;
	m_cbGrow=0;
	m_cbSize=0;
	m_hFile=INVALID_HANDLE_VALUE;	//OJO: no es zero! usar esto para testear si el handle esta asignado
	m_ibStartVentana=0;
	m_ibEndVentana=0;
	m_cbAccesed=0;
	m_it2DUltimoArchivo=ibMOST_32BITS;
	m_it2DUltimo=ibMOST_32BITS;
	m_cElementos2D=0;
	m_cbVentana=0;
	m_cbVentanaMinima=0;
	m_cbGranularity=0;
	m_cbElemento2D=0;
	m_cVecesGrow=0;
	m_fTempFile=FALSE;
	m_cPartes=0;
	m_iParte=0;
	m_fCompartido=FALSE;
	m_fRetro=FALSE;
	m_fDeleteOnClose=FALSE;
	m_cVecesAchicaVentana=0;
	m_cRefCompartido=0;
	m_fVentanaMaxima=FALSE;
	m_fCandidatoFlush=FALSE;
	m_fUsandoMasterMap=FALSE;
	m_cbSizeTrim=0; //zero indica que no se necesita hacer trim
	if (!fDesdeConstructor)
		{
		if (m_pzMaster)
			{
			assert(m_pzMaster->m_cRefCompartido!=0);
			m_pzMaster->m_cRefCompartido--;
			}
		assert(m_cRefCompartido==0);
		}
	m_pzMaster=NULL;
	}

template <typename T> ZArray<T>::ZArray()
	{
	ResetMembers(TRUE);
	}

template <typename T> ZArray<T>::ZArray(const ZArray<T> &zarrayCopiarDe)
	{
	//no esta' permitido copiar un zarray a otro. Quiza llegaste aca por pasar el zarray por valor en una funcion.
	assert(FALSE);
	ThrowException(excepcion_desconocida);
	}


template <typename T> void ZArray<T>::DoMapping(DWORD ib, DWORD ibDerechoMaximo)
	{
	int iFlush=-1; //con -1 se halla la profundidad inicial
	int iActual=0;
	while (FBajoDeRAM() && FFlushLista(iFlush, &iActual))
		{
		iFlush=iActual-1;
		iActual=0;
		}

#ifdef EXTRA_STATUS
	BOOL fAchico=FALSE;
#endif

	while (!FDoMappingInterno(ib, ibDerechoMaximo))
		{
#ifdef EXTRA_STATUS
		fAchico=TRUE;
#endif
		AchicaVentana();
		}

#ifdef EXTRA_STATUS
	if (fAchico && m_pzMaster==NULL)
		{
		char szBuff[100];
		char szFile[_MAX_FNAME];
		FillNombre(szFile);
		sprintf(szBuff, "DEBUG: %s zarray achica ventana", szFile);
		g_progreso->SetEtapa(szBuff,-1);
		}
#endif
	}

//IntentaMapView
//
//simplifica llamadas a MapViewOfFile.
//En especial, se encarga de arreglar los parametros si la ventana es mayor a 2GB
//
template <typename T> BYTE *ZArray<T>::IntentaMapView(DWORD *pibStart, DWORD *pibEnd, DWORD ib, DWORD*pcbVentana)
	{
	assert(m_pb==NULL);
	BYTE *pb = NULL;
	DWORD ibIntenta;
	DWORD cbGranularity=m_cbGranularity;
	DWORD ibStart=*pibStart;
	DWORD ibEnd=*pibEnd;
	DWORD cbVentana=*pcbVentana;
	BOOL fNecesitaQuitaAtras=FALSE;
	BOOL fQuitoAdelante=FALSE;
	assert(ibStart<=ib);
	assert(ibEnd>=ib);
	if (cbVentana>cbVENTANA_MAXIMA)
		{
OtraVez:
		DWORD cbQuita=cbVentana-cbVENTANA_MAXIMA; //debemos quitar esta cantidad de bytes
		if (m_fRetro || fNecesitaQuitaAtras)
			{
			//quita por atras. debe encajar en granularity
			ibIntenta=ibStart+cbQuita;
			DWORD cbMod=ibIntenta%cbGranularity;
			if (cbMod>0)
				ibIntenta+=(cbGranularity-cbMod); //encajar en granularity, pero hacia adelante (ver zarray testunit caso#1)
			if (ibIntenta>ib)
				ibIntenta=ib;
			assert(ibIntenta>=ibStart);
			DWORD cbDelta=ibIntenta-ibStart;
			ibStart=ibIntenta;
			if (cbDelta>cbQuita)
				cbQuita=0;
			else
				cbQuita-=cbDelta;
			assert(cbVentana>cbDelta);
			cbVentana-=cbDelta;
			}
		if (cbQuita>0)
			{
			if (fQuitoAdelante)
				{
				//nunca debe suceder. el maximo de rango es 4GB, y hemos quitado adelante y atras, asi que cbQuita debe ser zero para entonces
				assert(FALSE);
				//proteccion
				return NULL;
				}
			//quita por adelante
			fQuitoAdelante=TRUE;
			DWORD cbQuitaMaximo=ibEnd-(ib+m_cbElemento2D-1);
			if (cbQuita>cbQuitaMaximo)
				{
				cbQuita=cbQuitaMaximo;
				fNecesitaQuitaAtras=TRUE;
				}
			ibEnd-=cbQuita;
			cbVentana-=cbQuita;
			if (fNecesitaQuitaAtras)
				goto OtraVez;
			}
		}
	assert(cbVentana<=cbVENTANA_MAXIMA);
	assert(ibStart+cbVentana-1==ibEnd);
	*pibStart=ibStart;
	*pibEnd=ibEnd;
	*pcbVentana=cbVentana;
	ZArray<T> const *pzMaster=m_pzMaster;
	assert(pb==NULL);
	if (pzMaster)
		{
		if (ibStart>=pzMaster->m_ibStartVentana && ibEnd<=pzMaster->m_ibEndVentana)
			{
			m_fUsandoMasterMap=TRUE;
			pb=pzMaster->m_pb+(ibStart-pzMaster->m_ibStartVentana);
			}
		}

	if (pb==NULL)
		{
		m_fUsandoMasterMap=FALSE;
		assert(m_pb==NULL);
		pb=(BYTE*)MapViewOfFile(m_hfm, FILE_MAP_READ|(m_fReadOnly? 0 :FILE_MAP_WRITE), 0, ibStart, cbVentana);
#ifdef EXTRA_STATUS
		if (pb)
			InterlockedIncrement(&g_cZArrayMaps);
#endif
		
		}
	return pb;
	}

//KB: http://support.microsoft.com/default.aspx?scid=kb;en-us;125713
//On Windows NT, the size of a file mapping object backed by a named disk file is limited by available disk space.
//The size of a mapped view of an object is limited to the largest contiguous block of unreserved virtual memory in the process performing the mapping
//(at most, 2GB minus the virtual memory already reserved by the process).
//
//On Windows 95, the size of a file mapping object backed by a named disk file is limited to available disk space.
//The size of a mapped view of an object is limited to the largest contiguous block of unreserved virtual memory in the shared virtual arena.
//This block will be at most 1GB, minus any memory in use by other components of Windows 95
//Additional limitations when performing file mapping under Windows 95:
//1) The dwOffsetHigh parameters of MapViewOfFile() and MapViewOfFileEx() are not used, and should be zero. Windows 95 uses a 32-bit file system.
//2) The dwMaximumSizeHigh parameter of CreateFileMapping() is not used, and should be zero. Again, this is due to the 32-bit file system.
//END KB
template <typename T> BOOL ZArray<T>::FDoMappingInterno(DWORD ib, DWORD ibDerechoMaximoPedido)
	{
	Posible_Exception;
	assert(m_pb==NULL);
	const DWORD cbSize = m_cbSize;
	const BOOL fReadOnly = m_fReadOnly;
	const DWORD cbElemento2D=m_cbElemento2D;
	const DWORD cbGranularity = m_cbGranularity;
	ZArray<T> const *pzMaster=m_pzMaster;
	assert(cbGranularity>0);
	assert(ib%cbElemento2D==0);

	DWORD ibStart=ib-(ib%cbGranularity);
	assert(ibStart<=ib);
	assert(cbSize>0);
	if (m_hfm==NULL)
		{
		HANDLE hfm;
		if (pzMaster)
			{
			assert(m_fSizeConstante);
			hfm=pzMaster->m_hfm;
			}
		else
			{
			hfm=CreateFileMapping(m_hFile, NULL, (fReadOnly? PAGE_READONLY : PAGE_READWRITE)|SEC_COMMIT, 0, cbSize, NULL);
			}
		if (NULL==hfm)
			{
			//REVIEW ziggy: thow un diskfull si GetLastError()==ERROR_DISK_FULL
			ThrowError();
			}
		m_hfm=hfm;
		}
	
	const DWORD cbVentana=m_cbVentana;
	assert(cbVentana>0);
	assert(cbVentana>=cbElemento2D); //esto lo garantiza el codigo que asigna la ventana. Suposicion #1
	assert(cbVentana<=cbVENTANA_MAXIMA); //esto garantiza que si hay wrap, no sea mayor que ibStart.
	DWORD ibEnd = ib+cbVentana-1;
	DWORD ibLimiteDerecho;
	DWORD ibLimiteDerechoMaximo;
	BLOCK
		{
		BOOL fRetro=m_fRetro;
		if (m_fSizeConstante)
			{
			ibLimiteDerechoMaximo=(m_it2DUltimo+1)*cbElemento2D-1;
			}
		else
			{
			//si el tamanio varia, m_it2DUltimo es el maximo teorico, no el tamanio actual
			ibLimiteDerechoMaximo=cbSize-1;
			}

		if (fRetro)
			{
			//NOTA: aca fijamos el limite derecho, y mas abajo esto hara que movamos el ibStart hacia atras
			ibLimiteDerecho=ib+cbElemento2D-1;
			}
		else
			{
			ibLimiteDerecho=ibLimiteDerechoMaximo;
			}
		assert(ibLimiteDerecho<=cbSize-1);
		}
	//NOTA: aqui limitamos el final de la ventana para que nunca sea mayor a it2DUltimo, esto permite optimizaciones luego.
	assert((ibLimiteDerecho+1)%cbElemento2D==0);
	DWORD sobra=(ibEnd+1)%cbElemento2D;
	if (sobra!=0)
		ibEnd += (cbElemento2D-sobra); //asegurar que un elemento termina exacto en ibEnd, ver assert mas abajo
	if (ibEnd<ibStart || ibEnd>ibLimiteDerecho) //wrap, o mas grande de lo que se necesita
		ibEnd=ibLimiteDerecho;

	assert((ibEnd+1)%cbElemento2D == 0); //esto es necesario por performance y poder hacer simplificaciones
	assert(ibEnd-cbElemento2D+1>=ibStart);//por lo menos entra un elemento
	BYTE *pb=NULL;
	DWORD cbVentanaFinal=ibEnd-ibStart+1;
	if (cbVentanaFinal<cbVentana)
		{
		//extendemos hacia atras
		DWORD ibPrimero=m_it2DPrimero*cbElemento2D;
		if (ibPrimero<ibStart)
			{
			//intentamos extender la ventana hacia atras.
			//Esto optimiza si el array se esta accediendo de atras hacia adelante o random, al final del array
			if (ibEnd-ibPrimero+1<cbVentana)
				ibStart=ibPrimero;
			else
				ibStart=ibEnd-cbVentana+1;
			ibStart=ibStart-(ibStart%cbGranularity);	//encajarlo en granularity
			assert(ibStart<=ib);
			cbVentanaFinal=ibEnd-ibStart+1;
			}
		if (cbVentanaFinal<cbVentana && ibLimiteDerechoMaximo>ibEnd)
			{
			//extendemos hacia adelante
			DWORD cbDelta=cbVentana-cbVentanaFinal;
			DWORD cbFaltaParaMaximo=ibLimiteDerechoMaximo-ibEnd; //asi evitamos wrap y pasarnos del LimiteDerechoMaximo
			ibEnd+=min(cbDelta,cbFaltaParaMaximo);
			cbVentanaFinal=ibEnd-ibStart+1;
			}
		}
	assert(pb==NULL);
	if (ibEnd>ibDerechoMaximoPedido) //mas facil es hacer esto aqui, luego de todo el codigo general de calculo del rango
		{
		assert(ibStart<=ibDerechoMaximoPedido);
		cbVentanaFinal-=(ibEnd-ibDerechoMaximoPedido);
		ibEnd=ibDerechoMaximoPedido;
		}
	pb=IntentaMapView(&ibStart, &ibEnd, ib, &cbVentanaFinal);
	m_pb=pb;
	if (pb==NULL)
		{
		//no hay suficiente RAM.
		return FALSE;
		}
	m_ibStartVentana=ibStart;	//ibStart e ibEnd encajan siempre exacto en los limites de un elemento T
	m_ibEndVentana=ibEnd;
	assert((ibEnd+1)%cbElemento2D == 0);
	assert(ibEnd+1>=cbElemento2D); //necesario para el siguiente calculo que hace -1
	m_it2DEndVentana=(ibEnd+1)/cbElemento2D -1;
	assert(m_it2DEndVentana<=m_it2DUltimo);
	assert(ibStart+(cbElemento2D-1)>=ibStart); //no habra wrap. garantizado ya que en la ventana entra por lo menos un elemento2D
	DWORD it2DInicioVentana=(ibStart+(cbElemento2D-1))/cbElemento2D; //busca el 1er elemento de la ventana
	m_it2DInicioSeguro=max(m_it2DPrimero, it2DInicioVentana); //para optimizar luego en GetT
#if 0
	if (m_it2DInicioSeguro==m_it2DPrimero && m_ibEndVentana==ibLimiteDerechoMaximo)
		abarca todo el rango, se puede usar para algo interesante;
#endif
	return TRUE;
	}

template <typename T> void ZArray<T>::FlushLoEscrito()
	{
	assert(m_hFile);
	if (!m_fCandidatoFlush)
		{
		if (!m_pzMaster || !m_pzMaster->m_fCandidatoFlush)
			return;
		}
	FlushBase();
	}


template <typename T> void ZArray<T>::FlushLoEscritoInterno()
	{
	assert(m_hFile);
	assert(m_fCandidatoFlush || m_pzMaster && m_pzMaster->m_fCandidatoFlush);
	if (m_fCandidatoFlush)
		m_fCandidatoFlush=FALSE;
	if (m_pzMaster && m_pzMaster->m_fCandidatoFlush)
		m_fCandidatoFlush=FALSE;
	//escribir todo el cache del archivo a disco.
	//esto puede demorar, ya que podria tener hasta 4GB
	//EXPLICACION porque' hacemos esto: Si no hacemos este flush, el sistem operativo lo hara' en el background en paralelo con
	//nuestro programa. Esto no es lo ideal, ya que si hemos llegado aca, es porque estamos comiendo RAM mas rapido de lo que
	//el OS puede escribirlo al disco (paging). Por lo tanto, el paging no solo no nos esta sirviendo, sino que tambien nos esta'
	//lentenado. Si dejaramos que continue esto, llegaremos a usar el 100% del RAM y alli si que la computadora se congela, ya que
	//el OS seguira intentando escribir el RAM al disco mientras nosotros seguimos utilizando mas RAM.
	//Al hacer este flush, recuperamos toda la memoria que el archivo esta ocupando en RAM, y nuevamente tendremos mucho RAM disponible.
	//claro que no todo es perfecto, esta llamada puede demorar mucho, ya que el archivo podria tener hasta 4GB de data por escribir.
	//asi que esta funcion es la que tomara' la gran mayoria del tiempo del zarray en casos en que haya poca memoria disponible.
	//esto sucede si 1) el usuario tiene muy poca memoria, y/o 2) estamos escribiendo un zarray readwrite que es enorme y remapeandolo.
	FlushFileBuffers(m_hFile);
	}

template <typename T> void ZArray<T>::UndoMapping(BOOL fUndoViewOnly)
	{
	if (m_hfm)
		{
		if (m_pb)
			{
			assert(m_hFile);
			if (!m_fUsandoMasterMap)
				{
				UnmapViewOfFile(m_pb);
				if (!m_fReadOnly)
					{
					m_fCandidatoFlush=TRUE;
					if (m_pzMaster)
						{
						assert(!m_pzMaster->m_fReadOnly);
						m_pzMaster->m_fCandidatoFlush=TRUE;
						}
					}
				}
			else
				{
				assert(m_pzMaster!=NULL);
				}
			m_pb=NULL;
			}
		if (!fUndoViewOnly)
			{
			if (m_pzMaster==NULL)
				CloseHandle(m_hfm);
			m_hfm=NULL;
			}
		}
	assert(m_pb==NULL);
	assert(fUndoViewOnly || m_hfm==NULL);
	}


template <typename T> DWORD ZArray<T>::CbArchivoSize() const
	{
	assert(!m_fSizeConstante || m_cbSize==m_cbAccesed); //fSizeConstante --> cbSize==cbAccessed
	assert(m_cbAccesed >= 0);
	assert(m_cbSize>0);
	assert(m_cElementos2D>0);
	return m_cbAccesed;
	}

template <typename T> DWORD ZArray<T>::Ct2DSize() const
	{
	assert(m_cbSize>0);
	assert(m_cElementos2D>0);
	assert((m_cbSize%m_cbElemento2D)==0);
	assert((m_cbAccesed%m_cbElemento2D)==0);
	DWORD cbArchivo=CbArchivoSize();
	assert((cbArchivo%m_cbElemento2D)==0);
	return (cbArchivo/m_cbElemento2D);
	}

DWORD Log2_Dbg(DWORD x)
	{
	int r=0;
	while (x>1)
		{
		x/=2;
		r++;
		}
	return r;
	}

DWORD Pow2_Dbg(DWORD x)
	{
	int r=2;
	while (x>1)
		{
		r*=2;
		x--;
		}
	return r;
	}

template <typename T> void ZArray<T>::InitGranularity()
	{
	assert(m_cbGranularity==0);
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	//por definicion de Windows, la ventana de un "view" debe ser multiplo de AllocationGranularity
	m_cbGranularity=si.dwAllocationGranularity; //intentamos lo mas chico posible
	}

template <typename T> DWORD ZArray<T>::GetMaxVentanaPosible(DWORD *pcbRam) const
	{
	DWORD cbMemMax=CbRamDisponible();
	assert(pcbRam);
	*pcbRam=cbMemMax;
	assert(m_pb==NULL); //sino, no tendremos un valor preciso de GlobalMemoryStatusEx, ya que la ventana actual va a interferir con el resultado.
	DWORD cbRet=cbVENTANA_MAXIMA; //2GB limite de windows
	BLOCK
		{
		MEMORYSTATUSEX ms;
		ms.dwLength = sizeof (ms);
		if (GlobalMemoryStatusEx(&ms))
			{
			DWORDLONG avail=ms.ullAvailVirtual;
			if (avail<cbRet)
				cbRet=(DWORD)avail;
			if (cbRet<=cbMemDejarLibre)
				cbRet=1; //luego el caller lo corregira a un minimo establecido
			else
				cbRet-=cbMemDejarLibre;
			}
		}
	if (m_fSizeConstante && cbRet>m_cbSize)
		cbRet=m_cbSize;
	return cbRet;
	}


template <typename T> void ZArray<T>::UsaVentanaMaxima()
	{
	if (m_fVentanaMaxima)
		return;
	m_cVecesAchicaVentana=0;
	UndoMapping(TRUE); //se hace antes de SetSizeVentana para que el mapping actual no interfiera con el calculo de ventana
	SetSizeVentana(ct2DVentanaMaxima);
	UndoMapGrowRemap(m_it2DPrimero*m_cbElemento2D); //forzar a recrear el mapping	REVIEW ziggy
#ifdef EXTRA_STATUS
	if (m_fVentanaMaxima)
		{
		char szBuff[100];
		char szFile[_MAX_FNAME];
		FillNombre(szFile);
		sprintf(szBuff, "DEBUG: %s con ventana maxima", szFile);
		g_progreso->SetEtapa(szBuff,-1);
		}
#endif
	}

template <typename T> void ZArray<T>::UsaVentanaNormal()
	{
	m_cVecesAchicaVentana=0;
	UndoMapping(TRUE); //se hace antes de SetSizeVentana para que el mapping actual no interfiera con el calculo de ventana
	SetSizeVentana(ct2DVentanaNormal);
	UndoMapGrowRemap(m_it2DPrimero*m_cbElemento2D); //forzar a recrear el mapping, para liberar la ventana anterior. REVIEW ziggy
	}

template <typename T> void ZArray<T>::UsaVentanaChica()
	{
	m_cVecesAchicaVentana=0;
	UndoMapping(TRUE); //se hace antes de SetSizeVentana para que el mapping actual no interfiera con el calculo de ventana
	SetSizeVentana(ct2DVentanaChica);
	UndoMapGrowRemap(m_it2DPrimero*m_cbElemento2D); //forzar a recrear el mapping, para liberar la ventana anterior. REVIEW ziggy
	}

//IntentaVentana
//
//Intenta crear una ventana que cubra desde it2DStart, por ct2DElementos elementos.
//NO garantiza que lo va a poder hacer.
//Nunca achica el tamanio de la ventana ya existente, solo la crecera si es necesario.
template <typename T> void ZArray<T>::IntentaVentana(DWORD ct2DElementos, DWORD const it2DStart)
	{
	assert(it2DStart>=m_it2DPrimero && it2DStart<=m_it2DUltimo);
	assert(it2DStart+ct2DElementos>it2DStart && it2DStart+ct2DElementos-1<=m_it2DUltimo);
	DWORD cbElemento2D=m_cbElemento2D;
	DWORD cbGranularity=m_cbGranularity;
	DWORD ibStartReal=it2DStart*cbElemento2D;
	DWORD ibStart=ibStartReal-(ibStartReal%cbGranularity); //encajarlo en el granularity
	assert(ibStart<=ibStartReal);
	DWORD ibEnd=ibStartReal+(ct2DElementos*cbElemento2D)-1;
	DWORD it2DStartVentana=it2DStart;
	assert(m_pzMaster==NULL || m_pzMaster->m_cRefCompartido==1); //si es un slave, este metodo solo funciona solo si hay un solo slave, ya que puede cambiar la ventana del master
	if (ibStart>=m_ibStartVentana && ibEnd<=m_ibEndVentana)
		{
		//la ventana actual lo cubre
		return;
		}
	ZArray<T> *pzMaster=m_pzMaster;
	BOOL fIntentoMaster=FALSE;
	BOOL fStop=FALSE;
	while (pzMaster && !fStop)
		{
		if (ibStart>=pzMaster->m_ibStartVentana && ibEnd<=pzMaster->m_ibEndVentana)
			{
			//la ventana actual del master lo cubre
			return;
			}
		//puede que lo cubran entre ambos
		ZArray<T> *pzLeft;
		ZArray<T> *pzRight;
		if (pzMaster->m_ibStartVentana<=m_ibStartVentana)
			{
			pzLeft=pzMaster;
			pzRight=this;
			}
		else
			{
			pzLeft=this;
			pzRight=pzMaster;
			}
		if (ibStart>=pzLeft->m_ibStartVentana && ibEnd<=pzRight->m_ibEndVentana)
			{
			//ambas ventanas empalman o se intersectan
			if (pzRight->m_ibStartVentana<=pzLeft->m_ibEndVentana+1)
				return;
			}
		if (fIntentoMaster)
			break;
		assert(pzMaster->m_cRefCompartido==1); //si habrian mas, esto corromperia a los demas slaves
		pzMaster->IntentaVentana(ct2DElementos, it2DStartVentana);
		fIntentoMaster=TRUE;
		it2DStartVentana=pzMaster->m_it2DEndVentana+1; //empalma con la ventana del master
		DWORD cElementosAvanza=it2DStartVentana-it2DStart;
		if (cElementosAvanza>=ct2DElementos)
			{
			//hemos cubierto todo. corregimos el it2DStartVentana para que no se salga del limite pedido (sino podria terminar mas alla de m_it2DUltimo.
			//nota que como el master cubre todo, la siguiente llamada de UndoMapGrowRemap va a hacer que el slave haga m_fUsandoMasterMap=TRUE
			it2DStartVentana=it2DStart;
			fStop=TRUE;
			}
		else
			{
			//el slave solo necesitara una cantidad menor de elementos
			ct2DElementos-=cElementosAvanza;
			}
		if (m_fUsandoMasterMap)
			{
			m_fUsandoMasterMap=FALSE;
			m_pb=NULL;
			//obligatorio se tiene que salir, para que this haga su mapping, no puede quedarse con m_pb==NULL
			fStop=TRUE;
			}
		}
	DWORD ctVentanaActual=m_cbVentana/cbElemento2D; //aprox, ya que m_cbVentana no es necesariamente multiplo de cbElemento2D
	UndoMapping(TRUE); //se hace antes de SetSizeVentana para que el mapping actual no interfiera con el calculo de ventana
	if (ctVentanaActual<=ct2DElementos)
		SetSizeVentana(ct2DElementos);
	UndoMapGrowRemap(it2DStartVentana*cbElemento2D);
	}

//SetSizeVentana
//
//pasar ct2DVentanaMaxima/ct2DVentanaNormal como valores especiales. Nota: no garantiza q se usara tanto como se pide.
template <typename T> void ZArray<T>::SetSizeVentana(DWORD ct2DElementos)
	{
	DWORD cbRam;
	DWORD cbMaxVentana=GetMaxVentanaPosible(&cbRam);
	DWORD cbElemento2D=m_cbElemento2D;
	DWORD cbVentana=0;
	m_fVentanaMaxima=FALSE;

	if (ct2DElementos==ct2DVentanaMaxima || ct2DElementos==ct2DVentanaNormal || ct2DElementos>=0xFFFFFFFF/cbElemento2D) //max y wrap
		{
		if (ct2DElementos==ct2DVentanaNormal)
			{
			cbVentana=cbRam/2+1;	//solo agarra 1/2 de todo el ram disponible. +1 es para q no sea zero (puede ser si el array es de bytes, readonly y tamanio 1.
			}
		else
			{
			cbVentana=cbMaxVentana;
			}
		}
	else
		{
		cbVentana=cbElemento2D*ct2DElementos;
		}

	if (cbVentana>cbMaxVentana)
		cbVentana=cbMaxVentana;

	BLOCK
		{
		DWORD cbCubreTodo;
		if (m_cPartes==1)
			{
			cbCubreTodo=m_cbSize;
			}
		else
			{
			assert(m_fSizeConstante);
			cbCubreTodo=(m_it2DUltimo-m_it2DPrimero+1)*m_cbElemento2D;
			}
		if (cbVentana>=cbCubreTodo)
			m_fVentanaMaxima=TRUE;
		}
		
	assert(cbVentana>0);
#if 0
	if (cbRam>cbMemDejarLibre)
		{
		if (cbVentana>cbRam-cbMemDejarLibre)
			cbVentana=cbRam-cbMemDejarLibre;
		}
#endif
	DWORD cbVentanaPagina=m_cbGranularity;
	if (cbVentana<cbVentanaPagina)
		cbVentana=cbVentanaPagina; //es absurdo usar una ventana menor a la de una pagina
	DWORD cbVentanaMinima=m_cbVentanaMinima;
	if (cbVentana<cbVentanaMinima)
		cbVentana=cbVentanaMinima;
#ifdef ZARRAY_MEMORIAMINIMA
	cbVentana=m_cbVentanaMinima;
#endif
	m_cbVentana=cbVentana;
	}


template <typename T> void ZArray<T>::InitVentana(DWORD ct2DVentana)
	{
	assert(m_cElementos2D>0);
	assert(m_cbVentanaMinima==0);
	assert(m_cbVentana==0);
	assert(!m_fVentanaMaxima);
	assert(m_pb==NULL);
	m_cbVentanaMinima=m_cbElemento2D;
	SetSizeVentana(ct2DVentana);
	}

template <typename T> void ZArray<T>::AchicaVentana()
	{
	assert(m_cbVentana>0);
	DWORD cbVentanaOrig=m_cbVentana;
	DWORD cbActual=cbVentanaOrig-(cbVentanaOrig/8);
	assert(m_cbVentanaMinima>0);
	if (cbActual<m_cbVentanaMinima || cbActual==cbVentanaOrig)
		{
		//no se puede hacer mas chica :(
		//aqui es donde se acabo' el espacio virtual de RAM
		throw std::bad_alloc();
		}
	m_cbVentana=cbActual;
	m_fVentanaMaxima=FALSE;
	m_cVecesAchicaVentana++;
	}

template <typename T> void ZArray<T>::GetTempFileZArray(WCHAR *szFile) const
	{
	WCHAR szTempPath[_MAX_PATH];
	DWORD cb=GetTempPathW(countof(szTempPath), szTempPath);
	if (cb==0 || cb>=countof(szTempPath))
		{
		assert(FALSE);
		ThrowError();
		}
	UINT fileid=GetTempFileNameW(szTempPath, L"AGO", 0, szFile);
	if (fileid==0)
		{
		assert(FALSE); //no deberia pasar nunca
		ThrowError();
		}
	}

template <typename T> DWORD ZArray<T>::It2DMasterEndVentana()
	{
	assert(m_pzMaster);
	return (m_pzMaster->m_it2DEndVentana);
	}

template <typename T> void ZArray<T>::SetSizeConstante()
	{
	if (m_fSizeConstante)
		{
		assert(FALSE);
		return;
		}
	assert(m_cbAccesed<=m_cbSize);
	assert(m_cPartes==1);
	//arregla limites, para ignorar el extra que queda al final del array. Asi el codigo luego no se confundira.
	//nota que actualmente esto impide hacer Trim, asi que el codigo no puede depender del tamanio del archivo en el disco para deducir el numero de elementos.
	m_cbSizeTrim=m_cbSize; //esto indica que luego necesitaremos hacer un trim
	m_cbSize=m_cbAccesed;
	m_it2DUltimo=Ct2DSize()-1;
	m_it2DEndVentana=min(m_it2DEndVentana,m_it2DUltimo);
	m_ibEndVentana=min(m_ibEndVentana,m_cbSize-1);
	if (m_cbVentana>m_cbSize)
		m_cbVentana=m_cbSize;
	m_fSizeConstante=TRUE;
	}

//NOTA: em caso que el array crezca, el contenido del area crecida es arbitrario (NO zero!)
template <typename T> void ZArray<T>::OpenFile(
	ZARRAY_SIZE sizeParam,
    const WCHAR *file,						//Nobre del archivo que mantendra' el array. Si es NULL, se crea un archivo temporario que luego se borra (o si es slave, usa el archivo del pzMaster)
    ZARRAY_TA tipoAcceso,					//Solo-lectura? readOnly, sino readWrite
    ZARRAY_OC opcionCreacion,				//crearSiempre -> borra el archivo anterior, si existia. abrirSiExiste -> si no existe, lo crea, si no usa el que ya existe.
    ZARRAY_O optimizacion,					//optimizacion. Utilizacion principal del archivo (no estas obligado a hacerlo exactamente asi, es un hint)
    DWORD ct2DSize,							//Tamanio inicial del array, (en subarrays de T,o T si no se usan subarrays). Si es abrirSiExiste debes pasar 0, y luego Ct2DSize() te lo dara.
    DWORD cElementos2D /*=1*/,				//ver nota en definicion de la clase.
	DWORD cPartes	/*=1*/,					//para abrir solo una parte del archivo. Especificar el # de partes aqui. Util para multithread.
	DWORD iParte/*=0*/,
	ZARRAY_AC ac/*=archivoNOCompartido*/,				//pasar TRUE si se va a compartir el archivo entre varios zarrays
	DWORD ct2DVentana /*=ct2DVentanaNormal*/,
	DWORD it2DMaximoUsuario/* =0 */,		//maximo indice permitido. default 0 significa sin limite. Esto se usa solo para sizeVariable
	ZArray<T> *pzMaster /*=NULL*/)
	{
	if (m_hFile!=INVALID_HANDLE_VALUE)
		{
		//debiste llamar CloseFile en el objecto que estas reusando
		assert(FALSE);
		ThrowError(excepcion_malformato);
		}

	if (cElementos2D==0)
		ThrowError(excepcion_malformato);
	BOOL const fCompartido = (ac==archivoCompartido);
	m_fCompartido=fCompartido;
	m_cElementos2D=cElementos2D;
	InitGranularity();
	//init tamanios y limites
	BLOCK
		{
		if (m_cElementos2D>s_itUltimo/16)
			{
			//el subarray de T[] es demasiado grande, seria problematico evitar wraps al momento de crecer el array.
			//en realidad, se podria trabajar con (s_itUltimo+1)/2, pero el codigo de prevencion de wrap seria complicado.
			ThrowError(excepcion_malformato);
			}
		const DWORD cb2D=m_cElementos2D*sizeof(T); //el check anterior garantiza que no hay wrap aca
		m_cbElemento2D=cb2D;
		DWORD it2DUltimoArchivo= ((((DWORDLONG)ibMOST_32BITS)+1))/cb2D -2;		//ojo aritmetica 64bits
		if (it2DMaximoUsuario!=0)
			{
			assert(sizeParam==sizeVariable);
			if (it2DMaximoUsuario<it2DUltimoArchivo)
				it2DUltimoArchivo=it2DMaximoUsuario;
			}
		m_it2DUltimoArchivo=it2DUltimoArchivo;
		}

	const DWORD cbElemento2D=m_cbElemento2D;
	assert(cbElemento2D>0);
	const BOOL fReadOnly = (tipoAcceso == readOnly);
	const BOOL fCrearSiempre = (opcionCreacion == crearSiempre);
	if (fCrearSiempre && ct2DSize==0)
		{
		assert(FALSE); //fCrearSiempre -> ct2DSize tiene que ser mayor a 0.
		//puede suceder en la realidad si el tamanio que pasas es realmente zero (no como valor especial).
		//entonces nos protegemos y hacemos throw, xq zarray no puede funcionar con un array de tamanio zero
		ThrowError(excepcion_malformato);
		}
	assert(!fCrearSiempre || ct2DSize>0);  //fCrearSiempre -> ct2DSize tiene que ser mayor a 0.
	const BOOL fAccesoSequencial = (optimizacion == optSecuencial);
	m_fReadOnly=fReadOnly;
	BOOL fSizeConstante=(sizeParam==sizeConstante);
	m_fSizeConstante=fSizeConstante;
	if (!fSizeConstante)
		{
		//nota: si el archivo es compartido y tiene un master, debe mantenerse el size constante.
		if (fReadOnly || cPartes>1 || pzMaster!=NULL)
			ThrowError(excepcion_malformato);
		}
	assert(!fReadOnly || !fCrearSiempre); // fReadOnly -> !fCrearSiempre
	WCHAR szFile[_MAX_PATH];
	if (pzMaster!=NULL)
		{
		assert(fSizeConstante);
		assert(fCompartido);
		if (m_fReadOnly || !pzMaster->m_fReadOnly)
			{
			assert(pzMaster->m_fSizeConstante);
			m_pzMaster=pzMaster;
			if (pzMaster->m_pzFlushAntesQueEste!=NULL)
				{
				m_pzFlushAntesQueEste=pzMaster->m_pzFlushAntesQueEste; //lo hereda
				}
			pzMaster->m_cRefCompartido++;
			assert(m_fSizeConstante);
			}
		else
			{
			//probablemente no es lo que querias hacer.
			assert(FALSE);
			ThrowError(excepcion_malformato);
			}
		}
	if (file==NULL)
		{
		if (pzMaster==NULL)
			{
			assert(!fReadOnly && fCrearSiempre);
			GetTempFileZArray(szFile);
			m_fTempFile=TRUE;
			}
		else
			{
			pzMaster->GetFileName(szFile);
			m_fTempFile=FALSE;
			}
		}
	else
		{
#ifdef DEBUG
		if (pzMaster)
			{
			WCHAR szFileBuff[_MAX_PATH];
			pzMaster->GetFileName(szFileBuff);
			assert(wcsicmp(szFileBuff, file)==0);
			}
#endif
		assert(wcslen(file)+1<=countof(szFile));
		wcscpy(szFile, file);
		m_fTempFile=FALSE;
		}


	//NOTA: FILE_ATTRIBUTE_TEMPORARY es muy importante, indica al OS que queremos que mantenga lo mas posible el archivo en memoria.
	//Actualmente siempre se espeficica, porque ningun array se usa luego que cierra principal. Si eso cambia, hay que quitar el flag
	//de temporary al momento de cerrar el handle.
	HANDLE hFile;
	
	if (pzMaster)
		{
		hFile=pzMaster->m_hFile;
		}
	else
		{
		hFile=CreateFileW(szFile,
						GENERIC_READ|(fReadOnly? 0 : GENERIC_WRITE),
						(!fCompartido)?0: (fReadOnly?FILE_SHARE_READ : FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE),
						NULL,
						fCrearSiempre? CREATE_ALWAYS : OPEN_EXISTING,
						FILE_ATTRIBUTE_TEMPORARY | (fAccesoSequencial? FILE_FLAG_SEQUENTIAL_SCAN : FILE_FLAG_RANDOM_ACCESS), NULL);
		}
	if (hFile==INVALID_HANDLE_VALUE)
		ThrowError();
	m_hFile=hFile;
	wcscpy(m_filename, szFile); //se guarda en caso deba ser borrado luego
	DWORD cbSize;
	if (!fCrearSiempre)
		{
		//OJO: ver 'NOTA DE TRIM' en el metodo Trim. El tamanio del archivo no estara bien para el caso de archivos compartidos. Por eso usamos pzMaster->Ct2DSize().
		//si no pasas el pzMaster, entonces no pases ct2DSize zero a menos q sepas que se hizo el Trim en el archivo que estas abriendo.
		DWORD cbHigh=0;
		cbSize=GetFileSize(hFile, &cbHigh);
		assert(cbHigh==0); //solo soporta archivos menores a 4GB
		assert(cbSize>0); //archivo debe tener algo
		assert((cbSize%cbElemento2D) == 0); //tamanio debe ser multiplo de cbElemento2D. esto es obligatorio si al archivo fue creado con esta clase.
		DWORD ct2DSizeCalc=cbSize/cbElemento2D;
		if (ct2DSize == 0)
			{
			if (pzMaster)
				ct2DSize=pzMaster->Ct2DSize();
			else
				ct2DSize=ct2DSizeCalc;
			}
		else if (pzMaster)
			{
			assert(ct2DSize<=pzMaster->Ct2DSize());
			}
		else
			{
			assert(ct2DSize<=ct2DSizeCalc);
			}
		}

	if (ct2DSize-1>m_it2DUltimoArchivo)
		ThrowError(excepcion_archivoMuyGrande);
	cbSize=ct2DSize*cbElemento2D;
	assert(cbSize>0);
	if (cPartes>ct2DSize)
		{
		//no puedes dividir el archivo en mas partes que la cantidad de elementos
		//un caso raro donde podria suceder, es si abres un archivo existente de tamanio zero, y cPartes el cualquier valor (como 1).
		assert(FALSE);
		ThrowError(excepcion_malformato);
		}

	m_iParte=iParte;
	m_cPartes=cPartes;
	DWORD it2DPrimero;
	DWORD it2DUltimo;
	if (cPartes==1)
		{
		it2DPrimero=0;
		if (fSizeConstante)
			it2DUltimo=ct2DSize-1;
		else
			it2DUltimo=m_it2DUltimoArchivo;
		}
	else
		{
		assert(m_fSizeConstante);
		DWORD ct2DPorParte = (ct2DSize/cPartes);
		assert(ct2DSize>=1);
		it2DPrimero=iParte*ct2DPorParte;
		if (iParte==(cPartes-1))
			{
			it2DUltimo=(ct2DSize-1); //toma su parte, que es la sobra.
			}
		else
			{
			it2DUltimo=it2DPrimero+ct2DPorParte-1; //toma una parte entera
			}
		}
	m_it2DPrimero=it2DPrimero;
	m_it2DUltimo=it2DUltimo;

	DWORD const cbGranularity=m_cbGranularity;
	DWORD cbGrow=cbElemento2D; //el grow incial
	if (cbGrow<cbGranularity)
		{
		cbGrow=((cbGranularity/cbGrow)+1)*cbGrow; //agrandarlo
		assert(cbGrow>=cbGranularity);
		}
	m_cbGrow=cbGrow;
	assert(m_cbGrow%m_cbElemento2D == 0);
	m_cbSize=cbSize;
	InitVentana(ct2DVentana);
	if (m_fSizeConstante)
		{
		//el tamanio del array inicializado es el tamanio del archivo.
		//pretendemos que ya accedimos hasta el final, sino luego trucariamos el archivo original en CloseFileInterno,
		//nota: en el caso de compartido, se asume que el array sera llenado por completo por otros zarrays compartidos.
		m_cbAccesed=m_cbSize;
		}
	else
		{
		assert(m_cbAccesed==0);
		}

	if (m_cbSize>CbSizeArchivoMost())
		{
		//archivo es muy grande para zarray.
		//nota que este test se hizo luego de inicializar m_cbAccesed, si no, se podria truncar el archivo al cerrarlo
		assert(FALSE);
		ThrowError();
		}

	DoMapping(it2DPrimero*cbElemento2D);
	return;
	}

template <typename T> void ZArray<T>::UndoMapGrowRemap(const DWORD ib, const DWORD ibLimiteDerecho)
	{
	const DWORD cbSizeOld=m_cbSize;
	assert(cbSizeOld>0);
	assert(ib%m_cbElemento2D==0);
	assert(cbSizeOld%m_cbElemento2D==0);
	BOOL fNeedGrow=(ib>=cbSizeOld);	//como ambos son multiplos de m_cbElemento2D, con este test basta
	DWORD cbElemento2D=m_cbElemento2D;
	assert(fNeedGrow || ib+cbElemento2D-1<=cbSizeOld-1);	//y si no necesita crecer, esta' garantizado que se puede acceder el ultimo byte del elemento con cbSizeOld de tamanio.
#ifdef DEBUG
	if (fNeedGrow)
		{
		assert(!m_fSizeConstante); //esto seria un bug de zarray, ya que verificamos it2D<=m_it2DUltimo antes de llamar esta funcion
		assert(!m_fReadOnly && m_cPartes==1);
		}
#endif
	UndoMapping(!fNeedGrow);
	if (fNeedGrow)
		{
		assert(!m_fSizeConstante);
		assert(m_it2DPrimero==0);
		DWORD cVecesGrow=m_cVecesGrow;
		cVecesGrow++;
		if (cVecesGrow>=2)
			{
			cVecesGrow=0;
			//intentar crecer m_cbGrow
			if (m_cbGrow < (ibMOST_32BITS/16)) //check #1
				{
				m_cbGrow*=2;
				cVecesGrow=0;
				}
			}
		m_cVecesGrow=cVecesGrow;
		const DWORD cbGrow=m_cbGrow;
		assert(cbGrow%cbElemento2D == 0);
		assert(ib%cbElemento2D==0);
		DWORD diff = (ib/cbGrow)+1;
		DWORD cbSizeNuevo=cbGrow*diff;
		const DWORD cbSizeMost=CbSizeArchivoMost();
		
		if (cbSizeNuevo<ib+cbElemento2D || cbSizeNuevo>cbSizeMost) //detectar wrap y mapeo fuera del tamanio del archivo
			cbSizeNuevo=cbSizeMost; //maximo posible.
		assert(cbSizeNuevo>ib);
		if (m_cbSize==cbSizeNuevo)
			{
			//ya estaba en el limite maximo, y no lo podemos crecer mas
			ThrowException(excepcion_archivoMuyGrande);
			}
		m_cbSize=cbSizeNuevo;
		}
	assert(ib+cbElemento2D-1<=m_cbSize-1); //debe entrar un elemento completo en el tamanio actual
	assert(ib+cbElemento2D>ib); //no puede haber wrap, hicimos test de limites antes.

	DoMapping(ib,ibLimiteDerecho);
	assert(ib>=m_ibStartVentana);
	assert(ib+cbElemento2D-1<=m_ibEndVentana);
	}

//operator[]
//
//nota: si el array crece, el contenido del area crecida es arbitrario.
template <typename T> T& ZArray<T>::operator[] (DWORD it2D)
	{
	assert(m_pb!=NULL); //Nunca deberiamos de llegar aqui si el codigo de zarray esta OK.
	assert(m_cbElemento2D>0);
	assert(m_cElementos2D>0);
	assert(m_ibEndVentana<=m_cbSize-1); //si el array necesita crecer, siempre se saldra' de la ventana
	//NOTA: esta funcion esta' optimizada para el caso mas comun, es decir, si se pide acceder un elemento dentro de la ventana.
	//para ese caso, se hara muy pocos calculos.
	BOOL const fMenorIgualQueVentana= (it2D<=m_it2DEndVentana);
	DWORD const ib=it2D*m_cbElemento2D;	//aca puede haber wrap, pero lo detectamos luego
	if (fMenorIgualQueVentana && it2D>=m_it2DInicioSeguro)
		goto AccesoRapido;

	if (it2D<m_it2DPrimero || it2D>m_it2DUltimo)
		{
		//aqui detectamos wrap de ib
		if (it2D>m_it2DUltimo && !m_fSizeConstante)
			ThrowError(excepcion_archivoMuyGrande);
		assert(FALSE);
		ThrowError(excepcion_malformato);
		}
	
	BLOCK
		{
		assert(ib>=it2D);	//no deberia de suceder wrap ya que probamos arriba que it2D este' en un rango seguro
		assert(!(ib>=m_ibStartVentana && fMenorIgualQueVentana)); //como it2D<it2DInicioSeguro, es imposible que ib caiga en esta posicion, lo garantiza m_it2DInicioSeguro
		ZArray<T> const *pzMaster=m_pzMaster;
		if (pzMaster)
			{
			assert(m_fSizeConstante);
			DWORD ibStartMaster=pzMaster->m_ibStartVentana;
			if (ib>=ibStartMaster && ib<=pzMaster->m_ibEndVentana)
				{
				assert(ib+m_cbElemento2D-1<=pzMaster->m_ibEndVentana); //entra el elemento completo
				if (it2D%128==0)
					{
					Posible_Exception;
					}
				return *((T*)(pzMaster->m_pb+ib-ibStartMaster));
				}
			}
		UndoMapGrowRemap(ib);
		}
AccesoRapido:
	assert(ib%m_cbElemento2D==0);
	assert(ib>=it2D);	//no deberia de suceder wrap ya ib esta dentro de la ventana, porlotanto no pudo haber wrap.

	if (!m_fSizeConstante && ib>=m_cbAccesed)
		{
		//actualizarlo. esto sirve al momento de escribir el archivo para Trim.
		m_cbAccesed=ib+m_cbElemento2D; //hay garantia de que no habra wrap ya que cbGrow y cbVentana son multiplos de cbElemento2D
		assert(m_cbAccesed<=m_cbSize); //cbSize crece por multiplos de cbElemento2D asi que esto esta garantizado
		assert(m_cbAccesed<=m_ibEndVentana+1);
		assert(m_cbAccesed>=ib); //osea no hubo wrap
		}
	assert(m_pb);
	assert(ib+m_cbElemento2D-1<=m_ibEndVentana);  //FDoMapping garantiza esto
	assert(ib>=m_ibStartVentana);  //FDoMapping garantiza esto
	if (it2D%128==0)
		{
		Posible_Exception;
		}
	return *((T*)(m_pb+ib-m_ibStartVentana));
	}

template <typename T> void ZArray<T>::CloseFile(ZARRAY_CF cf)
	{
	assert(!m_fDeleteOnClose);
	if (cf==cfDelete)
		m_fDeleteOnClose=TRUE;
	CloseFileInterno(FALSE,cf==cfFlushSiRAMBajo && FBajoDeRAM());
	}

template <typename T> void ZArray<T>::Trim()
	{
	//NOTA DE TRIM: Lamentablemente no siempre podemos hacer Trim en el caso de archivos compartidos,
	//porque windows obliga a cerrar los file mappings antes de hacer trim, pero no queremos hacerlo
	//porque queremos mantener nuestro file mapping original (el caso del zarray 'soluciones').
	//entonces, no le hacemos trim en esos casos, pero luego debemos tener cuidado de no depender del tamanio del archivo para deducir el numero de elementos.
	assert(m_hfm==NULL && m_pb==NULL); //requerimiento de windows
	DWORD cbSizeTrim=m_cbSize;
	BOOL fNeedTrim= !m_fSizeConstante;
	if (m_cbSizeTrim>0)
		{
		assert(!fNeedTrim);
		fNeedTrim=TRUE;
		cbSizeTrim=m_cbSizeTrim;
		}
	if (fNeedTrim && m_cbAccesed!=cbSizeTrim)
		{
		assert(m_cRefCompartido==0);
		assert(m_pzMaster==NULL);
		assert(m_cbAccesed<cbSizeTrim);
		assert(!m_fSizeConstante || m_cbSizeTrim>0);
		//Nota: puede ser que cbSizeTrim-m_cbAccesed>=m_cbGrow, ya que se pudo haber creado el array en OpenFile con un tamanio especifico.
		if (cbSizeTrim-m_cbAccesed<0x80000000)
			{
			LONG cbDiff=(cbSizeTrim-m_cbAccesed);
			SetFilePointer(m_hFile, -cbDiff, NULL, FILE_END);
			}
		else
			{
			assert(m_cbAccesed<0x80000000); //si lo fuera, estariamos en el 'if' de arriba
			SetFilePointer(m_hFile, m_cbAccesed, NULL, FILE_BEGIN);
			}
		SetEndOfFile(m_hFile);
		m_cbSize=m_cbAccesed;
		m_cbSizeTrim=0;
		}
	}

template <typename T> void ZArray<T>::CloseFileInterno(BOOL fForzarDelete, BOOL fFlush)
	{
	UndoMapping(FALSE);
	if (m_hFile!=INVALID_HANDLE_VALUE)
		{
		BOOL fDelete=(m_fTempFile || (fForzarDelete && !m_fReadOnly) || m_fDeleteOnClose);
		if (!fDelete)
			Trim();
		if (fFlush)
			{
			assert(!fDelete); //no tiene sentido hacer flush si se pide borrar el archivo
			FlushLoEscrito();
			}
		if (m_pzMaster==NULL)
			CloseHandle(m_hFile);
		m_hFile=INVALID_HANDLE_VALUE;
		if (fDelete)
			{
			assert(m_filename[0]!=0);
			if (m_filename[0]!=0)
				{
				assert(g_fExcepcionBotada || !fForzarDelete || m_fTempFile || m_fDeleteOnClose); //probablemente olvidaste llamar CloseFile
				BOOL fDeleted=DeleteFileW(m_filename);
				assert(fDeleted || m_fCompartido); //nota: esto puede fallar si el archivo esta compartido por varios zarray. solo el ultimo zarray en morir podra borrar el archivo
				}
			}
		}
	ResetMembers(FALSE);
	}

template <typename T> ZArray<T>::~ZArray()
	{
	CloseFileInterno(TRUE, FALSE);
	assert(m_cRefCompartido==0); //si este es un master, los slaves ya deben haberlo liberado
	}


template <typename T> void ZArray<T>::partition(int &k, int &start, int &end)
	{
	ZArray<T> &v=(*this); 
	k = end;
	int i = start+1;
	T pivot = v[start];
	T vi;
	T vk;
	assert(sizeof(T)>sizeof(BYTE)*2); //esto garantiza que se pueden usar int en vez de unsigned int
	int ibMasterEnd;
	if (v.FTieneMaster())
		ibMasterEnd=v.It2DMasterEndVentana();
	else
		ibMasterEnd=INT_MAX;

	while (k > i)
		{
		while ((vi=v[i]) <= pivot && i <= end && k > i)
			{
			i++;
			if (i>ibMasterEnd && k>i)
				{
				v.IntentaVentana(k-i+1, i);
				ibMasterEnd=v.It2DMasterEndVentana();
				}
			}
		v.SetRetro(TRUE);
		while ( !((vk=v[k]) <= pivot) && k >= start && k >= i)
			k--;
		assert(k>=0);
		v.SetRetro(FALSE);
		if (k > i)
			{
			//este swap es mas eficiente que usar zswap, ya que tenemos a la mano los valores.
			v[i]=vk;
			v[k]=vi;
			}
		}
	assert(k>=0);
	zswap(start, k);
	}


template <typename T> void ZArray<T>::zswap(int iz,int der)
	{
	ZArray<T> &v=(*this); 
	T aux=v[iz];
	T aux2=v[der];
	v[der]=aux;
	v[iz]=aux2;
	}

template <typename T> void ZArray<T>::quicksort(int start, int end)
	{
	ZArray<T> &v=(*this); 
	int cSospechaIguales=0;
Repeat:
	BLOCK //BLOCK posiblemente reduce el uso del stack en la recursion
		{
		//maneja casos triviales, y el caso de sospecha de iguales
		int elementos=end-start+1;
		if (elementos <= 1)
			return;
		v.IntentaVentana(elementos, start); //intenta posicionar la ventana en la posicion optima
		if (elementos==2)
			{
			T tempStart=v[start];
			T tempEnd=v[end];
			if (!(tempStart<=tempEnd))
				{
				//este swap manual es mas eficiente, ya que tenemos los valores de ambos a la mano
				v[end]=tempStart;
				v[start]=tempEnd;
				}
			return;
			}
		if (cSospechaIguales==3)
			{
			int i;
			T test = v[start];
			for (i=start+1;i<=end;i++)
				{
				if (test!=v[i])
					break;
				}
			if (i>end)
				return;
			cSospechaIguales=0;
			}
		}

	int k;
	partition(k, start, end);

	//ahora hacer la recursion
	if (k-1-start>end-(k+1))
		{
		//NOTA (ziggy): aqui se desenvuelve una de las llamadas de quicksort, de tal forma que solo hace recursive
		//en la seccion mas pequenia. Esto reduce el worst case de uso de memoria (de stack) de N^2 a logN.
		//nota que si no se hace esto, el programa hara crash en el worst case si el array es grande.
		int endNuevo=k-1;
		if (k==end)
			{
			//NOTA de optimizacion (ziggy): si el pivot termino' al extremo derecho, significa que hemos
			//recorrido todo el array, y se cumple que v[x]<=v[start] para todo x del rango.
			//Luego hacemos swap de start y end para empezar la siguiente iteracion.
			//Si nuevamente se cumple esta propiedad en la siguiente iteracion, incrementamos el counter de sospecha.
			//A medida que incrementa el counter, las sospecha es mas fuerte de que pueden ser todos iguales.
			//en realidad la sospecha es que estan todos ordenados, excepto el primero q es mayor o igual al ultimo.
			//Esto es indispensable al momento de calcular densidades de la nube de puntos, ya que muchos puntos terminan
			//siendo iguales, y sin este codigo, el worst case (N^2) es muy comun. Con esta optimizacion ese caso baja a NlogN.
			//Nota: para reproducir un worst case muy malo, hacer una nube de densidades de muy pocos puntos (1000 por ejemplo),
			//a partir de una nube de puntos muy grande (10^6 por ejemplo). Asi, se generara muchos puntos duplicados al hacer las densidades.
			//
			//Esta optimizacion tambien se utiliza para otros casos, por ejemplo, si las volatilidades son zero, todos los puntos terminan siendo
			//iguales (e.g [1,1]), entonces el sort para el convex hull tambien necesita esta optimizacion.
			cSospechaIguales++;
			if (cSospechaIguales==3)
				{
				//entonces,  v[endNuevo] <= v[endNuevo+1] <= ... <= v[endNuevo+cSospechaIguales]
				//porlotanto, si el primero es igual al ultimo, deducimos que todos hasta ahora son iguales.
				T c1 = v[endNuevo];
				T c2 = v[endNuevo+cSospechaIguales];
				if (c1!=c2)
					{
					//si no es igual, entonces reseteamos la sospecha, simplemente estaban ordenados, pero no eran iguales.
					cSospechaIguales=0;
					}
				}
			}
		else
			{
			cSospechaIguales=0;
			quicksort(k + 1, end);
			}
		end=endNuevo;
		goto Repeat;
		}
	else
		{
		cSospechaIguales=0;
		quicksort(start, k - 1);
		start=k+1;
		goto Repeat;
		}
	}


template <typename T> void ZArray<T>::zsort(int first/*=0*/,int last /*=-1*/, BOOL fSoloVerifica /*=FALSE*/, BOOL fNOVerifica /*=FALSE*/)
	{
	assert(CElementos2D()==1); //solo funciona para este caso.
	if (last==-1)
		{
		assert(first==0);
		last=Ct2DSize()-1;
		}
	if (!fSoloVerifica)
		quicksort(first,last);
	if (fNOVerifica)
		return;
#ifndef COMPILACION_PARA_DISTRIBUCION_EXTERNA
	int i;
	ZArray<T> &v=(*this); 
	for (i=first; i<last; i++)
		{
		T temp=v[i+1];
		assert(v[i]<=temp);
		}
#endif
	}

DWORD CbRamDisponible()
	{
	DWORD cbRet=1;
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof (ms);
	if (GlobalMemoryStatusEx(&ms))
		{
		DWORDLONG avail=ms.ullAvailPhys;
		if (avail>ibMOST_32BITS)
			cbRet=ibMOST_32BITS;
		else
			cbRet=(DWORD)avail;
		}
	return cbRet;
	}

