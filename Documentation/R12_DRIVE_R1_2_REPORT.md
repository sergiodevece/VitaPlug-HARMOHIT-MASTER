# HARMOHIT MASTER — R1.2: DRIVE adaptativo (iteración única)

Fecha: 14 de octubre de 2026. Versión candidata R1.2.
**No instalada, sin commit ni push.** Pendiente del smoke físico con nivel igualado (§8).

## Resumen

R1.2 sustituye el DRIVE de R1 por la arquitectura adaptativa de la auditoría R12. Es una saturación tanh con rodilla referida al RMS del programa, una pequeña componente de segundo armónico y una nueva protección de ataques. No hay detector de transitorios de R1, ni ganancia, ni normalización.

Cumple los criterios técnicos planteados:
- Es audible desde intensidades moderadas.
- Responde igual de −24 a −6 dBFS RMS.
- Conserva los ataques aislados con menos de 0,8 dB de reducción a DRIVE 5. El prototipo perdía 4,7 dB.
- SIDE pasa a ser útil, sin bombeo, sin elevar ruido y sin alias apreciable.

Hay tres matices importantes:
- **DRIVE 10 no es más intenso que R1 al máximo.** Tras igualar nivel, el residuo es similar (−14 dB), pero con menos destrucción de picos y con segundo armónico.
- **El coste de CPU es el doble que en R1:** 2,8 % de un núcleo a 48 kHz.
- **auval no se ha podido ejecutar** sobre R1.2 sin instalar.

La decisión final depende de la escucha.

---

## 1. Arquitectura implementada

Archivo nuevo: `Source/DSP/AdaptiveDrive.h`. Por núcleo (STEREO enlazado L/R; MID y SIDE independientes), a la frecuencia sobremuestreada 4×:

```
R    = sqrt(media cuadrática |x|²)       ataque 60 ms / liberación 400 ms, suelo −60 dBFS
G    = memoria de programa lenta          media cuadrática 350 ms, suelo −60 dBFS
Pk   = envolvente de pico                 ataque instantáneo, liberación 150 ms
s    = smoothstep((20·log10(Pk/G) − 16 dB) / 8 dB)      0 en programa denso, 1 en arranques
T    = max( R · 10^(H(d)/20) ,  s · Pk / U(d) )          rodilla
wet  = T · ( tanh(x/T) + β(d) · HP10Hz(tanh(x/T)²) )
y    = x + w(d) · (wet − x)
```

Ley de control (d = DRIVE/10):

| Parámetro | Fórmula | Rango |
|---|---|---|
| H(d), rodilla sobre el RMS | 20 − 15·d | 20 → 5 dB |
| w(d), cantidad en paralelo | 0,9·√d | 0 → 0,9 |
| β(d), segundo armónico | 0,2·d | 0 → 0,2 |
| U(d), límite de \|x\|/T en arranques | 0,45 + 1,15·d^1,5 | 0,45 → 1,6 |

Propiedades por construcción:
- **Pendiente de pequeña señal exactamente 1.** Mover la rodilla no cambia la ganancia y no puede producir bombeo.
- **T es continua en el tiempo.** Todos los detectores son de un polo y Pk sigue |x| de forma continua.
- **Cero latencia añadida.** No hay lookahead. Se conservan las 6 muestras del sobremuestreo IIR 4× de R1 a 48 kHz.
- **DRIVE 0 devuelve un residuo de exactamente 0.** Los detectores siguen funcionando para estar listos al subir DRIVE.

## 2. Cambios en el código

| Archivo | Cambio |
|---|---|
| `Source/DSP/AdaptiveDrive.h` | **Nuevo.** Motor R1.2 completo. |
| `Source/DSP/MasterChannelCore.h` | Se elimina `driveResidual` de R1, con sus envolventes rápida y lenta y la ley de protección (1 − I). DRIVE recibe el control 0–1 sin la ley `shapeIntensity`. El bucle calcula primero el color de todos los canales y después DRIVE por trama, para enlazar el detector L/R. HARMONICS, EXCITER, RANGE, PUSH ALL, trim, sobremuestreo y suavizado de 20 ms quedan sin tocar. |
| `Tests/DriveTests.cpp` | Nuevas regresiones R1.2 a 44,1, 48 y 96 kHz: ataque aislado, independencia de nivel, color audible, SIDE sobre mono, colas sin elevación de ruido e independencia del tamaño de bloque. Se conservan todas las pruebas anteriores. |
| Sin cambios | `MasterEngine.h`, `PluginProcessor.*`, `PluginEditor.*`, parámetros e IDs, `CMakeLists.txt`, MIX y Gainer80. |

Auditoría y comparación en `Documentation/R12-audit/`:
- `tools/r12_compare.cpp` compara MIX, R1 y R1.2. MIX se compila desde su código sin modificar y R1 desde una copia congelada en `tools/r1_frozen/`.
- Salidas en `r12_compare_{44100,48000,96000}.txt`.
- `tools/bar_pairs.cpp`, `tools/logic_bounce.cpp` y `tools/bench.cpp`.

La herramienta de la auditoría R12 apunta ahora a R1 congelado y reproduce su salida idéntica.

## 3. Ataques aislados: problema y solución

El prototipo A2 dependía solo de R. Tras un silencio, R está en el suelo, la rodilla queda muy baja y el primer golpe se aplasta: −4,7 dB en un impulso a DRIVE 5.

La solución añade el término `s · Pk / U(d)`:
- Pk sube en el mismo instante que el golpe, así que la rodilla también sube y limita |x|/T a U(d). La reducción del pico queda acotada a w·(1 − tanh U / U) sin lookahead.
- El término solo actúa cuando el pico supera en más de 16–24 dB la memoria lenta del programa G: silencio, pausa, pasaje suave o subida brusca. En música densa, s ≈ 0 y el comportamiento es el de la rodilla RMS.

**Hallazgo de esta iteración.** La primera versión medía la cresta contra R, de 60 ms. Con un golpe fuerte, R sube en 1–3 ms y cierra la protección antes de que termine el ataque. Un bombo tras silencio perdía 3,3 dB a DRIVE 5. Medir la cresta contra G, de 350 ms, mantiene la protección durante el ataque y la libera hacia el cuerpo del golpe. Fue la única corrección estructural. Se hizo dentro de la arquitectura autorizada y antes de fijar la versión candidata.

Resultados a 48 kHz. El ataque es el pico en los primeros 5 ms; el cuerpo, la energía de 20 a 200 ms. Ambos frente a DRIVE 0 del mismo procesador.

| Escenario | R1.2 D3 | R1.2 D5 | R1.2 D7 | R1.2 D10 | R1 D10 |
|---|---:|---:|---:|---:|---:|
| Impulso −6 dBFS tras 2 s de silencio | −0,36 | −0,73 | −1,35 | −2,81 | −3,42 |
| Impulso −20 dBFS tras silencio | −0,37 | −0,75 | −1,37 | −2,85 | −0,27 |
| Bombo −6 tras silencio | −0,38 | **−0,77** | −1,42 | −2,97 | −5,96 |
| Caja, subida 0,3 ms, tras silencio | −0,17 | −0,33 | −0,62 | −1,38 | −4,42 |
| Bombo tras 1 s de pausa en música | −0,39 | −0,80 | −1,47 | −2,99 | −5,77 |
| Bombo tras 250 ms de pausa | −0,29 | −0,66 | −1,42 | −3,89 | −5,72 |
| Caja tras música suave (−42 RMS) | −0,19 | −0,30 | −0,49 | −1,01 | −3,99 |
| Salto de nivel −36 → −14 dBFS RMS | −0,39 | −0,80 | −1,47 | −3,06 | −7,55 |
| Bucle de batería denso (no aislado) | −0,49 | −1,11 | −2,26 | −5,43 | −5,92 |

El cuerpo cambia como mucho −0,24 dB a DRIVE 5 y entre −1,8 y −0,9 dB a DRIVE 10. La saturación entra en el cuerpo y respeta el ataque.

Seguridad:
- **Colas de reverberación con ruido decreciente:** la salida nunca supera a la entrada. La diferencia está entre +0,00 y −0,04 dB por debajo de −40 dBFS, sin elevación de ruido.
- **Cola de baja frecuencia del término par tras un impulso:** −100 dBFS a DRIVE 5 y −90 dBFS a DRIVE 10.
- **Bombeo:** la relación de nivel en ventanas de 20 ms frente a DRIVE 0 no supera +0,09 dB a DRIVE 5 ni +0,45 dB a DRIVE 10. Solo hay reducciones en los picos, nunca subidas de nivel.

## 4. Comparativa cuantitativa: MIX, R1 y R1.2

Material de Logic: los 5 compases del bounce, reconstruidos sin procesar (§6), a 48 kHz.
Columnas: residuo frente a DRIVE 0; residuo tras igualar nivel por mínimos cuadrados (LM); cambio de nivel; cambio de cresta; cambio del 0,1 % de picos superiores.

A −24 dBFS RMS. R1 y MIX dependen del nivel: estos son sus valores a este nivel.

| DRIVE | Proc. | Residuo | LM | Nivel | ΔCresta | ΔPicos |
|---:|---|---:|---:|---:|---:|---:|
| 3 | MIX | −6,7 | −20,1 | +3,24 | −1,51 | +1,89 |
| 3 | R1 | −48,1 | −52,1 | −0,03 | −0,05 | −0,07 |
| 3 | **R1.2** | **−32,4** | **−35,3** | −0,15 | −0,26 | −0,35 |
| 5 | MIX | +3,9 | −11,4 | +8,30 | −4,33 | +3,92 |
| 5 | R1 | −36,0 | −40,2 | −0,11 | −0,20 | −0,28 |
| 5 | **R1.2** | **−24,8** | **−27,6** | −0,35 | −0,56 | −0,82 |
| 7 | R1 | −25,0 | −29,5 | −0,40 | −0,63 | −0,95 |
| 7 | **R1.2** | **−18,7** | **−21,4** | −0,72 | −0,93 | −1,74 |
| 10 | MIX | +21,1 | −4,2 | +23,74 | −6,55 | +10,23 |
| 10 | R1 | −10,9 | −14,7 | −2,16 | −3,39 | −5,31 |
| 10 | **R1.2** | **−11,6** | **−14,1** | −1,71 | −1,96 | −4,47 |

Dependencia de nivel con el mismo material, residuo igualado en nivel a DRIVE 5:
- R1: −40,2 dB a −24 dBFS RMS y −33,5 dB a −18 dBFS RMS. Sigue subiendo con el nivel.
- R1.2: −27,6 dB a −24, −18, −12 y −6 dBFS RMS. Idéntico.

Con el bucle de la grabación A/B anterior, R1.2 da −39,7, −31,6, −25,0 y −17,1 dB igualados en nivel a DRIVE 3, 5, 7 y 10, también idéntico en los cuatro niveles.

Seno de 1 kHz. R1.2 es igual a −24, −12 y −6 dBFS de pico; R1 y MIX a −12 dBFS de pico:

| DRIVE | R1.2 THD | R1.2 H2 / H3 dBc | R1 THD (−12) | MIX THD (−12) |
|---:|---:|---:|---:|---:|
| 3 | 0,31 % | −51 / −57 | 0,11 % | 2,9 % |
| 5 | 0,91 % | −42 / −49 | 0,44 % | 11,0 % |
| 7 | 2,07 % | −35 / −41 | 1,50 % | 22,7 % |
| 10 | 5,61 % | −26 / −31 | 7,80 % | 38,0 % |

R1.2 aporta segundo armónico, H2 por encima de H3. R1 y MIX solo producían impares.

Aliasing a DRIVE 10, contenido no armónico con tonos de −6 dBFS:

| Tono | R1.2 | R1 | MIX |
|---:|---:|---:|---:|
| 7 kHz | −100 dBc | −112 dBc | −37 dBc |
| 11 kHz | −100 dBc | −80 dBc | −28 dBc |
| 14 kHz | −100 dBc | −62 dBc | −23 dBc |

Energía del residuo por encima de 18 kHz con música: R1.2 −33 dB, R1 −31 dB, MIX +1 dB.

## 5. STEREO, MID y SIDE

Residuo relativo a la componente procesada. La fuga a la otra componente queda por debajo de −130 dB en todos los casos.

| Material (Mid / Side RMS) | Banco | R1 D5 | R1 D10 | R1.2 D3 | R1.2 D5 | R1.2 D10 |
|---|---|---:|---:|---:|---:|---:|
| Mezcla real (−17,9 / −38,5) | MID | −28,8 | −7,4 | −32,1 | −24,6 | −11,5 |
| Mezcla real | SIDE | −63,1 | −32,6 | −32,8 | **−25,4** | −12,4 |
| Independiente: Mid seno, Side batería (−15,0 / −35,7) | SIDE | −65,0 | −34,5 | −36,6 | −28,9 | −14,7 |
| Side muy bajo (−17,9 / −68,9) | SIDE | −113,9 | −92,5 | −38,3 | −30,4 | −15,7 |

- **SIDE pasa de inoperante a útil.** Actúa a la misma escala que MID, porque cada banco se refiere a su propio nivel.
- **Side muy bajo:** procesamiento relativo similar al de un Side normal. Por debajo del suelo de −60 dBFS el efecto se atenúa, sin elevar ruido.
- **Estabilidad:** un programa mono con DRIVE 10 en SIDE sale exactamente mono, con diferencia L − R = 0 bit a bit, a 44,1, 48 y 96 kHz.

## 6. Mediciones con los WAV de Logic

Hallazgo previo: en ambos bounces, los compases 1, 3 y 5 contienen el mismo audio fuente, y también los compases 2 y 4. El compás 1 del bounce de DRIVE está a DRIVE 0. El compás 2 está a DRIVE 30 en R1, a −44 dB del original, y sirve como aproximación sin procesar. Con esto se reconstruye la entrada de los 5 compases.

**Validación del modelo.** R1 simulado con la automatización 0/30/50/80/100 reproduce el bounce real de Logic:

| Comparación | Modelo R1 | Logic real |
|---|---:|---:|
| DRIVE 50 vs 0, residuo (LM) | −32,7 (−37,1) dB | −32,1 (−36,6) dB |
| DRIVE 100 vs 0, residuo (LM) | −9,1 (−12,9) dB | −8,9 (−12,7) dB |
| DRIVE 100 vs 0, cambio de nivel | −2,75 dB | −2,85 dB |

**Predicción para R1.2 con la misma automatización** (residuo, LM y nivel por compás):

| Compás | DRIVE | Residuo | LM | Nivel |
|---:|---:|---:|---:|---:|
| 2 | 30 | −32,2 | −35,0 | −0,15 |
| 3 | 50 | −25,1 | −27,6 | −0,32 |
| 4 | 80 | −16,0 | −18,6 | −0,97 |
| 5 | 100 | −11,9 | −14,1 | −1,57 |

**Bounce de HARMONICS, como referencia.** Entre compases con la misma fuente, igualados en nivel: 50 frente a 0 da −11,3 dB con +4,5 dB de nivel, y 100 frente a 0 da −6,9 dB con +8,4 dB. Hay 7, 331 y 744 muestras recortadas a 0 dBFS en los compases 3 a 5. Ese residuo es sobre todo cambio tonal de los estantes, no saturación, así que no son magnitudes equivalentes.

**Automatización y continuidad:**
- Escalón de DRIVE 0 → 10 sobre DC: salto máximo de 0,00004.
- En los cambios de compás, la segunda diferencia máxima automatizada coincide con la de DRIVE fijo dentro del 3 %. No hay clics.
- Con ajustes constantes, la salida es idéntica bit a bit con bloques de 1, 37, 64, 512 y 4096 muestras.

## 7. Pruebas ejecutadas y problemas pendientes

| Prueba | Resultado |
|---|---|
| CTest (`HarmoHitMasterTests`, `HarmoHitDriveTests`) | 2/2 aprobado |
| 63 equivalencias HARMONICS/EXCITER frente a MIX, por modo y frecuencia | Aprobado, error 0 |
| Matriz R11 de 648 casos y automatización, estado y banco en edición | Aprobado; salto DC máximo 2,3·10⁻⁵ |
| Regresiones R1.2 a 44,1, 48 y 96 kHz | Aprobado. Bombo tras silencio a DRIVE 5: −0,75, −0,75 y −0,78 dB |
| Compilación Release arm64 de AU y VST3 | Correcta |
| Identificadores AU | aufx / HmR1 / Vtpl, `com.vitaplug.harmohit.master.r1`, idénticos a R1 |
| VST3 R1.2 cargado junto al VST3 de MIX en el host de pruebas | Aprobado; editor 760×380 |

Hashes SHA-256 de los binarios:
- AU: `e9117cbf91f599c7a01e752ee38fcc4eea02ec7172c3564c03fbccaa837f6640`
- VST3: `a7324079fba03813c178e1ee77366e888904907b759a8b13aca5bee27a8fec53`

Bundles en `<build temporal>/VitaPlugHarmoHitMasterR1_artefacts/Release/`. Es una ubicación temporal: hay que recompilar si macOS la limpia.

Problemas pendientes, sin ajustar en esta iteración:
1. **auval no ejecutado sobre R1.2.** Solo valida componentes registrados; requiere instalar. Primer paso del smoke.
2. **DRIVE 10 no es más intenso que R1 al máximo** tras igualar nivel: −14,1 frente a −14,7 dB. Es menos destructivo con los picos (−4,5 frente a −5,3 dB) y aporta H2. Si la escucha pide más agresividad en el tramo alto, eso requiere cambiar la ley y no se ha hecho.
3. **Bucle de batería denso a DRIVE 5: −1,1 dB en el ataque.** No son golpes aislados, así que la protección no actúa por diseño.
4. **CPU ×2 respecto a R1:** 2,8 % de un núcleo a 48 kHz, con los tres núcleos siempre activos. Es optimizable sin cambiar el sonido en una iteración de mantenimiento.
5. **La versión sigue en 0.1.0.** Solo el hash distingue R1 de R1.2. Las sesiones guardadas con R1 sonarán distinto con los mismos valores de DRIVE.
6. **Las pruebas a 44,1 y 96 kHz con música** reproducen el material de 48 kHz muestra a muestra. Las señales sintéticas y los senos sí son correctos para cada frecuencia y coinciden entre frecuencias.

## 8. Smoke físico

1. **Instalar** el AU de R1.2 sustituyendo al de R1, con autorización expresa. Ejecutar `auval -v aufx HmR1 Vtpl` y comprobar el hash de §7.
2. **Misma sesión de Logic, MASTER en Stereo Out** y la misma automatización de `stereo drive` 0/30/50/80/100 por compás. Hacer el bounce a `harmohit_drive_R12_por_compas.wav` en 24 bits a 48 kHz.
3. **Analizar** con:
   ```sh
   c++ -std=c++20 -O2 Documentation/R12-audit/tools/bar_pairs.cpp -o /tmp/bar_pairs
   /tmp/bar_pairs evidencias/harmohit_drive_R12_por_compas.wav
   ```
   Valores esperados según §6:

   | Pareja de compases | Residuo esperado | Cambio de nivel |
   |---|---:|---:|
   | 1–3 | −25 dB | −0,3 dB |
   | 1–5 | −12 dB | −1,6 dB |

   Una desviación de más de 2 dB indica que la instalación o la sesión no corresponden a R1.2.
4. **Escucha con nivel igualado.** DRIVE baja ligeramente el nivel: −0,35 dB a 5 y unos −1,7 dB a 10. Igualar con OUTPUT TRIM manualmente antes de comparar, nunca de forma automática. Comparar a ciegas DRIVE 0 frente a 5 y frente a 10, y DRIVE 5 en R1.2 frente a R1 si se conserva una copia del bounce anterior.
5. **Repetir en modo M/S** con DRIVE solo en SIDE a 5 y 10. Escuchar anchura y densidad del Side, y comprobar que el Mid no cambia.
6. **Material con pausas o arranques en seco.** Comprobar a DRIVE 5 y 7 que el primer golpe conserva el ataque.

Criterio de decisión: si la escucha con nivel igualado confirma densidad y carácter útiles a DRIVE 5–7 y ataques intactos, R1.2 se acepta. Si no, el concepto adaptativo se descarta según la decisión de producto, sin más reajustes.

---

## 9. Instalación local para el smoke (14‑oct‑2026)

| Paso | Resultado |
|---|---|
| Fuentes compiladas | Hashes de `AdaptiveDrive.h` `c3c46087…` y `MasterChannelCore.h` `b6bd08b3…`, los mismos de las pruebas finales. La compilación incremental no recompiló nada. |
| Binario probado (firma del enlazador) | `e9117cbf91f599c7a01e752ee38fcc4eea02ec7172c3564c03fbccaa837f6640` |
| Copia de seguridad de R1 | `<VITAPLUG>/backups/HARMOHIT_MASTER_R1_AU_backup_20261014/VitaPlug HARMOHIT MASTER R1.component`, con hash `0e83c28a…f92f` idéntico al instalado, árbol idéntico y `README_R1.txt` con instrucciones de restauración. |
| Logic | Cerrado de forma ordenada antes de sustituir el componente. |
| Firma | El bundle compilado solo tenía la firma del enlazador y no pasaba `codesign --verify --strict` ("code has no resources"). R1 estaba firmado ad hoc con los recursos sellados. Se aplicó `codesign --force --deep --sign -` al artefacto validado, como en R1. |
| Equivalencia tras firmar | Las 28 secciones Mach-O de código y datos son idénticas al binario probado. Solo cambia el bloque de firma. |
| Binario instalado (firmado) | `a7cdd541607fc4152fcc524dd359f259b58193aafd31b83c17e6cf5a3f1707bf`, árbol idéntico byte a byte al artefacto firmado. |
| Identidad | aufx / HmR1 / Vtpl, versión 0.1.0 (0x100), `com.vitaplug.harmohit.master.r1`. `codesign --verify --deep --strict` correcto. |
| auval | `auval -v aufx HmR1 Vtpl`: AU VALIDATION SUCCEEDED. |
| AU registrado frente al código R1.2 | `HarmoHitDriveTests --compare-au`: 4.062.960 muestras, error máximo 0. |
| Otros componentes | Sin cambios. MIX, sus copias antiguas y Gainer80 siguen igual. En Components solo queda un bundle de MASTER. |
