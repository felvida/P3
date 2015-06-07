/*  Web Server for client junio 2015 */
// Antes de version final > ARREGLAR DEPURA OJO
// if (num <10) Red('0'); 
// Red( num ); // '10' '08'

#include <EEPROM.h> 		/* para grabar/leer codigo pin y TAG de eeprom */
#include <SPI.h>
#include <Ethernet.h>
#define CASA 120       /* puerta A..I, 101..109, portal 100 */
#define VERSION " V3"   /* __FILE__ ->c:\docume\user\config\temp\build03456789.tmp\p2.cpp*/
// para compilar tienes dos opciones, para definir NUMCLAVES,NUMTRYACCES,NUMALARM : 1 para portal y 2 para casas
#define NUMCLAVES 52  /* Opc.1:20,20,20 para casa 20 =[(1soloPIN+ PINyTAG)*4+5manto+2PINseguridad] 20*11+20*9+20*6=520 */
   /*reg.ocupa 11 bytes   Opc.2:58,2,2 para portal 45=[ 2soloPIN+ TAG*4*9+5manto], 50*11 +3 *9+ 5 *6= 580 bytes */
#define NUMTRYACCES 3   /* 20 registro de tag y pin y su seg.cada uno ocupa 8 bytes */
#define NUMALARM  5     /* 20 registro alarmas, el reg.ocupa 6 bytes OJO eeprom de 1024 bytes */
#define NUMANALOG  5   /*numero de entradas analogicas usadas*/
#define INIAL 1         /* entrada inicial alarmas inc. de 1 a 3 solo contacto puerta y intrusion caja */
#define FINAL 3         /* final port de alarmas exc. */
#define STX 2     // Start Transmision 2 normal, 'p' para debug desde USB 
#define PORTRELE 2     /* salida que se activa 1 seg. al dar PIN y/o TAG correcto, conecta al RELE de puerta*/
#define DEBOUNCE 20    /* Retardo en miliseg, para evitar rebotes */
#define DELAY 1000     /* ton/toff miliseg. para salida RELE */
#define TOPEWEB 65     /* maxima respuesta del browser en caracteres ESTA MUY AJUSTADA*/
#define LONGPIN 5      /* long. de la palabra PIN 4 numeros */ 
#define LONGTAG 5      /* long. de la clave TAG (tarjeta RFID ) 5  bytes */
#define DELAYCLIENT 1  /* miliseg. retardo cliente ethernet */
#define LONGREG (LONGPIN+LONGTAG+1)  /*10 size of one element of table */

byte mac[] = { 0x00, 0xAD, 0xBE, 0xEF, CASA, CASA }; /*mac de tarjeta*/
//IPAddress ip(192, 168, 1, 177); // The IP address will be dependent on your local network:
IPAddress ip(192, 168, 0, 177); // The IP address will be dependent on your local network:

#define Serie Serial.print /*cuando se acabe depurar cambia funcion por null*/
#define Serieln Serial.println  /*cuando se acabe depurar cambia funcion por null*/ 
#define saca(frase) client.print(frase) 
#define sacaH(frase) client.print(frase,HEX) 

#define sacaLF(frase)  client.println(frase); Serial.println(frase)
#define trazaS(var) Serie(#var"=");Serie(var) // trazaS(vtip) -> Serial.print("vtip""=") 
#define diDepura(frase) Serie(frase) // Didepura("init.") -> Serial.print("init.") 
//#define DEBUG         // L.4: Comentar para versiones definitivas
#ifdef DEBUG
  #define testigo Serie("X lin."); Serieln( __LINE__-33); 
// el uso de : testigo -> +52 bytes, TrazaS ->+38 bytes en prog. DEBUG
#else
  #define testigo
#endif  
/* Objeto String ->long .length() , agrega .append("c") ,daOffset .indexOf("pi")  ,extrae .CharAt(n)  */
/* -------------------------------------- VAR */
const String tiString = "ti";   	/* OBJETO string, me roba memoria si lo pongo directo en indexOf()   */
const String alString = "al";   	/* OBJETO string  */
const String tagString = "tag";   	/* OBJETO string  */
const String pinString = "pin";   	/* OBJETO string  */
String readString = String( TOPEWEB);   	/* OBJETO string 65 char. for fetching data from browser */

const short offsetTecla [4] = {5,3,6,4}; 
char teclas[LONGPIN+1];       /* buffer (NO ASCII) de las teclas pulsadas(9->09h),+1 byte para desborde se graba una mas */
//char nada ='\0';              //
int pillo=0;                  //* cuenta numero de HTML sacadas por ETH
short int nteclas;            /* numero de teclas que hay en buffer */
byte teclaactual;            /* tecla mantenida actual  */
//byte teclaantes;            // tecla mantenida actual y ultima tecla pulsada de controlteclado()
short int Tprevious; 	      /* tecla leida previa de teclado() */
short int LastConfig; 	      /* te dice si =0, sin configurar, index. de la ult.configuracion en tabla */
long TlastKey =0; 		/* t en miliseg, ult.lectura buena de teclado */

struct table {char PinOK[LONGPIN]; byte  TagOK[LONGTAG]; char tip; } tabla[NUMCLAVES]; /*11 bytes x reg*/
// tabla[1].PinOk  tabla[1].TagOK y tabla[1].tip  
struct table2 {byte  Tag[LONGTAG];  long sTag; } acceso[NUMTRYACCES]; /*5+4 bytes x reg*/
// acceso[0].Tag  acceso[0].sTag
struct table3 {short int  puerto; short int valor; long sTime; } alarma[NUMALARM]; /* 2+ 4 bytes x reg*/
// alarma[1].puerto alarma[1].valor alarma[1].sTime
long segintruso=0;
short int numTag=0, numAlarm=0;  /*indicadores de por donde va el buffer circular*/
short int vindex=0;              /* numero de configuraciones de entrada, var. general aplicar a tabla */ 
short vueltamillis=0;           /* vuelta de contador millis(), para evitar desborde */
short iPin,iTag;   		/* indice de teclado se busco en tabla (indice del =) Pin y Tag ,-1 no esta */
long TcambioRele =0; 		/* t en miliseg, ult.cambio Rele */
long segini=0 ;      		/* total seg. pe.245678 desde ON arduino, se actualiza desde gettime() leido del browser */
long tvuelta=0;                 /* t en miliseg, de la ultima vuelta, sirve para detectar vuelta a 0 del contador */
/*                        a0    a1    a2  a3   a4  a5*/
const short int anaSup[]=  {  200,  255,  110, 190, 200,200};   /*110 PORTAL,120 CASA tope sup estado entradas analogicas, si lo supera alarma*/  
const  short int anaInf[]=  {  100,   10,   0,   50, 100,100};   /*tope inf estado entradas analogicas*/  
const byte VerAlarm[] =    {   0,    1,   2,    3,   0, 0 };   /*ojo en 0 esta PIR y HUMOS, 1 crack,2 door */
const byte SendAlarm[] =   {   0,    1,   2,    0,   0, 0 };   /*ojo en 0 esta PIR y HUMOS, k>anaInf[i]->open ,<>1 */
 byte EstadoAlarm[6] ;//= {   0,    0,   0,    0,   0, 0 };   /*ojo en 0 esta PIR y HUMOS, k>anaInf[i]->open ,<>1 */

const char* AlarmString[] = {"paso","crak","door","humo" }; /*version P3 mas completa*/
const char* DiStrin[] = {"Open","Close"} ; 

EthernetServer server(80);   			/* OBJETO server, port 80 */
//Client client = server.available();  	// creo OBJETO cliente e inicializo client para IDE 166 no puede ir aqui
byte code[LONGTAG+1]; 	                /* buffer con Rfid Leido inicializado,+1 byte mas para CRC */
byte checksum = 0;                     /* el checksum que calculo para poder comparar con code[5] ,13*3600+51*60= */   
long timetag ;                          /* time de lectura tag*/
int diai=0,mesi=0,anoi=0,dia_1_1_10i=0;	/* dia/mes/ano(inicio),de la puesta en Hora del browser,y calcula los dias desde 1/1/2010  */
int hori=0,mini=0,segi=0;  		/* hora min.13 51 00,         "   ,segini= 115 cuando se actualizo por browser [x millis()>>6 ] */
int vtip=32;                            /*  tipo de actualizacion 32 es ' '*/
const short meses[]={31,28,31,30,31,30,31,31,30,31,30,31}; /*dias de los meses del ano */
long seg=0, segvueltaantes=0;             /*segundos desde arranque, seg. vuelta anterior bucle loop */
short sucesoOld=0; /*suceso para alarmas */




void dopin()           /*pin=12345&tag=1a2b3c4d5e&al=123&d=124&t=1  actualiza PIN , segun vindex*/
{    short i,j=0; char car;  	     /*vindex es el index de tabla*/
     short n= readString.indexOf(pinString)+ 4; /*dindexOF() devuelve posicion de "p", n donde empieza el valor */
        for (i=n , j=0; i<n+LONGPIN ; i++){            /* [4..8]: 1234 & OJO pin con los 4 digitos->0000 */
		car= readString.charAt(i)-'0';       /*Serie(car); debug*/
         tabla[vindex].PinOK[j]= car;          /* grabar(0..4):12345*/      
         graba( vindex*LONGREG + j, car );       /* graba en zona eeprom */
         j++;
       } /* for PIN*/
} /*dopin() */         

void dotag() /*actualizar un tag (tabla+eprom) en tagstring '0F0A1A1F0A' , pos.segun vindex */
{  short tempbyte=0, j, bytesread=0; byte val, valA; 
    short  i= readString.indexOf( tagString) +4;  //tag=
     while (bytesread < 10)                         /* [0..9] read 10 digit code + 2 digit checksum */
       {   valA = readString.charAt(i++) ;    // 'a'
           val= ord(valA);      //10
           if (bytesread & 1 == 1)                                  /* 1,3,5,7,9 ->hacemos e 2 digito hex.  */
              { j= (bytesread >> 1);                                /* j= para 0,1=0  2,3:1 4,5:2 6,7:3 8,9:4  */
                tabla[vindex].TagOK[j] = (val | (tempbyte << 4));      /* desplaza viejo y sumas nuevo digito hex. */
                graba( vindex*LONGREG + LONGPIN + j, tabla[vindex].TagOK[j] );       /* graba en zona eeprom */
              } else    tempbyte = val;                              /* 0,2,4,6,8 Store the first hex digit first */
            bytesread++;                                   /*  next digit */
     } /* while ( bytesread */
} /*dotag()*/

void LeeRfid()
{ byte val; short tempbyte=0, bytesread=0;
     while (bytesread < 12) {             /* 2A002A6256XX [0..11] read 10 digit code + 2 digit checksum  */
       if( Serial.available() > 0) 
       {   val = Serial.read();    Serial.write(val);
           if((val == 0x0D)||(val == 0x0A)||(val == 0x03)||(val == STX )||(val == 's' ) ) /* if header or stop bytes before  */
               break ;   // sal del bucle           LF,RC,ETX ( o STX otra vez)   al leer 10 digitos acaba bucle */
           if ((val >= '0') && (val <= '9'))  val = val - '0';            /* Do Ascii/Hex conversion: */               
             else if ((val >= 'A') && (val <= 'F')) val = 10 + val - 'A';  /* Each 2 hex-digits, add byte to code: */        
           if (bytesread & 1 == 1)                                  /* 1,3,5,7,9 make some space for this hex-digit by */
              { code[bytesread >> 1] = (val | (tempbyte << 4));     /* 0,1:0 2,3:1 4,5:2 6,7:3 8,9:4  shif prev.hex-digit ,4 bits to left */
                if (bytesread >> 1 != 5)                                  /* If we are at the checksum byte, */
                     {  checksum ^= code[bytesread >> 1]; };             /* Calculate the checksum (XOR) */
              } else    tempbyte = val;                              /* 0,2,4,6,8 Store the first hex digit first */
            bytesread++;                                   /* ready to read next digit */
       } /* if (Serial.available */
     } /* while ( bytesread< */
} //LeeRfid

void TeclaUSB()
{ byte val; int i=0;        // p*p1p2p3p4p5p#  p*p1p2p3p# OJO! LIMITACION simulacion teclado * USB nº distintos
     while ( !Serial.available() ) ; // bucle hacer nada, mientras .avaliable()==0 >no llegue sig. car. por Serie 
     val = Serial.read();    Serial.write(val);// DEPURA
     if ((val >= '0') && (val <= '9'))  val = val - '0';            /* Do Ascii/Hex conversion: */               
     if (val == '*') val= 11 ;
     if (val == '#') val= 13 ;  /* Each 2 hex-digits, add byte to code: */        
     teclaactual =val;
}
void trataSerie() // conectado  ID-12 a patilla 0,1 llegan los datos de las lecturas  RFID
{ byte val; int i=0;
  if (Serial.available() > 0)  //  Datos por puerto serie?
  { val = Serial.read();
//  Serial.write( val); // MONITORIZAR DATOS SERIE
   if (val == 't' )                 // conectado Arduino+Shield Eth por USB, simula entrada teclado
         TeclaUSB() ;     
   if (( val == STX )||(val == 's' )) {                // check for header,'s' running conectado Arduino+Shield Eth por USB
          LeeRfid(); // se guarda en code[]           // simulas lectura rfid, enviando por monitor serie s0123456789s <-|  
          iTag= estaTag();
          timetag= seg;
          for (i=0; i<LONGTAG; i++)   acceso[numTag].Tag[i]= code[i];
          acceso[numTag++].sTag= seg; /*guardo en su tabla registro accesos */
          if (numTag >=NUMTRYACCES) numTag=0; /*desborde   , next line si  iTag >=0 -> found*/ 
      if (  iTag >=0 ) /* found */
      {  // Serie(" found,tip="); Serie( tabla[iTag].tip ); //DEPURA
        if ( ( tabla[iTag].tip=='2' )||( tabla[iTag].tip=='b' ) )
            Pulso( PORTRELE); /* esta en tabla y es tipo solo con TAG abrir*/
        if (tabla[iTag].tip=='b') { 
                                    BorraTag(iTag); iTag=-1;} // tabla[iTag].TagOK[0]=' ';
      };    /* si es tipo 3: pin +tag , hemos actualizado iTag */
    } /* if available*/
  } // STX 
}

short testCol(short col,short tecla1) // test si hay tecla pulsada activo fila, y miro si columna esta pulsada
{ short tecla= 40,i; // parametros col:pin a activar , tecla1: grupo salida 1,2,3>0, 4,5,6>3, 7,8,9>6 #0* >9
  i= offsetTecla[col];  // {5,3,6,4}; 
  digitalWrite(i, LOW); /*activo  pin de la fial que toca*/
  if ( !digitalRead(8) ) tecla=1 ; /*leo p8, si-> 1 */
  if ( !digitalRead(7) ) tecla=2 ; /*leo p7, si-> 2 */ 
  if ( !digitalRead(9) ) tecla=3 ; /*leo P9 ,si-> 3 */
  digitalWrite(i, HIGH); /*desactivo  */
  i= tecla1+ tecla;
  if (i==11) i =0;  // leo pines 879 los Out son 5364 
  return i; // si no hay tecla pulsada tecla devuelve 20+tecla1
}

short int teclado()  /* activa 1 salida, y lee entrada, detecta tecla pulsada  */
/* esta rutina ,rea liza un muestreo cada Debounce miliseg, y no devuelve un valor numerico : 
   49->ninguna pulsada, 52->hay cambio,espero que se estabilice, 53->esperando t. para muestreo
   o nos da el valor de la tecla pulsada de 0..9, 10->*,12-># 
   usa var .generales  tprevius es tecla memorizada de muestreo anterior (pe 10 miliseg antes   )
                       TLastkey es el instante ultima lectura de tecla */
// 2 muestras con 49, 2 con 5, y 2 con 49 > tecla='5'
// return 49,53...53 49,53...53      52,53..53  5,53....53 5,53.....53 5,53.....53 52,53....53 49,53....53 
// Tprev  49         49              '5'        '5'       '5'         '5'          49          49
{ short i, tcurrent=41 ; //tecla que leo ahora por defecto =41; 
if ( millis() < TlastKey)  TlastKey = millis();  // dada la vuelta,tarda 51 dias, pero si ha dado la vuelta
  if ( millis() < (TlastKey + DEBOUNCE)  )  return 53; /* timing not enough time has passed to debounce */
  TlastKey = millis();  /* ok we have waited DEBOUNCE milisec,reset the timer  */
  for(i=0;((i<4)&&(tcurrent>40));i++){ // i=0123       tcurrent= testCol(5,1);  // 123
    tcurrent= testCol( i, 3*i   );  //  tcurrent= testCol(3,4);  // 456
  }   // apenas detecte una tecla me salgo           tcurrent= testCol(6,7);  // 789
  //                                             //  tcurrent= testCol(4,10); // *0# 10 0 12//  
  if ( tcurrent == Tprevious) return tcurrent; /* tras retardo y 2 lecturas seguidas = */
  Tprevious = tcurrent; return 52 ; /* Tprevia tiene la tecla presionada ó 49 ninguna pulsada */ 
} /*teclado*/
// si han pulsado y la muestra anterior era ninguna tecla pulsada, da true. Mantenerla pulsada da false
short int kbhit()  // teclaactual, se actualiza  te dice tienes una pulsacion lista >  han pulsado y antes no habia pulsada ninguna
{int t;
  t= teclado(); // hay una tecla mantenida pulsada  , se ignoran 53, 52 y 49
  if ((t<40) && (Tprevious>48)) {teclaactual= t; return 1;} // y la tecla previa era que ninguna pulsada(49), evitar rafaga de '5' 
  return 0 ; //  
//  { if (teclaactual!=teclaantes) /*pe. marcar dos 5, tienes que pulsar 5, soltar, pulsar 5 y soltar >5,49,5,49*/
// if (teclaactual<53) teclaantes= teclaactual; /* no actualice teclaantes si estas en t.antirebote */
}

void bufferTeclado() /* procesa eventos de teclado si han pulsado una tecla (esta en teclaactual)
la almacena en buffer teclado> teclas[], graba en buffer circular de accesos
lo busca en tabla, si le corresponde se manda pulso a rele
no necesitas buscar varios pin, el  de un solo uso delante (index mas bajo) que el permanente */
{ short i;
  if ( kbhit() ) /* tecla presionada */
     { if (teclaactual==10) {nteclas=0; return;} /* es * CLR borra buffer  */
       if (teclaactual==12) // es # en INTRO        hay pines de 3,4 y 5 numeros nteclas=2,3 ó 5
        { for ( i=nteclas; i<LONGPIN; i++)   teclas[i]= 10+nteclas;  // almaceno resto de teclas con valores 10..19
          iPin= estaPin(); /*tip=0, 1,2,3, 4,5,6*/
          for ( i=0; i<LONGPIN; i++)   acceso[numTag].Tag[i]= teclas[i];  // almaceno en tabla accesos
          if (LONGPIN!=LONGTAG) acceso[numTag].Tag[i]= 0;     /*si  PIN 4 y  TAG 5-> acceso.Tag[4]= 0*/
          acceso[numTag++].sTag= seg;                       /*guardo en su tabla*/
          if (numTag >=NUMTRYACCES) numTag=0;             /*desborde   , next line si  iTag >=0 -> found*/ 
          if (  iPin >=0 )                       // si la encontro
            switch (tabla[iPin].tip)             /*'2' solo tag  se procesa en RFID(), actualiza iTag */
            { case '1': Pulso( PORTRELE); break; /*accedes con PIN SOLO, infinitas  veces*/
              case 'a': Pulso( PORTRELE);        // accedes con PIN SOLO, una sola vez
                          tabla[iPin].PinOK[0]='a'; break; /*ya entro una vez, pon valor imposible en RAM, no lo podra encontrar */                          
              case '3': if ( iPin==iTag)  Pulso( PORTRELE); iTag=-1; break;  //'3' TAG+PIN eterno, itag -1> TO OPEN pasa tarjeta otra vez
              case 'c': if  ( iPin==iTag )  Pulso( PORTRELE); /* 'c' solo TAG+PIN un solo uso*/                         
                            BorraTag(iTag); tabla[iPin].PinOK[0]='c'; iTag=-1;    break; // borralo de ram, 			                  
            } /*swicth*/
          nteclas=0;         /* al dar almohadilla , es como enter*/
        } else 
          teclas[nteclas++] =teclaactual; /* <>11 mete en buffer */
   }
 if (nteclas>LONGPIN) nteclas=0;  // si pulsan 6 teclas es buffer circular
} /* bufferTeclado()*/

void controlalarmas()  //se invoca cada 2 seg. lee in.analogicas entre INIAL y FINAL,
// debug> muestra su valor, segun veralarm[]:1> anainf[],2 o 3 <[anasup[]]
// graba en tabla alarma[].puerto .valor .stime
{short i,k, cerrado;
    for ( i = INIAL; i <= FINAL; i++) {          // saca solo entradas analogicos ACTIVAS con sensores             
              k= LeeAnalog( i);
              Serie(i);  Serie( AlarmString[i] );    Serie('='); // saca por puerto serie   1door=
              if ( k!= EstadoAlarm[i]) // solo si hubo cambio de lectura
               {  cerrado= DimeAlarma( i,k); // m 0 1  
                  if (cerrado) {    // DEPURAR x USB. tipo 2 o 3 es la puerta que coincide con puerta en pose 2 , pongo LF
                              alarma[numAlarm].puerto= i;
                              alarma[numAlarm].valor= k;
                              alarma[numAlarm].sTime= seg ;                                       
                              numAlarm++ ; Serie('+');
                              if (numAlarm >=NUMALARM) numAlarm=0;  // buffer ciclico, indice a 0
                              } 
                  else  Serie(' ');
                }                     // si changed> ' ' o si grabo '+'
              Serie( k ); Serie(' '); // saca por puerto serie  '34 '
              EstadoAlarm[i]= k; // actualiza estado        
    }; //for	 OUT serial> 1door=34 (sin cambios),1door= 34(cambio pero ABIERTO ), 1door=+34 (cambio pero CERRADO y grabo), 
} /* controlalarmas() */

void gettime() /*actualiza reloj en funcion de hora recibida por browser 4(TIME)15:35 00524 */
{    // d:\arduinoPRG\time\time\examples\timeNTP sinc por server UDP
        char mistr[]={0,0,0,0,0}; int i,s; short j,k,n;  	/*para recoger datos browser y pasar a val.numerico*/
        anoi=0; mesi=0; diai=0;hori=0;mini=0;segi=0;
        n= readString.indexOf(tagString) +4;    /* TAG="20101231" & OJO relleno con 5 digitos->2010/12/31 */
        mistr[0]=readString.charAt(n++); 
        mistr[1]=readString.charAt(n++); 
        mistr[2]=readString.charAt(n++); 
        mistr[3]=readString.charAt(n++);                
        i= 1000*(mistr[0]-'0')+ 100*(mistr[1]-'0')+10*(mistr[2]-'0')+ (mistr[3]-'0'); //2014
        mistr[0]=readString.charAt(n++);
        mistr[1]=readString.charAt(n++); mistr[2]= 0;    
        j= 10*(mistr[0]-'0')+ (mistr[1]-'0');      //12
        mistr[0]=readString.charAt(n++);
        mistr[1]=readString.charAt(n++);                 
        k= 10*(mistr[0]-'0')+ (mistr[1]-'0');    // 31
      	  if ( (i>=2010) && (i<2099) && (j>=0) && (j<13) && (k>=0) && (k<32) 	)		
              { vueltamillis=0;  /*al poner en fecha cuenta vueltas=0*/
                anoi= i;        /* actualiza el ano*/
      	        mesi = j--;   	/* actualiza el mes */
      	        diai = k;       /* actualiza el dia */
		short bisiesto=0; if ((anoi-2004) %4 ==0 ) bisiesto=1; /*2004,2010,2012,2016,...*/
		for (n=0,s=0 ;n<j ;n++) { s= s+meses[n]; /*suma dias de los meses pasados */
                                          if (n==1) s= s+ bisiesto; } /* feb de ano Bisiesto */
                short d_bisi=(i-2008)/4- bisiesto; /* dias a sumar por anos bisiestos */ 
                dia_1_1_10i= 365*(i-2010)+d_bisi+s+k -1; }                   
        n= readString.indexOf(pinString);  
        mistr[0]= readString.charAt(n+4);       /* hora: 1434 ,14h OJO pin siempre 4 digitos->0000 */
        mistr[1]= readString.charAt(n+5);                
        i= 10*(mistr[0]-'0')+ (mistr[1]-'0');   // 23
        mistr[0]= readString.charAt(n+6);       /* minuto: 1434, 34 min. */
        mistr[1]= readString.charAt(n+7);                 
        j= 10*(mistr[0]-'0')+ (mistr[1]-'0');   // 59
       	  if ( (i>=0)&& (i<24) &&(j>=0) && (j<60) )	
		{ hori = i;       /* actualiza hora */
 		  mini = j;}      /* actualiza minuto */
          segini = seg;	/*total seg. pe. 234567 */
 } /*gettime()*/
 


void ProcesaRespuesta()     /* tip define accion, campo pin y tag con param.
TIP:0(borra 1),1a(solo pin),2b(solo TAG),3c(pin+TAG),4(TIME)1535 20101231,5 cambia Pin y+1,z borra todo*/ 
{ short val; short ni= readString.indexOf(tiString);  /* de respuesta del browser actualiza el pin(4)1234 y el tag(5)ABCDE */    
  if ( ni>=0 )           /*campo tipo exista  si <0 ->NADA */
    { vtip= readString.charAt(ni+3);   /*el tipo recibido del browser, puedes avanzar 1 despues viene espacio HTTP*/
      if (vtip=='4') gettime(); 
      if (vtip=='Z')  BorraTodo(); /*LastConfig=0; esta incluido en borratodo */ 
      if (vtip=='7') Pulso(PORTRELE);  // abrir cerradura                
      if ((vtip >='0' && vtip <'4' ) ||  (vtip >='a' && vtip <'d' ) ) /*0..3 y a..c */
       {  ni= readString.indexOf(alString)+3;  vindex= readString.charAt(ni++);// A..Z,[ %5B, \%5C ]%5D ^%5E ,_, `%60 , a..z
          if (vindex=='%') { vindex= readString.charAt(ni++);   val= ord(vindex); // 5
                             vindex= readString.charAt(ni);     vindex=16*val+ord(vindex); } //B %5c ->92 : 27
          vindex= vindex-'A' ; 
            if ( (vindex>=0) && (vindex<NUMCLAVES) )     {    /*vindex 0..3, lastconfig 1..4*/
                   tabla[vindex].tip= vtip;                    /* actualiza tipo de dato a tratar en tabla */         
                   graba( vindex*LONGREG +LONGPIN+LONGTAG, vtip);  /* actualiza EEPROM tipo de dato vindex*LONGREG+ LONGPIN + j,*/
                   if  (vindex>=LastConfig) LastConfig= vindex+1; } /* incrementa LastConfig para vtip bueno */ 
         switch (vtip)  {
         case '0':  tabla[vindex].PinOK[0]='x'; BorraTag(vindex); 
//                   tabla[vindex].TagOK[0]=' '; /*este ya no lo encuentra mas*/
                  if (vindex==LastConfig-1) LastConfig--; /* 0 borrado segun campo leido en string del browser */  
                  break;                                /* vindex 0..2, lastconfig 1..3 decrementa LastConfig por borrado vtip = 0 */
         case 'a': dopin(); break; 
         case '1': dopin(); break;            /*1 actualiza pin segun campo leido vindex */
         case 'b': dotag(); break; 
         case '2': dotag(); break;            /*2 actualiza TAG segun campo vindex  */
         case 'c': dopin(); dotag(); break;
         case '3': dopin(); dotag(); break;   /*3 actualiza PIN+TAG segun campo vindex */
         default :  ;  }  /*switch*/
      } /*if vtip*/
   } /*if n>=0*/
} /*ProcesaRespuesta()*/



 
void BorraTag(short k0) // limpia pose.(k0) de la tabla tagOK
{short i;
 for (i=0;i<LONGTAG;i++) ; tabla[k0].TagOK[i]=0;
}
void BorraTodo() /*borrado total eprom y tabla*/
{ short k; 
   for ( k=0  ; k<NUMCLAVES ;k++ )      
           {    tabla[k].PinOK[0]=' '; tabla[k].tip='0'; 
                BorraTag(k); 
                graba( k *LONGREG +LONGREG -1, 0);     } // tag todo a 0 
LastConfig=0;
} /*BorraTodo()*/

void Pulso(int puerto) /* Pone var. TcambioRele  */
{ TcambioRele= millis(); /* init , solo necesito actualizar la var. */
}

void MantenRele(int puerto) /* salida rele alto o bajo, hasta que pasen DELAY miliseg. de TcambioRele  */
{ if (TcambioRele!=0) /* si es 0 pasa  */
    if ( millis()< (TcambioRele+DELAY)  ) /* miliseg. termina sobrepasando */
            digitalWrite(puerto, HIGH);
           else { digitalWrite(puerto, LOW);
                  TcambioRele =0; } /* desactiva el pulso, reset. var. */
}        

short ord(short val) // da ordinal del valor 0>0..9>9,a>10..f>15
{ short valor;
           if ((val >= '0') && (val <= '9'))  valor = val - '0';            /* Do Ascii/Hex conversion: */               
             else {if ((val >= 'a') && (val <= 'f')) valor = 10 + val - 'a';
                   if ((val >= 'A') && (val <= 'F')) valor = 10 + val - 'A';}             
             /* Each 2 hex-digits, add byte to code: */     
return valor;             
}


  
void damemoria() // muestra ram libre por Serie , si donde='r' tambien por eth
{  int   memo= memoryTest(); //     util para dimensionar tables ranuras,Log,alarmas y comprobar que no pierdes memoria 
   Serie(" run ");Serie( seg); Serie(" seg,");
   Serie( memo); Serie(" bytes");
}

int memoryTest()  // mide ram libre de http://jeelabs.org/2011/05/22/atmega-memory-use/ antes hacia malloc
{ 
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}


void setup() {
  // Open serial communications and wait for port to open:
  short i,j,k; int value;
  Serial.begin(9600);      /* start serial port at 9600 bps: */
  while (!Serial) { ; } // wait for serial port to connect. Needed for Leonardo only
  int memo= memoryTest(); 
  Serial.print(memo); Serial.print(" bytes ");
  Ethernet.begin(mac, ip);  // start the Ethernet connection and the server:
  server.begin();
  Serial.print("IP=");
  Serial.println(Ethernet.localIP());
  for (i=0;i<LONGPIN;i++) teclas[i]=0; //'0'; 
  pinMode(7, INPUT);       /* digital input cambiado el antiguo 2 (Int0) por el 9 */
  digitalWrite(7, HIGH);   /* PullUp Resistor connected */
  pinMode(8, INPUT);   
  digitalWrite(8, HIGH);   /* PullUP-> al leer con res. conectada a 5 volt */
  pinMode(9, INPUT);       /* dentro de micro de arduino-> lee HIGH */
  digitalWrite(9, HIGH);   /* si no se ha pulsado ninguna tecla */
  pinMode(3, OUTPUT);      /* digital output for scanning */
  pinMode(4, OUTPUT);   
  pinMode(5, OUTPUT);   
  pinMode(6, OUTPUT);   // pines del teclado
  pinMode(PORTRELE, OUTPUT);    /* out for rele puerta & led in boar   */
  Ethernet.begin(mac, ip);
  server.begin();    /* y ahora actualizo datos PIN,TAG... segun eeprom   */

  LastConfig= 0; j=0;
  for (k=0; k<NUMCLAVES ;k++) {
    for (i=0; i<LONGPIN; i++) tabla[k].PinOK[i] = leeEP(j++);   /* PIN [0..4]: 12345  */
    for (i=0; i<LONGTAG ;i++)                  
           tabla[k].TagOK[i] = byte( leeEP(j++) );  /* [5..9] TagOK: 2?1.+(0x1F, 0x00, 0x1A, 0x86, 0x24)*/  
    tabla[k].tip= leeEP(j++);               /*tipo registro*/
    if (k%3==2) Serieln(); /*en depuracion LF cada 3 configuraciones, normal parpadeo led ROJO INIT*/
    if (tabla[k].tip!=0) LastConfig= k+1;         /*k 0..2 ,lastconfig 1..3 el ultimo config., el ultimo procesado*/
  }
}
void graba(int position,int mibyte) // graba en eprom esa posicion
{
 EEPROM.write(position, mibyte);
}
int leeEP(int position) // lee en eprom esa posicion
{ int doy;
  doy= EEPROM.read( position); 
  return doy;
}


short estaPin() /* en la tabla? devuelve la primera ocurrencia, OJO si puede haber 2 pin en tabla como funciona ? */
{ short i,k; char vale,c;  /*por ello para un pin doble a y 3, debes poner 1 el a(se borrara) y despues encontrara el tipo 3*/
//  if (nteclas != LONGPIN) return -2; /* si quieres que introduzcan  todo el codigo , quita comentario del principio*/
//  longitud variable pe.3, 4 o 5, en tabla 123yy, 1234y o 12345 compara contra buffer teclado,hex con  0102031313,0102030414,0102030405
  for (k=0 ; k<LastConfig ;k++ ) /*con k recorre la tabla*/
   {  vale= 1;	for (i=0 ;i<LONGPIN;i++ ) /*comprueba este reg. de la tabla,supongo bueno , */
     		   { if (  ( tabla[k].PinOK[i]!= teclas[i] ) && (tabla[k].PinOK[i]!='I') ) // 'y'-'0'='I' no vale si son <>, y tabla<>'&'
                        {vale= 0; exit;}; /*si <> ese reg. no vale, sal bucle i*/
                    }     
      if (vale) return k; /*si ese reg. vale acaba y devuelve su indice*/
   }
 return -1; 
}
char estaTag() /* en la tabla? no puede haber 2 tag en tabla , el primero el de un solo uso *********** */
{ short i,k; char igual ;
  for (k=0 ; k<=LastConfig ;k++ ) /*recorrer la tabla*/
   { if (tabla[k].tip !='0') // si es 0 estaba borrado 
     { igual= 1;
       for (i=0 ; i<LONGTAG ;i++ )
              if   ( tabla[k].TagOK[i]!= code[i] )
          {igual=0; exit;}; /*apenas hay uno <> acabas bucle interno */
       if (igual) return k; /*si es reg. vale acaba y devuelve su indice*/
     } 
   }  
return -1; 
}

void LogAcces(EthernetClient client) /*da informacion de log accesos PIN o TAG,seg*/
{ short i,k; 
             saca(' '); saca( iPin ); saca( iTag ); //ultimos leidos
             saca("</br>Try acces)PIN TAG ");  saca( numTag ); saca('/'); saca(NUMTRYACCES);
             for (k=0; k<NUMTRYACCES ;k++)
             {    if (k%2==0) sacaLF("</br>"); // 2 por linea
             
                  if ( k< 16) saca('0'); /* antepongo 0, pe. 00,01..0A  ,1f ya es 10*/
                  saca( k); // 
                  saca(')'); //    RedOrden(k); // da posicion en este formato 00) 01) ... 99) 
                  
		  for (i=0; i<LONGTAG; i++) sacaH( acceso[k].Tag[i] ); // TAG
				fechahoraEth( client, acceso[k].sTag,0);       // instante
             }
}

void LogAlarm(EthernetClient client) /*da informacion de las alarmas*/
{ short i,k; 
             saca("</br>Alarm)Port=Val ");  saca( numAlarm ); saca('/'); saca(NUMALARM);
//             saca(".Hack"); fechahoraEth(segintruso,0);
             for (k= INIAL; k<= FINAL ;k++) // alarmas configuradas
             {    if (k%3==0) saca("</br>"); // 3 por linea

                  if ( k< 16) saca('0'); /* antepongo 0, pe. 00,01..0A  ,1f ya es 10*/
                  saca( k); // 
                  saca(')'); //    RedOrden(k); // da posicion en este formato 00) 01) ... 99) 

                  saca(alarma[k].puerto ); saca("="); saca(alarma[k].valor ); saca(' '); // valor leido        	  
	              fechahoraEth(client, alarma[k].sTime,0);       // instante
             }
}
void infoConfig( EthernetClient client ) /*da informacion de configuracion PIN,TAG y TIPO*/
{ 	     short i,k;
             saca("</br>Config)PIN-TAG:TIP,hay "); saca( LastConfig ); saca("/"); saca(NUMCLAVES);
             for (k=0; k<LastConfig ;k++) // k<NUMCLAVES mostrar todos los datos
             {    if (k%3==0) sacaLF("</br>"); // 3 por linea
                  saca( byte(k+'A') ); // A, B, C,D ....
                  saca(' '); if (k<10) saca('0');  // su offset  dentro del array
                  saca(k); saca(')') ; // en formato ' 00)' o '12)')
                  for (i=0; i<LONGPIN; i++)         // car. a car el Pin  
                     saca( byte( tabla[k].PinOK[i]+'0') );
		  saca("-"); // ahora hacemos el tag
	          for (i=0; i<LONGTAG; i++){ //  RedHex( tabla[k].TagOK[i]); //
                                            if ( tabla[k].TagOK[i] < 16) saca('0'); /* antepongo 0, pe. 00,01..0A  ,1f ya es 10*/
                                            sacaH( tabla[k].TagOK[i]); // , HEX)
                                            }                                         
		  saca(':'); //
                  saca(tabla[k].tip); saca(' ');         //tipo ranura
             }
} /*infoConfig()*/

void pideHtml(EthernetClient client) // parte del Get html
{  	sacaLF("<form method=get>");
        sacaLF("Pin:<INPUT name=pin type=text size=5 maxlenght=5>");
  	sacaLF("Tag:<INPUT name=tag size=10 maxlenght=10>"); //894
  	sacaLF("al:<INPUT name=al size=1 maxlenght=1>");
  	sacaLF("tip:<INPUT name=ti size=1 maxlenght=1>"); 
	sacaLF("<input type=submit value=OK></form>"); 
} /*pidehtml()           012345678901234567890123456789012345678901-> 42 car. */


void fechahoraEth(EthernetClient client,long vseg,byte ano) // Calcula seg.running, y en funcion de segini, hora y dia actual> saca por Eth
{ short int i=0; int s=0; /*s suma dias hasta 366*/
int horc,minc,segc;  		        /* hora minutos segundos calculados a partir de linea anterior+ millis() */
int diac,mesc,anoc,dia_1_1_10c;	        /* dia mes ano calculados a partir de t ON Arduino */

 long Soffset = vueltamillis*67108864 +86400 * long(dia_1_1_10i) +3600*long( hori)+ 60*long(mini); 
 /*seg.desde puesta en hora a 00:00 de 1/1/10*/
 long seg_runing= vseg -segini;     /* diferencia seg. desde puesta en hora hasta este momento*/
 long stot= Soffset+ seg_runing;    /* seg.desde puesta en hora (+contador seg cuando se puso en hora).a 00:00 del 1/1/10, pe 11718960 */

 saca(",s:");  saca( vseg );  saca('=');
 int d1= int( stot /86400) ;            /* dias transcurridos desde 1/1/10 00:00 0..3650 ,pe. 135*/
 long h0= stot -d1 *86400;    		/* segundos restantes despues de quitar dias 0..86400 pe 55060 */
 horc= h0 /3600;                	/* horas 0..23,pe 15 */
 int m0= int (h0 -(horc *3600) );    		/* % segundos restantes despues de quitar horas 0..3600 pe. 1090 */
 minc= m0 /60;                		/* minutos 0..59 pe 17 */
 segc= m0 - minc *60;                	/* seg. 0..59 pe*/
 long anos=(d1*100)/36525; /* al div. entre 365,25 te da los anos desde 2010,pe. 2018-> 2922 dias-> 292200/36525=8 */
 anoc= 2010+anos;          /*anos 2008 (sumado a 2010)*/
 int restodias= d1 - (anos* 36525)/100 ; /* d1 - anos* 365,25 */ 
 short bisiesto=0; if ((anoc-2004)%4 ==0 ) bisiesto=1; /*2004,2008,2012,2016,...*/
 do { s=s +meses[i]; if (i==1)   s=s+ bisiesto; /* feb de ano Bisiesto */ 
 } while ( (i++<12) && (s <= restodias ) ); /* 31>15(0)->15 enero,31<35(0),59>35(1)->4 feb, 31<77(0),59<77(1),89>77(2) ->18 mar ... */                        
 mesc= i--; /* 0->ene 1, 1->feb 2, 2-> mar, 3-> abr .. */
 diac= ++restodias -( s-meses[i] );                 /* 15-(31-31)=15    ,59-(59-28)=59-31=4,69-(90-31)=69-59=10 ... */                    
 saca(diac); saca("/"); saca(mesc); // ESTO POR WBROWSER 
 if (ano) { saca("/");  saca(anoc);} //  muestro ano tambien
 saca(' '); saca(horc ); saca(':'); saca(minc );  saca(':');saca(segc); // ARREGLAR
} 


short  LeeAnalog(short int anaport)  // leer in analogica
{ short k;
  k= analogRead( anaport) >>5 ; // minimo >>2 divides entre 4 0..256 , >>3 entre 8 0..128
  return k ; // haciendo >>2 hay oscilaciones 210,209, 216
}
short DimeAlarma(short int i,short k)
{ short m= -1; // segun VerAlarm y sus limites interpreto Open,Close
              switch ( VerAlarm[i] )  {
                 case 1: case 5 : if (k>anaInf[i]) m=0 ; //sup.Minimo "OPEN" lectura normal 0, anainf 10
                                else  m=1; //"CLOSE"
                           break;                      
                 case 2 : if (k<anaSup[i]) m=1; // inf.Max "CLOSE" lectura normal 1, anasup 100 usado para contacto magne. casas
                                else m=0 ; // "OPEN"
                            break; 
                 case 3 : if (k>anaInf[i]) m=1; // Sup.Minimo "CLOSE" lectura normal 1,
                                else m=0; //"OPEN"
                            break;
                case 4:   if (k<anaSup[i])     m=0 ; //inf.Max "OPEN" lectura normal 0, anainf 10
                                else  m=1; //"CLOSE                     
                 }; /*switch */	
return m;
}

void SacaAlarma(EthernetClient client)
{ short i,k,m ;
  for ( i = INIAL; i <= FINAL; i++) {          /* saca solo entradas analogicos ACTIVAS */
              saca(',');  saca( AlarmString[i]); //crack,humo,
              saca("= ");
              k= LeeAnalog( i); //              k= analogRead(i) >> 2; //divides entre 4
              saca( k ); saca(' ');
              m= DimeAlarma( i,k); // m 0 1 
	      saca( DiStrin[m]); // open close
            };  	
}
void cabezaHtml( EthernetClient client)
{ int memo;
          sacaLF( "HTTP/1.1 200 OK");   sacaLF("Content-Type: text/html");
          sacaLF(); // OBLIGATORIO !!!
          sacaLF("<html>"); //client.print( SacaStr2(
          sacaLF("<H1>Casa ");   saca(+CASA-100);          saca(VERSION);
          saca("</H1><body style=background-color:yellow>"); /*set background to yellow */
          sacaLF("<font color='blue' size=\"4\">"); /*output some sample data to browser */
          saca("run=");  saca( seg); saca(" seg, "); // seg. running
          memo= memoryTest(); 
          Serie( memo); Serie(" bytes "); Serie("seg="); Serie( seg); // DEBPURA PRUEBA <<<<<<<<<<<          
          saca("Round:");    saca( vueltamillis); // numero de vueltas
          fechahoraEth(client,seg, 1); // DiaHoraeInitEth();   
          saca(" Ton=");                      /* dia/mes/ano hora:minutos:00 de puesta en hora */
          saca(diai); saca('/');  saca(mesi); saca('/');  saca(anoi); saca(' ');
          saca(hori); saca(":"); saca( mini); saca(":,s=");  sacaLF(segini); // DEPURA
          saca("</br>tip old=");  saca(vtip); // saca(vtip, BYTE); OJO
          saca(','); saca( ++pillo); // numero de acciones (click) desde browser
}

void datosAnalog( EthernetClient client )   // output the value of each analog input pin
{
          client.print("</br>Analog input</br> ");
          for (int analogChannel = 0; analogChannel < 6; analogChannel++) {
            int sensorReading = analogRead(analogChannel);
            client.print(" A");
            client.print(analogChannel);
            client.print(" is "); //SacaStr2 KAPUT
            client.print(sensorReading);
            Serie(" A"); // Serial.print(" D");
            Serie( analogChannel);
            Serie(" is ");
            Serie( sensorReading);
          }
}

void datosDigital(EthernetClient client)
{
          client.println("</br>Digital input</br>");
          Serial.println();
          for (int Channel = 2; Channel < 10; Channel++) {
            int sensorReading = digitalRead( Channel);
            saca(" D");
            client.print(Channel);
            client.print(" is ");
            client.print(sensorReading);
            Serie(" D"); // Serial.print(" D");
            Serie(Channel);
            Serie(" is ");
            Serie( sensorReading);
          }
}

void ProcesaRespuesta2()     /* tip define accion, campo pin y tag con param.
TIP:0(borra 1),1a(solo pin),2b(solo TAG),3c(pin+TAG),4(TIME)1535 20101231,5 cambia Pin y+1,z borra todo*/ 
{ short val,np; 
  short ni= readString.indexOf( tiString);  /* de respuesta del browser actualiza el pin(4)1234 y el tag(5)ABCDE */    
  Serial.print("ni=");   Serial.print( ni);
  if ( ni>=0 )           /*campo tipo exista  si <0 ->NADA */
    { vtip= readString.charAt( ni+3);   /*el tipo recibido del browser, puedes avanzar 1 despues viene espacio HTTP*/
      Serial.print( ",vtip=");Serial.print( vtip); 
         np= readString.indexOf( alString)+3; 
         Serial.print(",np=");   Serial.print( np);
         vindex= readString.charAt(ni++);// A..Z,[ %5B, \%5C ]%5D ^%5E ,_, `%60 , a..z
         Serial.print(",vindex=");   Serial.print( vindex);
          if (vindex=='%') { vindex= readString.charAt(ni++);   val= ord(vindex); // 5
                             vindex= readString.charAt(ni);     vindex=16*val+ord(vindex); } //B %5c ->92 : 27
          vindex= vindex-'A' ; 
          Serial.print(",n vindex=");   Serial.println( vindex);
     } 
} /*ProcesaRespuesta()*/

void TrataCREth( EthernetClient client)
{         ProcesaRespuesta();
          cabezaHtml(client);          // send a standard http response header
          SacaAlarma(client);
//          datosAnalog(client);
//          datosDigital(client);
          pideHtml( client); // cuadritos pidiendo datos
          client.println("</html>"); // final html
          Serial.println();
}

void trataEth(){
  EthernetClient client = server.available();  // listen for incoming clients
  if (client) {
    while (client.connected()) {
      if (client.available()) {
         char c = client.read();
         Serial.write(c); // MONITORIZAR DATOS DEL BROWSER
      	 if (readString.length() < TOPEWEB) 
                 readString.concat( c);  // store characters to string  
         else { Serial.print("Eth.Desborde!="); Serial.println(readString); 
                 readString="";   
                }; // borralo que era muy largo*/       
         if (c == '\n' ) {         // so you can send a reply
            TrataCREth( client);
            readString="";
            break;
         } // \n
      } // fi avalaible
    } // while connected
    delay( 1);     // give the web browser time to receive the data
    client.stop();     // close the connection:
  } // if (client)
}

void loop() {
  seg= millis() /1000;    // divido miliseg entre 1000 
  MantenRele( PORTRELE);  /* mira si todavia hay que tener activo RELE */
  trataEth();
  teclaactual= teclado(); // da tecla que esta pulsada: 49(ninguna),52(cambio ),53 (wait miliseg para muestreo )
  Serial.println(TlastKey);
  Serial.println(teclaactual);
  trataSerie();
  bufferTeclado();
  if ( ( segvueltaantes !=seg) && ( seg % 10 ==0 )) // en cada cambio de seg, y cada 10 seg.
      { damemoria();
       Pulso( PORTRELE); //depura
      }
  if ( seg<segvueltaantes )         // desborde ? se ha dado vuelta al contador de seg ?
          {   vueltamillis++;  } // problem.contador millis() de 4294967295 pasa a 0,se corrige con vueltami                
  segvueltaantes =seg; //vueltamillis
}

/* Programa para control de un acceso con RFID, Hw que necesitamos para version final: tarjeta VBING2015C */
// version que borra config acceso 'a' al abrir puerta, admite pin de 3,4 o  5 numeros,
// depuracion en casa, solo con cable usb+rj45, desde serial monitor pega el tag, cambia STX por 'p', use send+ pegar p0107075ed6p
// version 2015 funciona tambien sobre placa arduino eth, para compilar con IDE ver 1.6.4
// el string esta incluido en core a partir de version arduino 19
// esta version compila en arduino 1.6.4, en win7 HP sp1 b32
// para Arduino Duemilanove At328, con Ethernet Shield (sin SD) en com 4
// al compilar da un sketch size 16520 bytes (of 30720 max)
// ATmega328> Flas Memory 32Kb(-2K bootloader), EEprom(1KB), SRAM (2KB)
// se puede depurar Duemile con emparedado Ethernet Shield , por puerto USB
// para volcarlo en ethernet necesitas adaptador RS232 TTL, tras click para subir, das boton de reset y lo sube! 




