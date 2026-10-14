# Changelog — VitaPlug HARMOHIT MASTER

## v0.2.0 — R2 · Motor DRIVE 670 (2026-10-14)

### Nuevo
- **Motor DRIVE 670**: compresión variable-mu con detección por realimentación, coloración push-pull y saturación de transformador, inspirada en compresores clásicos de válvulas. El núcleo es una copia literal del prototipo DRIVE 670 LAB R0.2 aprobado en escucha real.
- **Tres regímenes** según la posición del mando: TC5 (DRIVE 1–3, recuperación dependiente del programa), TC3 (4–7) y TC2 (8–10).
- **POST GAIN** de +1 dB por paso (0 a +10 dB) después de todo el procesamiento DRIVE, sin limitador ni normalización.
- **DRIVE en paralelo**: `salida = p670 + (coloured − dry)`. HARMONICS/EXCITER quedan fuera del compresor y del POST GAIN.
- **Calibración por banco**: STEREO 0 dB, MID −3,01 dB y SIDE +15 dB (provisional), en `DriveBankCalibration.h`.
- **Mapeo** del DRIVE continuo (0–100) a 11 pasos enteros, con histéresis y arranque en el paso actual.
- Batería de integración frente al prototipo (equivalencia bit a bit), documentación técnica consolidada y flujo de mastering Mid/Side.

### Cambios de sonido respecto a R1.2
- DRIVE tiene un motor y un significado nuevos. Las sesiones antiguas recuperan el valor del parámetro pero suenan distinto: más compresión y +1 dB de nivel por paso. **Prueba sobre copias de las sesiones** y revisa el nivel de salida.
- HARMONICS, EXCITER, RANGE, PUSH ALL, OUTPUT TRIM, los modos y la interfaz no cambian. Con DRIVE 0, HARMONICS/EXCITER son equivalentes bit a bit a HARMOHIT MIX.

### Sin cambios
- Identificadores AU/VST3 (`aufx / HmR1 / Vtpl`, bundle `com.vitaplug.harmohit.master.r1`), IDs y rangos de parámetros, sobremuestreo 4× y latencia (6 muestras), e interfaz de 760×380 (idéntica píxel a píxel a R1.2).

### Limitaciones conocidas
- CPU de unos 6,3 % de un núcleo a 48 kHz (M5).
- Calibración SIDE provisional.
- Desfase del puntero del mando de unos 3°.
- Binarios solo arm64, firmados ad hoc y sin notarizar.
- VST3 sin probar en un DAW.

## R1.2 (no publicada)
DRIVE adaptativo con rodilla referida al programa. Descartado tras la escucha. Véase `Documentation/R12_DRIVE_R1_2_REPORT.md`.

## R1.1 (no publicada)
Diagnóstico del DRIVE R1 y auditoría independiente. Véanse `Documentation/R11_DRIVE_DIAGNOSIS.md` y `Documentation/R12_DRIVE_AUDIT.md`.

## R1 (no publicada)
Primera versión MASTER: bancos STEREO/MID/SIDE, HARMONICS/EXCITER de MIX, DRIVE R1 e interfaz definitiva. Véase `Documentation/MASTER_R1_REPORT.md`.

## Historial heredado
El historial de Git anterior procede de HARMOHIT MIX y se conserva sin reescribir.
