//comun.h
//
#pragma once
#pragma warning (disable:4996)
#include <stdlib.h>
#include "versioncomun.h"
#include <emmintrin.h>

#define COMUN_H_INCLUIDO
#ifndef NDEBUG
#define DEBUG
#endif

//COMPILACION_PARA_DISTRIBUCION_EXTERNA
//
//se usa para controlar #defines que controlan si se incluye cierto codigo o no. En las
//versiones que distribuimos externamente, por seguridad no se incluye cierto codigo
#define COMPILACION_PARA_DISTRIBUCION_EXTERNA //definir esto cuando se haga un build para distribuir externamente


//USAR_BIGFLOAT
//
//Permite usar la clase MAPM para hacer calculos de alta precision.
//MAPM se usa como si fuera un double.
//en util, el codigo define la precision global que usara' MAPM (llama a m_apm_cpp_precision).
//Mientras mas precision, mas lento sera el codigo que usa MAPM.
//

//#define USAR_BIGFLOAT	//normalmente esta linea debe estar comentada. Se usar para hacer pruebas solamente.
#ifdef USAR_BIGFLOAT
	//aca se puede prender bigfloat para partes del codigo
	//#define USAR_BIGFLOAT_MONTECARLO
	//#define USAR_BIGFLOAT_ESTADISTICOS
	//#define USAR_BIGFLOAT_RANDOMS
	//#define USAR_BIGFLOAT_RANDOMS_GENERATOR	//OJO!! cambia el generador random por el de bigfloat
#ifdef COMPILACION_PARA_DISTRIBUCION_EXTERNA
	#error Esto no debe suceder
#endif
#define PRECISION_BIGFLOAT 100 //digitos de precision para bigfloat (digitos en base 10 de la parte float, sin contar signo ni exponente). el double tiene 15.
#endif


//#define HAYS_ZERO //define esto para recibir los mismos numeros que el hays.old0
#ifdef HAYS_ZERO
#define TRACE_CALCS_MONTECARLO
#define USAR_METODO_ANTIGUO_CARTERAS //usar el metodo antiguo o el optimizado (SSE2) para sumar las carteras
//#define ANTIGUO_CALCULACARTERAS esto no deberia ser necesario ya que no cambia los calculos, solo q no hace recursivo cCalculaCarteras
#else //Compilacion normal
#define OPTIMIZAR_ALIGN16_SSE2 //optimizaciones a mano de instrucciones SSE2
#define NUEVO_PROM_PARCIAL
#endif //END Compilacion normal

#define eps 1e-7
#define fo(i,j) for(int i=0;i<j;i++)
#define fo1(i,n) for(int (i)=1;(i)<(n);(i)++)
#define all(v) (v).begin(),(v).end()
#define rall(v) (v).rbegin(), (v).rend()

#define VECES_MAXIMO 255 //con este limite, entra en 8 bits. Si se incrementa a 16 bits, debe cambiarse mas codigo.
typedef BYTE PASO_INDICE; //indice de pasos al iterar el rango de valores de un instrumento. Nota que VECES_MAXIMO < 2^sizeof(PASO_INDICE)
#define DIR_MONTECARLODATA1 L"AGORISKV" VERSION_APP
#define DIR_MONTECARLOLIC L"LIC"

#pragma warning (disable:4996) //REVIEW WALTER: warnings de fscanf etc
#ifndef countof
#define countof(arr) sizeof(arr)/sizeof(arr[0])
#endif
#ifndef BLOCK
#define BLOCK	//para usar bloques de codigo sin nombre
#endif

#ifndef COMPILACION_PARA_DISTRIBUCION_EXTERNA
	#ifndef NO_EXTRA_STATUS
		#define EXTRA_STATUS //extra statuses en ventana principal (debug y release) para saber q va sucediendo en el codigo	
	#endif
#endif

#ifndef NODEFINIR_MUCHAS_EXCEPCIONES //esto sirve para usar codigo comun fuera de hays (unit tests)
#define MUCHAS_EXCEPCIONES	//siempre dejarlo en debug por lo menos.
#endif

#ifndef Posible_Exception
	#ifdef MUCHAS_EXCEPCIONES
		#define Posible_Exception (g_progreso? g_progreso->SetProgreso() : NULL)
	#else
		#define Posible_Exception //nada
		#ifdef DEBUG
			#pragma message("OJO: Posible_Exception apagado en DEBUG. Por que?")
		#endif //DEBUG
	#endif //MUCHAS_EXCEPCIONES
#endif //Posible_Exception

class CProgreso
	{
public:
	CProgreso() {m_cProgresoTotal=m_cProgresoActual=0;}
	virtual void SetEtapa(char *str, unsigned int progreso=0, BOOL fNoExcepcion=FALSE) =0;		//linea 1. pasar progreso==-1 para no afectar el progreso de la etapa anterior.
	virtual void TerminaEtapa() =0; //se llama cuando termino' una etapa con progreso de %
	virtual void AppendEtapa(char *str) =0;		//agrega texto a la etapa actual, sin \n
	virtual void SetProgreso(char *str, BOOL fNoExcepcion=FALSE)=0;	//linea 2
	virtual void IncProgreso(unsigned int cprogreso, BOOL fNoExcepcion=FALSE)=0;	//linea 2
	virtual void SetProgreso()=0;			//solo avisa que esta' vivo. sirve para hacer una exception en puntos sin texto de progreso.
	virtual void NotificaInicioFlush(LONG cFlush)=0;
	virtual void NotificaFinFlush(LONG cFlush)=0;
#ifdef EXTRA_STATUS
	virtual void SetTextoDebug(char *str)=0;
#endif
	int CProgresoTotal() {return m_cProgresoTotal;}
protected:
	unsigned int m_cProgresoTotal;
	unsigned int m_cProgresoActual;
	};
