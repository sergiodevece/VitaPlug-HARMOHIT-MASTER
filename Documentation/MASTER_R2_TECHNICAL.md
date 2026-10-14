# HARMOHIT MASTER R2 (v0.2.0) — Documentación técnica

Documento consolidado de la versión estable R2. Los informes históricos se conservan sin cambios de contenido; están enumerados en §15.

## 1. Arquitectura DSP

```
MasterEngine
 ├─ banco STEREO  (MasterChannelCore, 2 canales; un detector DRIVE enlazado)
 ├─ banco MID     (MasterChannelCore, 1 canal)
 └─ banco SIDE    (MasterChannelCore, 1 canal)
 → decodificación M/S + fundido de modo (20 ms) → OUTPUT TRIM global (suavizado) → salida
```

`MasterChannelCore` (por banco):
1. Saneado de NaN/Inf.
2. Subida a 4×.
3. HARMONICS: tres estantes de primer orden.
4. EXCITER: residuos de presencia y brillo con fundido RANGE / PUSH ALL.
5. DRIVE 670.
6. Bajada a la frecuencia base.
7. Saneado.

Los tres bancos se procesan siempre. En modo STEREO se escucha el banco STEREO; en M/S se escuchan MID y SIDE decodificados.

## 2. Routing paralelo: seco y contribuciones

Por banco y en cada muestra sobremuestreada:

```
dry       = muestra de entrada del banco
coloured  = HARMONICS → EXCITER (dry)                    →  contribución H/E = coloured − dry
p670      = Drive670Core(dry · G) / G                    →  contribución DRIVE = p670 − dry
salida    = dry + (coloured − dry) + (p670 − dry) = p670 + (coloured − dry)
```

- **Alineación exacta.** El núcleo DRIVE no tiene latencia y las tres ramas comparten la misma muestra. No hay filtrado en peine.
- **El seco cuenta una sola vez.**
- **HARMONICS y EXCITER desactivados.** `coloured − dry` es exactamente 0 y la salida es el 670 sin cambios.
- **DRIVE 0 en reposo** (paso 0, mezcla 0, POST GAIN 1). La salida es `coloured`, idéntica bit a bit al camino de HARMOHIT MIX.
- **Alternativa descartada:** el 670 en serie sobre `coloured`, como hacían R1/R1.2. Comprimiría HARMONICS/EXCITER y les aplicaría el POST GAIN.

## 3. Sobremuestreo y latencia

- `juce::dsp::Oversampling<float>(2, 2, filterHalfBandPolyphaseIIR, true, true)`, 4×. Es el mismo sobremuestreador que usa el prototipo DRIVE 670 LAB, así que no hay una segunda cadena de filtros.
- **Latencia comunicada:** 6 muestras a 44,1, 48 y 96 kHz, igual en STEREO y M/S. No cambia respecto a R1/R1.2.
- El filtro IIR no es de fase lineal: incluso con todo a 0, la salida no es nula frente a la señal seca. Es un rasgo heredado de HARMOHIT MIX.

## 4. DRIVE 670

Núcleo: `Source/DSP/Drive670Core.h` y `Source/DSP/Drive670PostGain.h`. Son copias **literales** del prototipo DRIVE 670 LAB R0.2 aprobado; las pruebas `Drive670CoreIsVerbatim` y `Drive670PostGainIsVerbatim` comparan ambos archivos byte a byte.

- **Celda variable-mu.** Par push-pull de válvulas con ley tipo Child y corte suavizado. La ganancia es `exp(−Vc)`, lineal en dB, y la curvatura crece con la reducción de ganancia.
- **Detector por realimentación** sobre la salida de la celda. El ratio efectivo crece con el nivel (1 + Ksc·salida). Red RC con ataque, recuperación y, en TC5, una memoria lenta dependiente del programa.
- **Color.** Etapa de salida push-pull de sesgo fijo, con desequilibrio par, límite de corriente de placa y pasa-altos de 8 Hz en la parte par. El transformador de salida satura en graves (~90 Hz).
- **Puesta en escala estática.** La salida se divide por la ganancia de entrada, así que por debajo del umbral el banco es neutro en nivel. No hay compensación automática.
- **Regímenes por paso:**
  - DRIVE 1–3: TC5 (0,2 ms; ≈ 2 s en picos aislados y hasta 10 s con carga sostenida).
  - DRIVE 4–7: TC3 (0,4 ms / 2 s).
  - DRIVE 8–10: TC2 (0,2 ms / 0,8 s).
- **Transiciones.** Los coeficientes se deslizan en 150 ms, sin reiniciar los detectores. DRIVE 0 se funde con el seco en 30 ms.
- **POST GAIN.** `postGainDb = paso × 1 dB` es la última operación. Rampa lineal de 20 ms que llega exactamente al valor del paso. Un único multiplicador por trama, compartido por los canales del banco.
- **Calibración del motor** (prototipo, mezcla de referencia a −18 dBFS RMS): reducción media en el 20 % más fuerte de ventanas de 400 ms de 0,5 / 1 / 1,5 | 2 / 2,4 / 2,7 / 3 | 3,5 / 4,2 / 5 dB para DRIVE 1 a 10. No son garantías universales.

Referencia técnica: se inspira en el comportamiento variable-mu y en las constantes de tiempo publicadas del Fairchild 670. Es un modelo de comportamiento, no una simulación de circuito ni una emulación. No está afiliado ni respaldado por los titulares de esa marca.

## 5. Bancos STEREO, MID y SIDE

- Cada banco tiene su propia instancia de `Drive670Core`: detector, memoria TC, color, POST GAIN y rampas propios, sin estado compartido.
- STEREO: L/R enlazados, con una sola tensión de control para los dos canales.
- MID y SIDE: mono, independientes y procesados simultáneamente. Los botones M y S de la interfaz solo eligen el banco que se edita.
- Medido:
  - influencia cruzada MID→SIDE de −126 dB y SIDE→MID de −147 dB;
  - reconstrucción M/S sin DRIVE frente a STEREO de −137 dB;
  - un programa mono con DRIVE 10 en SIDE sigue siendo exactamente mono.

## 6. Calibración por banco (`Source/DSP/DriveBankCalibration.h`)

Desplazamiento de operación recíproco `p = 670(x·G)/G`. Por debajo del umbral el banco es neutro en nivel; el detector y la etapa de válvulas trabajan a la referencia del banco. No hay normalización ni seguimiento de RMS.

| Banco | Desplazamiento | Estado |
|---|---:|---|
| STEREO | 0 dB | Idéntico al prototipo aprobado |
| MID | −3,01 dB | Un programa mono en M/S se comporta como en STEREO (−133 dB de residuo) |
| SIDE | +15 dB | **Provisional.** Con un Side típico (−34 a −36 dBFS RMS en mezclas a −18 dBFS RMS L/R), DRIVE 5 da 1,9–2,0 dB de reducción (MID: 2,3 dB). Con +20 dB, DRIVE 3 superaba 2,5 dB |

## 7. Mapeo de DRIVE

- El parámetro público `stereo|mid|side.drive` no cambia: Float 0–100 con paso 0,1, mostrado como 0–10.
- `paso = round(valor / 10)`, limitado a 0–10, con histéresis de 0,05 pasos (un cambio exige alejarse 0,55 pasos). En `prepareToPlay` el plugin arranca directamente en el paso actual.

| Parámetro | Paso | POST GAIN | Lectura en el mando |
|---:|---:|---:|---|
| 0 | 0 | — (DRIVE desactivado) | ≈ 0,3 (desfase del puntero) |
| 7 | 1 | +1 dB | ≈ 1 |
| 50 | 5 | +5 dB | 5 |
| 70 | 7 | +7 dB | ≈ 6,9 |
| 100 | 10 | +10 dB | 10 |

El puntero del mando está desplazado unos 3° respecto a su ángulo programado. Es un rasgo de la interfaz aprobada, presente también en R1/R1.2, y no afecta al paso interno.

## 8. Automatización y compatibilidad

- Los IDs de parámetro, sus rangos y los identificadores AU/VST3 (`aufx / HmR1 / Vtpl`, UID VST3 250544335) no cambian.
- Una automatización antigua continua de 0 a 100 recorre los pasos 0 a 10 una vez cada uno. Un jitter de ±0,3 no hace alternar pasos.
- Los cambios de DRIVE (0↔10, TC5↔TC3, TC3↔TC2) y de modo STEREO↔M/S durante la reproducción no producen clics. La segunda diferencia máxima queda entre 0,76 y 1,06 veces la del estado fijo de referencia.
- **Sesiones v0.1.0 (R2 aprobada) → v0.2.0:** el estado se restaura con 0 diferencias en los 20 parámetros y el audio es idéntico muestra a muestra (VST3, host JUCE).
- **Cambio de comportamiento R1.2 → R2:** las sesiones antiguas recuperan el valor de DRIVE, pero el motor y su significado son nuevos. Por ejemplo, DRIVE a 70 aplica ahora compresión TC3 y +7 dB de POST GAIN. Recomendación: abrir **copias** de las sesiones, revisar los parámetros y bajar el nivel de salida antes de reproducir, y no sobrescribir presets antiguos.

## 9. Nivel interno y OUTPUT TRIM

- Toda la cadena trabaja en coma flotante. Entre el POST GAIN y el OUTPUT TRIM solo hay operaciones lineales: bajada IIR, decodificación M/S y fundido de modo.
- No hay limitador, clipper, techo ni normalización. La única intervención es sustituir valores no finitos por 0.
- Prueba: una mezcla a −10 dBFS RMS a DRIVE 10 alcanza +4,02 dBFS; con OUTPUT TRIM a −12 dB queda exactamente 12,000 dB más baja, sin recortes.
- OUTPUT TRIM: de −24 a +12 dB, sin cambios. Recortar o no por encima de 0 dBFS depende del host y del formato de exportación (el PCM entero recorta).

## 10. Resultados de pruebas (compilación limpia v0.2.0, macOS 26.5, Apple M5)

| Suite | Resultado |
|---|---|
| `HarmoHitMasterTests` | Aprobada. Incluye las 63 equivalencias HARMONICS/EXCITER frente a HARMOHIT MIX (error 0), estado y edición, M/S, ausencia de asignaciones en tiempo real y transiciones |
| `HarmoHitDriveTests` | Aprobada. Mando → attachments → APVTS, automatización y estado sin cambios en la edición (salto DC 0,00044), matriz de 648 casos y reducción de ganancia monótona |
| `HarmoHitDrive670IntegrationTests` | 62/62. Equivalencia **bit a bit** con DRIVE 670 LAB R0.2 en STEREO sin H/E a 44,1, 48 y 96 kHz, con 4 materiales, DRIVE 0/1/3/5/7/10, automatización que cruza regímenes y el procesador APVTS real. Además: routing, M/S, calibración SIDE, automatización, estado y robustez (tamaño de bloque, silencio, NaN, asignaciones, latencia y headroom) |
| `Drive670CoreIsVerbatim`, `Drive670PostGainIsVerbatim` | Aprobadas |
| CTest | **5/5** |
| Medidas | Idénticas a las de la ejecución R2 aprobada (salvo el tiempo de CPU) |
| VST3 en host JUCE junto a MIX | Aprobado, con el editor de 760×380 |
| Interfaz | Render del editor v0.2.0 idéntico píxel a píxel al de R1.2 (5 estados, a 1× y 2×) |
| Binario frente a R2 aprobada | De 28 secciones Mach-O, solo `__cstring` difiere, y únicamente en la hora de compilación que inserta JUCE |

La validación de auval y del AU registrado sobre el artefacto distribuido exacto se publica en el GitHub Release v0.2.0.

## 11. Consumo de CPU

Banco de pruebas: 60 s estéreo a 48 kHz, HARMONICS y EXCITER a 50 en todos los bancos, un hilo en Apple M5.

| Configuración | R1.2 | R2 |
|---|---:|---:|
| DRIVE 0 | 2,13 % | 6,29 % |
| DRIVE 5 | 3,24 % | 6,33 % |

El aumento se debe a que el 670 funciona siempre en los tres bancos, con unos 1,8 % por banco. Así los detectores están calientes y los cambios de modo y DRIVE no producen clics. La optimización queda para una versión futura y no forma parte de R2 congelada.

## 12. Decisiones de diseño

- El núcleo DRIVE se copió sin cambios para conservar exactamente el sonido aprobado. La adaptación vive en el routing y en una calibración aislada.
- Se mantiene el mismo sobremuestreo: es la condición para la equivalencia bit a bit y para que la latencia no cambie.
- DRIVE va en paralelo para preservar HARMONICS/EXCITER.
- El DRIVE continuo se convierte en 11 pasos para conservar exactamente lo que se aprobó en la escucha.
- No hay limitador ni normalización: el nivel final es una decisión del ingeniero.

## 13. Limitaciones

- La calibración SIDE es provisional.
- CPU de unos 6,3 % (§11).
- Desfase del puntero de unos 3°.
- Binarios solo arm64, firmados ad hoc y sin notarizar.
- VST3 no probado en un DAW; Intel y Windows no probados.
- La batería de equivalencia frente al prototipo requiere el proyecto de laboratorio y material privado que no se publican.

## 14. Verificación de la distribución

Los hashes SHA-256 de los ejecutables están en `RELEASE_NOTES_v0.2.0.md`. Los del ZIP, en el GitHub Release y en `SHA256SUMS.txt`.

## 15. Informes históricos

| Informe | Contenido |
|---|---|
| `MASTER_R1_REPORT.md`, `R1-measurements.txt` | MASTER R1 |
| `R11_DRIVE_DIAGNOSIS.md`, `R11-drive-baseline.csv` | Diagnóstico de DRIVE R1.1 |
| `R12_DRIVE_AUDIT.md`, `R12-audit/` | Auditoría independiente y herramientas |
| `R12_DRIVE_R1_2_REPORT.md` | DRIVE adaptativo R1.2, descartado |
| `R2_DRIVE670_INTEGRATION_PLAN.md`, `R2_DRIVE670_INTEGRATION.md`, `R2-drive670-integration-tests.txt` | Integración R2 |
| `VINTAGE_RECOVERY_2026-10-13.md`, `DEVELOPMENT_NOTES.md` | Historial heredado de HARMOHIT MIX |
