VITAPLUG HARMOHIT MASTER R2 — WINDOWS x64 — MIGUELOMENER EDITION

VitaPlug HARMOHIT MASTER v0.2.0, DRIVE 670. La edición solo identifica este paquete.
VST3 nativo x64 para Blue Cat PatchWork dentro de Pro Tools; NO es AAX ni AU.

1. Cierra Pro Tools y cualquier aplicación que utilice el plugin.
2. Si ya existe el mismo MASTER, conserva una copia de seguridad fuera de la carpeta VST3.
   No sobrescribas HARMOHIT MIX, Gainer80 ni otros plugins VitaPlug.
3. Descomprime el ZIP. SHA256SUMS.txt permite verificar todos los archivos incluidos.
4. Copia la carpeta COMPLETA "VitaPlug HARMOHIT MASTER.vst3" a:
   C:\Program Files\Common Files\VST3\
   Puede solicitar administrador. No copies solamente el binario interno ni uses directorios AAX.
5. Abre Pro Tools e inserta Blue Cat PatchWork x64 en una pista estéreo de prueba.
6. En PatchWork carga "VitaPlug HARMOHIT MASTER" como VST3; si no aparece,
   comprueba la carpeta VST3 explorada por PatchWork y actualiza su lista.
7. Comprueba el panel verde/vintage MASTER 760 x 380, MODE, entrada y salida de audio.
8. Antes de reproducir baja OUTPUT TRIM a -12 dB y usa un volumen de escucha prudente.
9. Sigue README_PRUEBAS.txt y envía una captura real dentro de Pro Tools/PatchWork.

DRIVE añade POST GAIN fijo de +1 dB por paso (hasta +10 dB).
OUTPUT TRIM conserva -24..+12 dB. No hay limitador ni normalización automática.
Sesiones anteriores a R2 pueden recuperar DRIVE y sonar distintas: trabaja sobre copias.

DEPENDENCIAS: runtime-dependents.txt identifica las DLL del binario real.
Si falta MSVCP140/VCRUNTIME140, instala el Microsoft Visual C++ Redistributable x64
oficial compatible con VS2022; no descargues DLL sueltas de terceros:
https://learn.microsoft.com/es-es/cpp/windows/latest-supported-vc-redist
No funciona en Pro Tools/PatchWork de 32 bits. Binario sin firma Authenticode.
La compatibilidad física/musical con Pro Tools/PatchWork está pendiente de tu smoke.
