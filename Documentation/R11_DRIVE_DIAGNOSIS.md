# HARMOHIT MASTER — investigación R1.1 de DRIVE

Fecha: 13 de octubre de 2026. **Incidencia abierta; reparación no aplicada.**

El usuario confirma que R1 carga en Logic y que funcionan STEREO, M/S, selección de bancos,
HARMONICS, EXCITER, RANGE, PUSH ALL y respuesta de la UI. Reporta DRIVE sin efecto perceptible.
Ese smoke no se sustituye ni se invalida por los ensayos de este informe: R1 no se declara validada.

## 1. Conclusión y causa raíz

No se ha reproducido una inoperancia total de DRIVE en el código ni en el AU instalado.
El AU y el procesador generan exactamente las mismas muestras en los ensayos controlados:
4.062.960 muestras comparadas, error máximo 0. Los tres bancos generan diferencias
progresivas y armónicos. Por tanto no es correcto afirmar que DRIVE llega siempre a cero,
que la rama esté desconectada o que el DSP no tenga ningún efecto.

Sí se demuestra una sensibilidad muy baja a niveles bajos/medios, especialmente antes del
final del recorrido. La causa matemática de esa respuesta es la combinación de una ley
cúbica conservadora, una mezcla proporcional a esa misma intensidad y una saturación con
pendiente unidad para señal pequeña. La protección de ataques reduce aún más el efecto.
Esto **puede contribuir** al síntoma perceptivo, pero no demuestra la causa exacta de la
ausencia de efecto en la sesión de Logic: faltan su nivel de entrada, valores efectivos y audio.

No he retocado ganancias, curvas ni protección para presentar un cambio de sonido como una
reparación de un fallo no reproducido. No existe todavía un DSP R1.1 corregido ni un antes/después
de reparación. Las comparativas siguientes son DRIVE 0 frente a DRIVE activo en R1.

## 2. Recorrido auditado

| Etapa | Código y comprobación |
|---|---|
| Mando | `PluginEditor.cpp`: Knob, componente `drive`, rango recibido del attachment 0..100; la escala gráfica es 0..10. |
| Attachment | `bindBank()` enlaza `stereo.drive`, `mid.drive` o `side.drive`; el test pone el mando a 70 y verifica el banco seleccionado y los demás. |
| APVTS | `createParameterLayout()`: tres parámetros Float independientes, mismo rango y versionHint, sin compartir significados. |
| AU | IDs: stereo 331432980, mid 983783012, side 1410319923. Se comprueba el readback al escribirlos mediante AudioUnitSetParameter. |
| Unidades AU | Los continuos son normalizados 0..1 en la API; los value strings muestran porcentajes/dB. El host de test convierte las unidades correctamente. Esta diferencia no es un defecto del plugin. |
| Lectura | `PluginProcessor::processBlock()`: cada raw DRIVE se divide por 100 una sola vez y entra en su MasterSettings::banks[i]. |
| Smoothing | `MasterChannelCore::process()`: objetivo `shapeIntensity(drive,1.75)` y ramp de 20 ms a 4x; no se reinicia si el objetivo no cambia. |
| Algoritmo | `driveResidual()`: devuelve un residual no nulo cuando hay señal e intensidad positiva; se mide en los tres bancos. |
| Detectores | Ataque rápido 0.5 ms / release 20 ms; lento 15 ms / release 80 ms. No se usan como mute ni gate. |
| Mezcla | Residual alineado: `(tanh(k*x)/k - x) * w`; el seco no se elimina ni se normaliza. |
| Suma | `process()`: `coloured = harmonicInput + exciter`, salida `(coloured + drive) * trim`. No se descarta el residual. |
| Routing | `MasterEngine::process()`: cores STEREO, MID y SIDE independientes; en M/S ambos se procesan y reconstruyen. `editBank` no lo lee el DSP. |
| Bypass/estado | Los ensayos fuerzan bypass off y trim 0. Prepare arranca un ramp de 20 ms; se mide después de 500 ms. La automatización, estado y cambio de edición tienen regresiones. |

En R1, con d=DRIVE/10, I=d*(0.38+0.62*d*d), k=1+5*I,
protección=0.85*(1-I)*transient, w=0.72*I*(1-protección).
La protección deja al menos el 15 % de la mezcla prevista y a DRIVE 10 vale cero:
no puede anular completamente el máximo. Para señales pequeñas,
`tanh(k*x)/k ≈ x-k²*x³/3`: la diferencia relativa disminuye con el cuadrado del nivel.
La división por k cancela la ganancia lineal, no el contenido no lineal.

## 3. Matriz reproducible y cifras

[CSV completo](R11-drive-baseline.csv): 648 filas, 3 bancos × 3 rates × 3 señales × 4 niveles × 6 controles.
Rates: 44.1, 48 y 96 kHz. DRIVE: 0, 1, 3, 5, 7 y 10.
HARMONICS/EXCITER apagados; trim 0; bypass off. Warm-up 500 ms y ventana medida 100 ms.

Los niveles -24/-18/-12/-6 dBFS son **picos de la componente de prueba**, no RMS.
El seno tiene RMS 3.01 dB menor. En M/S el nivel se define antes de decodificar a L/R:
la componente seleccionada lleva el test y la otra un seno 1300 Hz a amplitud 0.45 veces.
STEREO también utiliza dos canales distintos. Se incluyen impulsos cada 10 ms y ataques
sintéticos cada 20 ms. No se presenta ese ataque sintético como un archivo de batería real.

Se miden RMS/picos entrada/salida, delta máximo y RMS frente al neutro con igual latencia,
delta relativo, crest factor y armónicos. THD aquí incluye H3/H5/H7/H9, no todos los bins.
Las columnas armónicas solo tienen significado para el seno; en impulsos/ataques son placeholders 0.

Ejemplo STEREO, 48 kHz, seno 1 kHz, pico -18 dBFS (entrada RMS 0.08901947, pico 0.12589253):

| DRIVE | RMS salida | Pico salida | Delta máximo vs 0 | H3 dBc | THD 3/5/7/9 % | Crest salida dB |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0.08901947 | 0.12589255 | 0 | -163.84 | 0.00000383 | 3.0103 |
| 1 | 0.08900703 | 0.12586913 | 0.00002342 | -86.67 | 0.004640 | 3.0099 |
| 3 | 0.08893823 | 0.12573986 | 0.00015269 | -70.39 | 0.030221 | 3.0077 |
| 5 | 0.08868683 | 0.12526889 | 0.00062366 | -58.18 | 0.123258 | 2.9997 |
| 7 | 0.08777379 | 0.12356940 | 0.00232315 | -46.75 | 0.459609 | 2.9709 |
| 10 | 0.08135884 | 0.11184987 | 0.01404268 | -30.79 | 2.892428 | 2.7646 |

A DRIVE 10, la diferencia RMS relativa al neutro es 9.039 % en ese caso.
MID y SIDE producen las mismas cifras dentro de redondeo float para su componente.
A -24 dBFS, DRIVE 5 solo da THD 0.031875 %, y DRIVE 10 da 0.822161 %.
A -6 dBFS, DRIVE 10 da 12.75651 %. Se demuestra la fuerte dependencia del nivel;
no se certifica audibilidad o aprobación musical a partir de un porcentaje.

Transitorios a 48 kHz/-18 dBFS, picos después del mismo sobremuestreador:

| Señal | DRIVE 0 | DRIVE 3 | DRIVE 5 | DRIVE 7 | DRIVE 10 |
|---|---:|---:|---:|---:|---:|
| Impulso | 0.07915626 | 0.07913800 | 0.07906840 | 0.07875132 | 0.07548084 |
| Ataque sintético | 0.14952746 | 0.14934850 | 0.14875902 | 0.14646949 | 0.12866971 |

El overshoot del ataque neutro ya está presente en el sobremuestreo R1/MIX; no se modifica.
No existe aquí una reducción excesiva añadida por DRIVE moderado: también en ataques es débil.

## 4. Cambios y resultados de pruebas

Cambios de esta fase: `Tests/DriveTests.cpp`, tres líneas de CMake para target/CTest,
este informe y el CSV. DSP, procesador, editor, recursos, parámetros, IDs y versiones intactos.
Se conservan los tests existentes. Snapshot previo de Source/Tests/CMake:
`/private/tmp/harmohit-master-r11.UXXlFs/R1-*`.

- CTest completo: 2/2 aprobado, última ejecución 7.74 s.
- 63 equivalencias HARMONICS/EXCITER con MIX conservadas y aprobadas (error máximo 0).
- Matriz independiente del editor: 648 casos, diferencias progresivas; H3 por encima del
  suelo numérico y THD medida al máximo. No solo se exige que los samples sean finitos.
- Mando/attachments: seleccionan y escriben DRIVE en los tres bancos sin alterar los otros.
- AU instalado: las mismas 648 mediciones, CSV igual; comparación de 4.062.960 muestras
  de las componentes seleccionadas contra el código: error máximo 0.
- Automatización de DRIVE y modo sobre DC: paso máximo 0.000109836 (límite 0.001).
  Es un ensayo de discontinuidad, no una afirmación de ausencia de clics en toda música.
- Cambiar editBank durante ese audio: igualdad bit a bit frente a edición fija.
- Restauración de los tres DRIVE: valores idénticos; estado completo se cubre en suite R1.
- Tests existentes NaN/Inf, bloques grandes y conteo de new en RT siguen aprobados;
  no se ha realizado una captura de todas las asignaciones de sistema ni ThreadSanitizer.
- AU y VST3 recompilados Release arm64, todavía **versión 0.1.0 / DSP R1**:
  `/private/tmp/harmohit-master-r11.UXXlFs/build/VitaPlugHarmoHitMasterR1_artefacts/Release`.
- `auval -v aufx HmR1 Vtpl`: AU **instalado R1** aprobado, código 0.
  No se atribuye ese resultado al bundle temporal. El intento de registrar su fábrica
  aisladamente devolvió un componente nulo; se retiró ese camino no fiable del harness.
  Validar con auval cualquier futuro AU corregido sigue pendiente de autorización de instalación.
- La compilación del host diagnóstico tiene un warning de nullability en una llamada Apple;
  el componente se comprueba no nulo antes de instanciarlo. No es un warning del DSP.

SHA-256 del ejecutable instalado, sin cambios:
`0e83c28acb5ed3ecdcae922ae356c07035b1433823aaa18c6ff3767d5001f92f`.
SHA-256 CSV común: `4383fea163bd521ee537776a284afb13e87ec2a585cddc296919fad091e31f0a`.
La versión 0.1.0 por sí sola no identifica una compilación; por eso se comprueba el hash.
No se instaló ni sobrescribió ningún AU; no se abrió Logic ni se alteraron proyectos.
MIX y Gainer80 no se editaron. No hay commits, pushes ni Releases nuevos.

## 5. Reproducir las pruebas

En el build temporal indicado:

```sh
/private/tmp/harmohit-master-r11.UXXlFs/build/HarmoHitDriveTests --dsp-only
/private/tmp/harmohit-master-r11.UXXlFs/build/HarmoHitDriveTests --au
/private/tmp/harmohit-master-r11.UXXlFs/build/HarmoHitDriveTests --compare-au --dsp-only
ctest --test-dir /private/tmp/harmohit-master-r11.UXXlFs/build --output-on-failure
```

`--au` y `--compare-au` requieren el AU R1 registrado en macOS. No aceptan una ruta
de bundle como prueba de haber cargado esa compilación. `--csv=/ruta/salida.csv` guarda cifras.
CTest normal no depende de que haya un plugin instalado. El build temporal puede limpiarlo macOS;
el código de tests, CSV e informe permanecen en el proyecto.

## 6. Siguiente smoke y datos imprescindibles

Sin instalar una supuesta reparación y usando una sesión de prueba nueva del usuario:

1. Una pista estéreo, MASTER como único efecto, sin copias dry/aux paralelas para esta prueba.
2. Modo STEREO; H/E 0; trim 0; bypass off. Desactivar automatización de DRIVE solo en esa prueba.
3. Seno 1 kHz de pico -18 dBFS: el VU RMS corresponderá aproximadamente a -21.01 dBFS.
4. Alternar DRIVE 0 y 10. En los ensayos el pico pasa de 0.125893 a 0.111850 y H3 queda en
   -30.79 dBc; comprobar el audio, no solo el dibujo del mando.
5. Verificar en controles genéricos de Logic que `stereo drive` muestra 100 % al máximo
   y permanece allí durante reproducción. En M/S, verificar el parámetro del banco activo.
6. Repetir con el material que reveló el fallo. Facilitar nivel INPUT y dos bounces cortos,
   uno a DRIVE 0 y otro a 10, sin compensación/normalización, más estado de la instancia
   o capturas de modo, parámetros y automatización. No hace falta alterar el proyecto original.

Con esos datos se puede distinguir nivel insuficiente de alimentación, automatización/estado,
routing de la sesión o una diferencia específica de Logic. Son hipótesis abiertas, no causas
atribuidas al usuario. Si se confirma que el problema de producto es sensibilidad insuficiente,
la calibración se medirá con esta misma matriz y límites de transitorios antes/después.
Hasta entonces **no se declara reparado DRIVE ni validada R1**.
