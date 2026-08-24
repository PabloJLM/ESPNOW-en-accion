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
//   PING                     -> lanza un barrido de activos
//   SEND <texto>             -> mensaje custom a TODOS
//   SENDTO <id_hex> <texto>  -> mensaje custom a un nodo especifico
//   CANNED <idx> [id_hex]    -> mensaje prehecho (ver catalogo en
//                                meshCannedAt/meshCannedCount, mesh.h);
//                                sin id_hex va a todos
//   NODES                    -> fuerza un volcado inmediato
//
// Formato pensado para VISA Read/Write: parseable con "split" por
// comas y por salto de linea.
void serialMasterBegin();
void serialMasterLoop();

// Para pantalla "Modo PC": cuantos volcados/comandos van y hace
// cuanto se vio actividad por USB (0 si nunca / no es maestro).
uint32_t serialMasterDumpCount();
uint32_t serialMasterCmdCount();
uint32_t serialMasterLastRxAt();
