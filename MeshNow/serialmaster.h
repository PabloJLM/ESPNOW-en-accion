#pragma once
#include <Arduino.h>
#include "config.h"

// Interfaz serial del nodo MAESTRO para LabVIEW.
// Solo hace algo si IS_MASTER == 1.
//
// SALIDA (cada ~500 ms), lineas de texto terminadas en \n:
//   $STAT,<miId>,<tx>,<rx>,<relay>,<dup>,<directos>,<total>,<pps>
//   $NODE,<id>,<mac>,<rssi>,<hops>,<directo0o1>,<edadMs>
//   ...una linea $NODE por nodo vivo...
//   $END
//
// ENTRADA (comandos desde LabVIEW, una linea por comando):
//   PING          -> lanza un barrido de activos
//   SEND <texto>  -> difunde <texto> a toda la malla (multi-salto)
//   NODES         -> fuerza un volcado inmediato
//
// Formato pensado para VISA Read/Write: parseable con "split" por
// comas y por salto de linea.
void serialMasterBegin();
void serialMasterLoop();
