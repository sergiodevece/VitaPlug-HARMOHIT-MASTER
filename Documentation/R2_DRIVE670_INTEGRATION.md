# HARMOHIT MASTER — Integración de DRIVE 670 LAB R0.2

Fecha: 14 de octubre de 2026.

**Estado:** compilado y probado. **No instalado**: el MASTER instalado sigue siendo R1.2 (`a7cdd541…`). Sin commits. Sin aprobación musical hasta el smoke en Logic.

**Punto de recuperación:** `<VITAPLUG>/backups/HARMOHIT_MASTER_R1.2_source_snapshot_20261014/`. Contiene el árbol completo, el estado de Git, el parche y los hashes. El plan previo está en `R2_DRIVE670_INTEGRATION_PLAN.md`.

## 1. Archivos

| Archivo | Cambio |
|---|---|
| `Source/DSP/Drive670Core.h`, `Source/DSP/Drive670PostGain.h` | **Nuevos.** Copia literal del LAB R0.2; dos pruebas de CTest exigen que sean idénticos byte a byte. |
| `Source/DSP/DriveBankCalibration.h` | **Nuevo.** Calibración aislada por banco y mapeo de DRIVE a pasos. |
| `Source/DSP/MasterChannelCore.h` | DRIVE R1.2 sustituido por el núcleo 670 con contribución diferencial. Añade accesos de prueba. HARMONICS, EXCITER, RANGE, PUSH ALL, trim y sobremuestreo sin cambios. |
| `Source/DSP/MasterEngine.h` | `prepare()` asigna la calibración de cada banco y arranca en el paso actual de DRIVE. Añade accesos de prueba. |
| `Source/PluginProcessor.cpp/.h` | `currentSettings()` se reutiliza en `prepareToPlay` y `processBlock`, más un acceso de prueba. Parámetros, IDs y layout sin cambios. |
| `Tests/Drive670IntegrationTests.cpp`, `Tests/LabReference.cpp` | **Nuevos.** Batería de integración, con el motor del LAB como referencia (solo lectura). |
| `Tests/MasterTests.cpp`, `Tests/DriveTests.cpp` | Se actualizan tres aserciones que describían el DRIVE anterior (§8). |
| `CMakeLists.txt` | Objetivos y tests de integración, condicionados a que exista el proyecto LAB. |

Sin cambios, verificados por hash frente al punto de recuperación: `PluginEditor.*`, recursos gráficos y `HarmoCore.h`. `AdaptiveDrive.h` (R1.2) se conserva en el árbol, sin uso. MIX, Gainer80 y el LAB no se han modificado.

## 2. Arquitectura final

```
L/R ─► MasterEngine ─┬─► banco STEREO (2 canales enlazados) ─┐
                     ├─► banco MID  (1 canal) ───────────────┼─► decodificación M/S + fundido de modo ─► × OUTPUT TRIM ─► salida
                     └─► banco SIDE (1 canal) ───────────────┘
Banco (MasterChannelCore), dentro de su dominio 4×:
   dry ─► HARMONICS ─► EXCITER ─► coloured
   dry ─► ×G ─► Drive670Core R0.2 (celda variable-mu, realimentación, push-pull, transformador, TC, POST GAIN) ─► ×1/G ─► p670
   salida del banco = p670 + (coloured − dry)          [DRIVE 0 en reposo: salida = coloured]
```

Los tres bancos se procesan siempre, cada uno con su propio núcleo: detector, memoria TC, color, POST GAIN y rampas propios. Los botones M y S solo eligen qué banco se edita.

## 3. Mapeo de DRIVE

- El parámetro `stereo|mid|side.drive` no cambia: Float 0–100 con paso 0,1, mostrado como 0–10.
- El paso interno es `round(valor / 10)`, limitado a 0–10.
- Hay una histéresis de 0,05 pasos: el paso solo cambia cuando el valor se aleja 0,55 pasos del actual. Es determinista para una misma automatización.
- 0 da el paso 0 y 100 da el paso 10.
- `prepareToPlay` arranca en el paso actual, como el plugin LAB, sin rampa desde 0.
- Una automatización antigua continua de 0 a 100 recorre los pasos 0 a 10 una vez cada uno.
- Un jitter de ±0,3 alrededor de 25, 35 o 75 no alterna pasos.
- El aspecto del mando no cambia.

## 4. Routing paralelo

La salida del banco es `p670 + (coloured − dry)`, que equivale a `dry + residuo H/E + (p670 − dry)`.

- **Alineación exacta.** Seco, color y 670 pertenecen a la misma muestra sobremuestreada y el núcleo no tiene latencia.
- **Sin doble seco.** La resta usa la misma muestra que entra al 670.
- **H/E desactivados.** `coloured − dry` vale exactamente 0, así que la salida es la del LAB.
- **H/E activos.** Su contribución se suma sin pasar por el compresor ni por el POST GAIN, así que su comportamiento aprobado no cambia.
- **DRIVE 0 en reposo.** La salida es `coloured` bit a bit, lo que mantiene las 63 equivalencias con MIX.

Alternativa descartada: el 670 en serie sobre `coloured`, como hacían R1/R1.2. Comprimiría HARMONICS/EXCITER y les aplicaría el POST GAIN, así que su comportamiento cambiaría con DRIVE.

## 5. Sobremuestreo y latencia

El LAB R0.2 y MASTER usan el mismo sobremuestreador: `juce::dsp::Oversampling<float>(2, 2, filterHalfBandPolyphaseIIR, true, true)`. El núcleo se ejecuta dentro del dominio 4× existente de cada banco:
- hay una sola cadena de filtros;
- no hay sobremuestreo duplicado;
- la latencia comunicada no cambia: 6 muestras a 44,1, 48 y 96 kHz, igual en STEREO y M/S.

Por eso la equivalencia es **bit a bit**, no aproximada (§7).

## 6. Calibración provisional por banco (`DriveBankCalibration.h`)

Ganancia de operación recíproca `p = 670(x·G)/G`. Por debajo del umbral el banco no cambia de nivel; el detector y la etapa de válvulas trabajan a la referencia del banco. No hay normalización ni seguimiento de RMS.

| Banco | Desplazamiento | Motivo |
|---|---:|---|
| STEREO | 0 dB | Idéntico al LAB R0.2 |
| MID | −3,01 dB | M = (L+R)/√2 es 3 dB más caliente; un programa mono en M/S se comporta como en STEREO (residuo −133 dB) |
| SIDE | **+15 dB (provisional)** | Medido (abajo) |

Medidas con dos mezclas a −18 dBFS RMS L/R (Mid −15, Side −34 a −36 dBFS RMS). GR media del banco SIDE (dB):

| Side | Desplazamiento | D3 | D5 | D7 | D10 |
|---|---:|---:|---:|---:|---:|
| −35,7 dBFS (mezcla de Logic) | 0 dB | 0,00 | 0,00 | 0,00 | 0,01 |
| | +10 dB | 0,16 | 0,74 | 1,22 | 2,20 |
| | **+15 dB** | **1,14** | **1,85** | **2,38** | **3,63** |
| | +20 dB | 2,49 | 3,20 | 3,72 | 5,44 |
| −34,4 dBFS (mezcla A/B) | **+15 dB** | **1,31** | **2,03** | **2,56** | **4,03** |
| −45 dBFS (Side 10 dB más bajo) | **+15 dB** | 0,00 | 0,06–0,16 | 0,39–0,50 | 0,85–1,02 |
| −25 dBFS (Side 10 dB más alto) | **+15 dB** | 3,97–4,21 | 4,67–4,91 | 5,22–5,47 | 7,47–8,01 |

Con +15 dB, un Side típico recibe un 75–85 % de la reducción del banco MID (MID a DRIVE 5: 2,31 dB). El régimen 1 se mantiene suave. Con +20 dB, DRIVE 3 ya supera los 2,5 dB.

Otras comprobaciones:
- El Side mono sale exactamente mono.
- Las colas y el silencio quedan en silencio digital.
- El ruido de Side a −66 dBFS entre ráfagas fuertes no bombea: 0,02 dB de variación.
- MID y SIDE son independientes: −126 y −147 dB de influencia cruzada.

**Debe confirmarse en el smoke M/S.**

## 7. Equivalencia LAB/MASTER (HARMONICS y EXCITER desactivados, trim 0 dB, STEREO)

- **Bit a bit** a 44,1, 48 y 96 kHz, en cuatro materiales (mezcla de Logic, mezcla A/B, batería acústica y seno de 1 kHz), con DRIVE 0, 1, 3, 5, 7 y 10.
- También bit a bit bajo automatización 0>10>0>3>4>3>7>8>7>5>1>9, que cruza TC5→TC3→TC2.
- También a través del procesador real con `stereo.drive` = 30 y 100.

Al ser idénticas las muestras, coinciden también el RMS, los picos, la GR, el THD, los armónicos, los transitorios y la respuesta temporal (valores en `R2-drive670-integration-tests.txt`).

## 8. Regresiones de HARMONICS, EXCITER y suites anteriores

- **`HarmoHitMasterTests`:** las 63 equivalencias H/E frente a MIX, estado y edición, M/S, sin asignaciones en tiempo real y transiciones, todas aprobadas.
- **Aserción sustituida:** "DRIVE no aumenta el pico". El POST GAIN aprobado sube la salida 1 dB por paso. Ahora se exige que el pico no supere POST GAIN + 1 dB respecto al camino neutro.
- **`HarmoHitDriveTests`:** cadena mando → attachments → APVTS, automatización y estado sin cambios en la edición (salto DC máximo 0,00044), matriz R11 de 648 casos, aprobados. Se retiran las regresiones R1.2. Se adaptan dos aserciones de la matriz:
  - el THD se mide con los órdenes 2 a 9, por encima de 0,1 %, porque el 670 tiene componente par;
  - la propiedad monótona pasa a ser la reducción de ganancia: la diferencia frente al neutro no es monótona cuando la compresión fuerte se combina con el POST GAIN, igual que en el LAB.
- **Routing:** con HARMONICS 50, EXCITER 60 a 8 kHz y H70+E50 PUSH ALL, junto con DRIVE 3 o 10, la salida coincide con LAB(DRIVE) más la contribución H/E sin cambios. Residuo por debajo de −100 dB a 44,1 y 48 kHz.

## 9. Automatización y estados

- **Cambios de DRIVE sobre música:** la segunda diferencia máxima, comparada con el mayor de los estados fijos de origen y destino, queda entre 0,76 y 1,06 en 0→10, 10→0, 3→4, 4→3, 7→8 y 8→7. No hay clics.
- **STEREO↔M/S durante la reproducción:** relación 1,000, porque los bancos siguen activos.
- **Cambio del banco editado:** salida idéntica bit a bit (`HarmoHitDriveTests`).
- **Restauración de sesión:** recupera los tres DRIVE (7, 4 y 9) y la salida es finita.

## 10. CPU y latencia

| Banco de pruebas, 60 s estéreo a 48 kHz, H/E 50, tres bancos activos | R1.2 | Integración 670 |
|---|---:|---:|
| DRIVE 0 | 2,13 % | 6,29 % |
| DRIVE 5 | 3,24 % | 6,33 % |

El aumento se debe a que el 670 funciona siempre en los tres bancos, cerca de 1,8 % cada uno como en el LAB. Así los detectores están calientes y los cambios de modo y DRIVE no producen clics. Es una optimización posible (§15). La latencia sigue en 6 muestras. Sin asignaciones en el hilo de audio con automatización de DRIVE y modo.

## 11. Headroom

Una mezcla a −10 dBFS RMS a DRIVE 10 alcanza +4,02 dBFS antes del OUTPUT TRIM. Con el trim a −12 dB queda en −7,98 dBFS: exactamente 12,000 dB menos y sin muestras recortadas. No hay limitador, clipper ni normalización. El OUTPUT TRIM sigue siendo de −24 a +12 dB, sin cambios.

## 12. Compilación y hashes

- AU y VST3 arm64 Release, como el MASTER instalado, que también es arm64.
- El workflow de Windows (VST3 x64) compila el mismo C++. Las pruebas de integración se omiten allí porque requieren el proyecto LAB. No se ha ejecutado ese build en esta sesión.
- IDs sin cambios: aufx / HmR1 / Vtpl, `com.vitaplug.harmohit.master.r1`, versión 0.1.0.
- Firmados ad hoc, con `codesign --verify --strict` correcto.
- `HarmoHitCoexistenceTests`: el VST3 nuevo carga junto al VST3 de MIX, con el editor aprobado de 760×380.

| Artefacto | SHA-256 |
|---|---|
| AU | `086cd3c38c75856976b75a07eaece5d85fe17c36fda7c0d6146e286d7177afb2` |
| VST3 | `fe36a4382d7fd3297b8bee6b0ee096c73025020b7deb9c9bc2a926f8e8e80317` |

CTest: **5/5**. Son MasterTests, DriveTests, la batería de integración y dos pruebas de copia literal del núcleo. La batería de integración tiene 62 comprobaciones aprobadas y 0 fallos.

## 13. Instalación controlada (pendiente de autorización)

1. Cerrar Logic de forma ordenada.
2. Copiar el MASTER R1.2 instalado (`a7cdd541…`) fuera de Components, por ejemplo en `backups/HARMOHIT_MASTER_R1.2_AU_backup_<fecha>/`, y comprobar su hash.
3. Copiar con `ditto` el AU nuevo desde `<build>/VitaPlugHarmoHitMasterR1_artefacts/Release/AU/` y comprobar el hash `086cd3c3…`.
4. Ejecutar `codesign --verify --deep --strict` y `auval -v aufx HmR1 Vtpl`.
5. Comprobar qué binario resuelve el sistema cargando el AU registrado:
   - **Prueba recomendada.** En Logic, poner en dos pistas iguales LAB R0.2 a DRIVE n y MASTER en STEREO con H/E a 0 y DRIVE n, e invertir la polaridad de una. La suma debe ser silencio digital: es la misma salida bit a bit con la misma latencia.
   - **Prueba simple.** A DRIVE 10 sobre material muy bajo, la salida sube unos 10 dB. R1.2 no lo hace.
6. En Logic: Gestor de plug-ins, VitaPlug HARMOHIT MASTER, "Restablecer y volver a analizar selección".

## 14. Restauración del MASTER anterior

- **Binario.** Cerrar Logic y sustituir el componente por la copia R1.2 (`a7cdd541…`), que se habrá hecho en el paso 2 de §13. Después, auval y reanálisis en Logic.
  Existe también la copia de R1 en `backups/HARMOHIT_MASTER_R1_AU_backup_20261014/`.
- **Código fuente.** Extraer `harmohit-master-R1.2-worktree.tar.gz` del punto de recuperación siguiendo su `README_RESTORE.txt`.

## 15. Pendiente de validación musical

1. **Smoke STEREO:** que suene como LAB R0.2. Debe hacerlo por construcción; la prueba de nulo lo confirma.
2. **Smoke M/S:** la calibración SIDE de +15 dB es provisional, y el MID de −3,01 dB debe sentirse coherente con STEREO.
3. **DRIVE combinado con HARMONICS/EXCITER:** el carácter de la suma paralela. H/E no se comprimen ni reciben el POST GAIN.
4. **Gestión de nivel:** POST GAIN hasta +10 dB por banco con OUTPUT TRIM manual.
5. **CPU:** 6,3 % de un núcleo. Si molesta, una optimización futura es no ejecutar el 670 de los bancos del modo inactivo, con una estrategia de calentamiento al cambiar de modo.
6. **Coherencia de la sesión:** las sesiones guardadas con R1 o R1.2 cargarán sus mismos valores de DRIVE con el sonido 670 y su POST GAIN. Por ejemplo, un DRIVE a 7 subirá +7 dB más compresión.

---

## 16. Preflight e instalación para el smoke (14‑oct‑2026)

### 16.1 Comprobación visual: aprobada, sin discrepancias

- **Tamaño.** El editor mide 760×380 puntos, fijo y no redimensionable (`setSize(760,380)`). Se dibuja escalando `HarmoHitMasterApproved.png` (1774×887 px, misma proporción 2:1). En Retina se representa a 1520×760 píxeles.
- **Origen de la cifra 1180×590.** En el repositorio solo aparece como `genericSize` del fondo genérico de **MIX** (`visual-checks/HARMOHIT_MIX_editor_background_diagnostics.txt`) y en una captura de MIX. Ningún documento de MASTER usa esa medida. El `README.md` de MASTER indica "Editor JUCE 760x380 basado en la imagen MASTER aprobada", y las capturas de MASTER R1 en Logic son 760×380 y 1520×760.
- **Fidelidad.** El editor de R2 se renderizó fuera de pantalla con los fuentes actuales y con los de R1.2 del punto de recuperación, en 5 estados (por defecto; STEREO con DRIVE 7, H50, E45 y trim −6; M/S editando M; M/S editando S; DRIVE 100), a 1× y a 2×. Resultado: **0 píxeles distintos en los 10 renders**. La interfaz instalada es la misma que la de R1.2 aprobada en Logic. `PluginEditor.*` y los recursos tienen además el mismo hash.

### 16.2 Mapeo de DRIVE: correcto

Medido con el plugin real: APVTS, normalización del host, mando, motor y restauración de estado.

| Parámetro | Normalizado en el host | Ángulo programado del mando | Lectura en la escala impresa | Paso interno | POST GAIN | Tras restaurar el estado |
|---:|---:|---:|---:|---:|---:|---|
| 0 | 0,000 | −135,0° | ≈ 0,3 | 0 | +0 dB (sin DRIVE) | 0 → paso 0 |
| 7 | 0,070 | −116,1° | ≈ 1,0 | 1 | +1 dB | 7 → paso 1 |
| 50 | 0,500 | 0,0° | 5 | 5 | +5 dB | 50 → paso 5 |
| 70 | 0,700 | +54,0° | ≈ 6,9 | 7 | +7 dB | 70 → paso 7 |
| 100 | 1,000 | +135,0° | 10 | 10 | +10 dB | 100 → paso 10 |

- El paso es el mismo al cargar en ese valor y al automatizar desde 0 hasta él.
- La escala impresa abarca de unos −141° ("0") a +139° ("10"). El puntero dibujado tiene un desfase de unos +3° respecto al ángulo programado. Por eso en el extremo inferior el puntero se lee como unos 0,3 pasos. Es preexistente (idéntico en R1.2), está en la interfaz congelada y no afecta al paso interno.
- Parámetro 7 de 100 equivale a 0,7 en el mando y produce el paso 1. El "7" impreso corresponde al parámetro 70.
- Automatización antigua y restauración de presets: cubiertas por `HarmoHitDrive670IntegrationTests` (rampa continua de 0 a 100, jitter, restauración 7/4/9) y por `HarmoHitDriveTests` (estado de los tres DRIVE).

### 16.3 Instalación

| Comprobación | Resultado |
|---|---|
| Artefacto frente a las pruebas | Ningún fuente cambió después de compilar las pruebas; la compilación no recompiló nada. |
| Logic | No estaba abierto. No hubo que cerrarlo ni se descartó nada. |
| Copia de R1.2 | `<VITAPLUG>/backups/HARMOHIT_MASTER_R1.2_AU_backup_20261014/VitaPlug HARMOHIT MASTER R1.2.component`, hash `a7cdd541…` y árbol idénticos, con `README_RESTORE_R1.2.txt`. |
| Instalado | `~/Library/Audio/Plug-Ins/Components/VitaPlug HARMOHIT MASTER.component`, árbol idéntico byte a byte al artefacto R2 firmado. |
| Hash | `086cd3c38c75856976b75a07eaece5d85fe17c36fda7c0d6146e286d7177afb2` |
| Identidad | aufx / HmR1 / Vtpl, "VITAPLUG: VitaPlug HARMOHIT MASTER", `com.vitaplug.harmohit.master.r1`, 0.1.0, arm64. |
| Firma | Ad hoc con recursos sellados (flags 0x2); verificación estricta correcta. |
| auval | `auval -v aufx HmR1 Vtpl`: AU VALIDATION SUCCEEDED, 0 advertencias. |
| Binario que resuelve el sistema | `HarmoHitDriveTests --compare-au` carga el AU registrado: 4.062.960 muestras frente al código R2, **error máximo 0**. |
| Otros componentes | MIX `4c3a5cfe…`, Gainer80 `cec5703b…` y DRIVE 670 LAB `2e1c1f6a…` sin cambios. |
