# HARMOHIT MASTER — Auditoría DSP independiente del DRIVE (R12)

Fecha: 14 de octubre de 2026. Fase exclusivamente de auditoría.
No se ha modificado DSP, interfaz, parámetros, MIX ni Gainer80. No se ha instalado nada. Sin commits.

Material de la auditoría (todo reproducible, ver §11):

| Archivo | Contenido |
|---|---|
| `Documentation/R12-audit/tools/audit_drive.cpp` | Banco de medidas. Compila **sin cambios** `HarmoCore.h` de MIX y `MasterEngine.h`/`MasterChannelCore.h` de MASTER. |
| `Documentation/R12-audit/tools/ProtoDrive.h` | Prototipo de la alternativa A. **Solo auditoría, no es código de producto.** |
| `Documentation/R12-audit/tools/wav_ab.cpp`, `wav_repeats.cpp` | Análisis del WAV sin dependencias. |
| `Documentation/R12-audit/audit_output.txt` | Salida completa E0–E11. |
| `Documentation/R12-audit/wav_ab_output.txt`, `wav_repeats_output.txt` | Salidas del análisis del WAV. |

---

## 1. Resumen ejecutivo

1. **El WAV no contiene ningún cambio de estado de procesamiento.** Es un bucle de 2 compases a 112 BPM repetido 7 veces. Alineando cada repetición con su desplazamiento entero correcto, las 6 repeticiones coinciden con la primera entre −123 y −138 dB (95–99,7 % de muestras idénticas). Cualquier paso por HARMOHIT MASTER, incluso con todo a cero o con su parámetro de bypass, difiere de la señal seca en −14,7 dB por la fase del sobremuestreo IIR. Por tanto el archivo no puede contener una transición bypass → MASTER activo. **No es evidencia de que DRIVE no funcione; es evidencia de que la captura no registró el cambio.**
2. **El DRIVE de MASTER sí actúa, y a DRIVE 10 sobre ese mismo material es claramente medible.** Al nivel nativo del WAV (RMS −24,9 dBFS) produce un residuo de −13,8 dB, baja los picos 3,7 dB y el RMS 1,7 dB. Eso es audible y visible en un medidor. La ausencia total de efecto a DRIVE 10 en el banco STEREO o MID no se explica por el DSP.
3. **Pero el diseño actual es musicalmente inadecuado, por razones estructurales:**
   - La rodilla es absoluta (≈ 1/k en dBFS). En DRIVE 5 está a −7,4 dBFS, por encima de prácticamente todas las muestras de una mezcla a −25 dBFS RMS. Residuo a DRIVE 5: −40 dB. El recorrido útil se concentra en el último 30 % del mando.
   - La respuesta depende 16–20 dB del nivel de entrada para un mismo DRIVE.
   - El "detector de transitorios" no detecta transitorios: da el mismo valor medio (0,125) con un seno estacionario que con música. Funciona como una reducción casi constante del 5–9 % de la mezcla, se anula a DRIVE 10 y actúa sobre todo donde la saturación ya es despreciable.
   - En M/S el banco SIDE es prácticamente inoperante: con un Side a −41 dBFS, DRIVE 10 da −37 dB relativo, unos −78 dBFS absolutos.
4. **El DRIVE de MIX no es "saturación" en sentido estricto:** su pendiente de pequeña señal es 1 + 12·A², hasta +31,5 dB de ganancia a DRIVE 10, seguida de un tanh muy duro. Su carácter evidente procede sobre todo de ganancia y recorte, con aliasing de hasta −28 dBc. MASTER eliminó esa ganancia al dividir por k, y con ella casi todo el efecto audible.
5. **Recomendación única:** no gastar la iteración en reajustar constantes del diseño actual. Usarla para sustituir el núcleo de DRIVE por una **saturación con rodilla referida al nivel de programa** (alternativa A). Esta conserva sobremuestreo, topología de residuo, parámetros, UI, HARMONICS y EXCITER. El prototipo de auditoría ya cumple la progresión y la independencia de nivel. Le falta resolver los transitorios aislados tras silencio (§9).

---

## 2. Causa raíz: hechos, hipótesis e incógnitas

### Hechos demostrados

| # | Hecho | Evidencia |
|---|---|---|
| F1 | El WAV no contiene ningún cambio de procesamiento en sus 30 s. | `wav_repeats_output.txt`: 6 repeticiones contra la 0, entre −123,0 y −138,0 dB. |
| F2 | Todo paso por MASTER deja un residuo ≥ −14,7 dB frente a la señal seca, incluidos todo a cero y el bypass interno (`processBlockBypassed` también sobremuestrea). | E1. Mejor retardo entero 6 muestras, residuo −14,7 dB. |
| F3 | DRIVE 10 STEREO, sobre el material del WAV a su nivel: residuo −13,8 dB, picos −3,7 dB, cresta −2,2 dB, RMS −1,7 dB. | E3, MASTER, −24 RMS. |
| F4 | El AU instalado es el binario auditado por Codex. | SHA-256 `0e83c28a…f92f`, subtipo `HmR1`, fecha 13‑oct 20:16. |
| F5 | El código compilado reproduce exactamente las cifras de Codex. | E2: THD 2,8924 % a −18 dBFS pico, DRIVE 10. Codex: 2,892428 %. |
| F6 | La réplica de `driveResidual` usada para aislar la protección es fiel. | E0: diferencia máxima 6·10⁻⁸ frente a `MasterEngine` real. |

### Hipótesis, ordenadas por evidencia

1. **H1. La captura A/B no registró la salida del plugin, o el estado no cambió dentro de los 30 s.** Ejemplos: grabación desde un punto previo al insert, bounce de la región, el cambio de bypass fuera del tramo exportado o el mismo estado en ambas mitades. Es la **única** hipótesis compatible con F1 y F2. Cualquier transición real habría dejado como mínimo −53 dB relativos, aunque fuera DRIVE en el banco Side. El WAV muestra −123 dB.
2. **H2. DRIVE se subió en un banco sin peso audible.** En modo M/S con el banco S en edición, DRIVE 10 sobre un Side a −41 dBFS queda unos −78 dBFS absolutos (E7), inaudible. Explica la percepción, no el WAV.
3. **H3. Sensibilidad real insuficiente en el recorrido medio.** Con este material, DRIVE 5 da −40 dB de residuo y DRIVE 7 −29 dB (E3). Si la escucha se hizo en esas posiciones, la ausencia de efecto es esperable. No explica "incluso a máximo" en STEREO o MID.
4. **H4. Estado del host:** automatización en Read que devuelve DRIVE a 0, otra instancia u otra pista. Sin evidencia a favor ni en contra. El binario instalado queda descartado (F4).

Descartado con la evidencia disponible: rama DSP desconectada, parámetro a cero permanente, normalización incorrecta, binario distinto o cancelación del residuo.

### Incógnitas pendientes

- Punto exacto de la señal que se grabó y método de grabación.
- Instante del cambio de bypass y tipo de bypass: el botón de Logic o el parámetro `bypass` del plugin.
- Modo, banco en edición y automatización de la instancia durante la prueba.

**Prueba que cierra la cuestión, sin tocar código:** repetir la captura con un **control positivo**. Durante 4 s se sube HARMONICS a 3, que el usuario ya confirmó audible, y después DRIVE 0 → 10 en modo STEREO. Se analiza el WAV con `wav_repeats`. Si no aparece el escalón de HARMONICS, la captura no está tomando la salida del plugin.

---

## 3. Auditoría del DRIVE de MIX y de MASTER

### 3.1 Recorrido del parámetro (verificado en código)

Mando 0–10 (escala gráfica) → APVTS `stereo|mid|side.drive` 0–100 → `processBlock` /100 → `shapeIntensity(x,1.75)` = 1,75·x(0,38+0,62x²) → rampa de 20 ms a 4× → `driveResidual` divide otra vez por 1,75: I = x(0,38+0,62x²).
El ×1,75 y ÷1,75 es un residuo de MIX. Es inocuo pero confunde. `editBank` no llega al DSP. Codex verificó attachments y AU, y no he encontrado nada que lo contradiga.

### 3.2 MIX (`harmohit/Source/DSP/HarmoCore.h`)

```
A = 1.75 · d(0.38 + 0.62 d²)        g = 1 + 12A
y = x + A · (tanh(g·x) − x)         (bloqueo de DC a 12 Hz, en paralelo con EXCITER)
pendiente de pequeña señal = 1 + A(g − 1) = 1 + 12A²
```

| DRIVE | A | g | Ganancia de pequeña señal |
|---:|---:|---:|---:|
| 3 | 0,229 | 3,7 | +4,2 dB |
| 5 | 0,468 | 6,6 | +11,2 dB |
| 10 | 1,75 | 22 | +31,5 dB |

El techo es ±1,75, unos +4,9 dBFS. MIX DRIVE es esencialmente **ganancia más recorte tanh duro**. En E3 sube el RMS hasta 22 dB y en E6 genera alias de −28 dBc a 11 kHz. El tanh es simétrico, así que solo produce armónicos impares. El bloqueo de DC es superfluo.

### 3.3 MASTER R1 (`Source/DSP/MasterChannelCore.h`)

```
I = d(0.38 + 0.62 d²)     k = 1 + 5I     w = 0.72·I·(1 − p)
p = 0.85·(1 − I)·clamp((envFast − envSlow)/envFast, 0, 1)
y = x + w · (tanh(k·x)/k − x)                   (serie: HARMONICS → EXCITER → DRIVE)
residuo de pequeña señal ≈ −w·k²·x³/3           rodilla efectiva |x| ≈ 1/k
```

| DRIVE | I | k | w (sin p) | Rodilla 1/k |
|---:|---:|---:|---:|---:|
| 3 | 0,131 | 1,65 | 0,094 | −4,4 dBFS |
| 5 | 0,268 | 2,34 | 0,193 | −7,4 dBFS |
| 7 | 0,415 | 3,08 | 0,299 | −9,8 dBFS |
| 10 | 1,000 | 6,00 | 0,720 | −15,6 dBFS |

Diferencias matemáticas esenciales respecto a MIX:

1. **Se eliminó toda la ganancia lineal**, porque tanh(kx)/k tiene pendiente 1. Era la mayor parte de lo que hacía audible a MIX.
2. **La no linealidad depende solo del nivel absoluto.** Con un residuo cúbico, el THD crece unos 2 dB por cada dB de entrada. Entre −24 y −6 dBFS el residuo a DRIVE 5 pasa de −40 a −20 dB (E3).
3. **El efecto neto es compresivo.** El residuo tiene signo opuesto a x, así que el RMS **baja** al subir DRIVE: −1,7 dB a DRIVE 10 sobre el WAV. Quien venga de MIX, donde DRIVE sube hasta +22 dB, puede percibirlo como "no hace nada" o incluso "suena más pequeño".
4. **Solo produce armónicos impares**, H2 por debajo de −200 dBc. No añade la componente par que suele asociarse a "densidad cálida" en mastering.

### 3.4 Detectores y protección de transitorios

- Constantes a 4× la frecuencia base: rápido 0,5 ms / 20 ms y lento 15 ms / 80 ms, sobre |x| instantáneo, sin elevar al cuadrado ni prefiltrar.
- **No discrimina transitorios.** El valor medio de "transient" es 0,125 con un seno estacionario de 1 kHz o 5 kHz, 0,119 a 100 Hz y 0,125 en la música (E9, E8). El envolvente rápido sigue el rizado de la propia forma de onda y el lento no. El cociente es casi constante para cualquier señal periódica.
- **Efecto real:** una reducción casi fija del 5–9 % de la mezcla. Cuesta 3,6 dB de residuo a DRIVE 3, 3,0 dB a DRIVE 5, 2,0 dB a DRIVE 7 y 0 dB a DRIVE 10 (E8).
- **Contradicción de diseño.** Con una curva sin memoria y pendiente 1, las únicas muestras que saturan son los picos. "Proteger los ataques" equivale a proteger lo único que se procesa. El factor (1 − I) además apaga la protección justo donde la saturación es fuerte.
- **Riesgo adicional.** La mezcla se modula a la frecuencia de la señal porque el detector sigue el rizado. Es otra fuente de intermodulación, de nivel bajo.

### 3.5 Mezcla, routing, sobremuestreo y M/S

- **Mezcla dry/wet:** el residuo se suma alineado dentro del mismo dominio 4×. No hay filtrado en peine ni cancelaciones (E0, E2). Correcto.
- **Routing:** `MasterEngine` procesa siempre los tres núcleos. En M/S, `stereo` se descarta mediante `mode`, sin reinicio de filtros. Correcto. HARMONICS y EXCITER aumentan el nivel que entra en DRIVE, porque MASTER los encadena en serie y MIX los aplica en paralelo. Es una diferencia de carácter respecto a MIX, aceptable pero no documentada.
- **Sobremuestreo:** IIR polifásico de media banda 4×, 6 muestras de latencia a 48 kHz y fase no lineal. El plugin "neutro" no es nulo frente a la señal seca: −14,7 dB, solo por fase (E1). Viene de MIX. No afecta al nivel de DRIVE, pero debe tenerse en cuenta en cualquier prueba nula y en el sobreimpulso de ataques que Codex ya señaló.
- **M/S:** con rodilla absoluta, el banco SIDE solo actúa si el Side es fuerte. En este material el Side está a −41 dBFS RMS y el Mid a −21,9. El aislamiento entre componentes es correcto: fuga ≤ −130 dB (E7).

---

## 4. Interpretación crítica del diagnóstico de Codex (R11)

**Demuestra:**
- Que la cadena mando → APVTS → AU → DSP está conectada.
- Que el AU instalado produce las mismas muestras que el código.
- Que DRIVE genera armónicos crecientes y monótonos.

He reproducido sus cifras de seno exactamente (F5).

**No demuestra, o lo interpreta de forma incompleta:**
1. **Nada sobre audibilidad musical.** Midió senos y señales sintéticas en ventanas de 100 ms, con los niveles −24…−6 definidos como **pico**. No probó música real ni una referencia de audibilidad. Mi ancla: EXCITER 3 y 5, confirmados audibles por el usuario, dan −36,6 y −24,2 dB de residuo sobre el WAV (E10). DRIVE 3 y 5 dan −52,7 y −40,4 dB, unos 16 dB menos.
2. **No examinó la protección.** No midió que el detector responde igual a un seno estacionario que a música, ni lo que cuesta.
3. **No evaluó M/S con un Side realista.** Sus pruebas fijan la componente activa al mismo nivel que STEREO, lo que oculta que SIDE es casi inoperante en mezclas reales.
4. **"Puede contribuir al síntoma"** es correcto para DRIVE ≤ 7. Para DRIVE 10 en STEREO o MID no basta, porque el efecto sobre este material es claro (F3). El síntoma "nada a máximo" requería buscar fuera del DSP, como hace este informe con el WAV.
5. **Los 4.062.960 samples con error 0** prueban equivalencia AU = código, no calidad. El PASS de CTest y auval valida integridad, no el objetivo sonoro.
6. **Sus límites de PASS no se justifican musicalmente.** Por ejemplo, "paso máximo 0,001" cubre la discontinuidad, pero ningún criterio exige que DRIVE 5 sea perceptible.

Su honestidad metodológica es buena: no declaró reparación ni validación. El fallo es de alcance, no de rigor numérico.

---

## 5. Análisis del WAV

`evidencias/HARMOHIT_MASTER_DRIVE_AB.wav`: estéreo, 48 kHz, 24 bits, 30,000 s, marcador "Tempo 112".

| Medida | Valor |
|---|---|
| RMS global | −24,88 dBFS |
| Pico | −8,17 dBFS |
| Cresta | 16,7 dB |
| Muestras por encima de −18 dBFS | 6,5 % |
| Muestras por encima de −15 dBFS | 2,6 % |
| Muestras por encima de −12 dBFS | 0,43 % |
| Muestras por encima de −9 dBFS | 0,003 % |
| Mid / Side RMS | −21,9 / −41,1 dBFS |

- **Estructura.** El periodo es de 2 compases: 205714,29 muestras, 4,2857 s. Logic coloca cada repetición en la muestra entera más cercana, de modo que los desplazamientos alternan 205714 y 205715.
- **Por qué el análisis preliminar parecía raro.** Con un retardo fijo, algunos pares aparecen "totalmente distintos" (≈ 0 dB, 0 % idénticos) con RMS y picos idénticos. Es el error de una muestra, no procesamiento. Así se interpretan los tramos de 0 dB en `wav_ab_output.txt`.
- **Con alineación exacta por repetición** (`wav_repeats_output.txt`):

| Repetición | Inicio | Residuo frente a la rep. 0 | Idénticas |
|---:|---:|---:|---:|
| 1 | 4,29 s | −138,0 dB | 99,65 % |
| 2 | 8,57 s | −126,6 dB | 97,79 % |
| 3 | 12,86 s | −123,1 dB | 95,02 % |
| 4 | 17,14 s | −123,0 dB | 94,90 % |
| 5 | 21,43 s | −123,1 dB | 95,16 % |
| 6 | 25,71 s | −123,0 dB | 94,86 % |

  −123 dB relativo a −25 dBFS equivale a unos −148 dBFS, el escalón de cuantificación de 24 bits. Las diferencias de LSB son compatibles con redondeo o dither del render. No son procesamiento.
- **Comparación con lo que debería verse.** Pasar de seco a MASTER activo deja como mínimo −14,7 dB. Pasar del bypass interno a DRIVE 10 deja −14,8 dB. Incluso DRIVE 10 solo en Side dejaría unos −53 dB relativos al total. **Ningún escenario con el plugin en la ruta y un cambio de estado es compatible con −123 dB.**
- **Lo que no se puede saber del archivo:** si todo el WAV es seco o todo procesado en un único estado. La energía entre 23 y 24 kHz (−33,1 dB) no basta para distinguir el paso por el IIR.

---

## 6. Evaluación musical y matemática de la arquitectura actual

Respuestas directas:

**A. ¿Es correcto el planteamiento matemático?**
Es correcto como implementación: estable, alineada, sin alias apreciable (−80 dBc en el peor caso de E6) y con DRIVE 0 transparente respecto a su ruta. Es **inapropiado para el objetivo** por tres razones:
- La rodilla está fijada en dBFS absolutos, y no hay control de entrada en la UI aprobada para llevar la señal hasta ella.
- La ley de intensidad deja casi todo el efecto en el último 30 % del mando.
- La protección de transitorios no protege transitorios. Suprime un 20–35 % del residuo en DRIVE 3–7 y no actúa a DRIVE 10.

**B. ¿Depende demasiado del nivel?**
Sí. Con la misma posición de mando, el residuo varía unos 20 dB entre −24 y −6 dBFS RMS (E3). Con una mezcla a −25 dBFS RMS y picos a −8:

| DRIVE | Residuo | Cambio de picos | Lectura |
|---:|---:|---:|---|
| 3 | −52,7 dB | −0,04 dB | inaudible |
| 5 | −40,4 dB | −0,17 dB | prácticamente inaudible |
| 7 | −29,0 dB | −0,6 dB | sutil |
| 10 | −13,8 dB | −3,7 dB | claro |

En el banco Side de una mezcla normal, inaudible en todo el recorrido.

**C. ¿Hay diferencia real entre 0, 3, 5, 7 y 10?**
Medible sí. Musicalmente, sobre el WAV a su nivel, 0, 3 y 5 son indistinguibles, 7 es sutil y 10 es claro. Hay unos 12 dB de salto de residuo entre 3 y 5, entre 5 y 7, y unos 15 dB entre 7 y 10. La progresión es monótona, pero arranca demasiado abajo. Datos completos para senos, música, batería e impulsos en E2–E5.

**D. ¿Está justificada la complejidad del detector?**
No. No distingue transitorios (E9), su efecto es una atenuación casi constante (E8) y su ley (1 − I) lo desactiva donde haría falta. Una saturación progresiva más sencilla, sin detector, rinde más con menos coste. La protección de ataques debe venir de la posición de la rodilla y de la mezcla, no de modular el residuo a la frecuencia de la señal.

**E. ¿Causa más probable en Logic?**
H1: la captura no registró un cambio de estado (§2), agravada por H2 y H3 en la percepción. **No hay evidencia de fallo de DSP ni de binario.**

---

## 7. Alternativas DSP (máximo dos)

### A. Saturación con rodilla referida al programa (recomendada)

```
P  = media cuadrática de x (ataque 60 ms, liberación 400 ms), enlazada L/R en STEREO;
     un detector por núcleo en M/S
R  = sqrt(max(P, suelo²))                     suelo ≈ −40 dBFS
T  = R · 10^(h(d)/20)                          rodilla h dB por encima del RMS del programa
u  = x / T
wet = T · ( tanh(u) + β(d) · DCblock(tanh(u)²) )
y  = x + w(d) · (wet − x)
```

Prototipo A2 de auditoría: h = 20 → 5 dB, w = 0,9·√d, β = 0,25·d.

- **Fundamento.** Es la misma familia tanh de HARMOHIT con pendiente 1, sin ganancia de compensación y sin normalización. La única diferencia es que la rodilla se mide en dB **por encima del RMS del programa**, no en dBFS. El mando expresa cuánto de la estructura de cresta entra en saturación. No es un compresor: la ganancia de pequeña señal es 1 y no hay elemento de reducción de ganancia. El RMS de salida solo baja lo que recortan los picos (E11: −1,6 dB de cresta a DRIVE 10).
- **Comportamiento medido (A2, E11):**

| DRIVE | Residuo música | Cambio de cresta | Ataque de batería | Cuerpo |
|---:|---:|---:|---:|---:|
| 3 | −36,3 dB | −0,29 dB | −0,38 dB | −0,05 dB |
| 5 | −28,3 dB | −0,73 dB | −0,85 dB | −0,13 dB |
| 7 | −21,7 dB | −1,21 dB | −1,68 dB | −0,29 dB |
| 10 | −13,9 dB | −1,64 dB | −3,94 dB | −0,86 dB |

  La progresión es casi lineal en dB a lo largo del mando y coincide en saliencia con EXCITER en las mismas posiciones.
- **Respuesta al nivel.** Es idéntica de −24 a −6 dBFS RMS mientras el programa esté por encima del suelo. Por debajo se comporta como una rodilla absoluta.
- **Riesgo: colorear igual un pasaje silencioso o un fundido.** El suelo lo limita. En la iteración conviene una referencia híbrida, T ≥ T_abs(d).
- **Transitorios.** Los ataques que superan el RMS en más de h saturan de forma progresiva. Entre DRIVE 0 y 5 se conservan, con −0,85 dB en batería y −0,96 dB en los picos de música. A DRIVE 10 se reducen de 3 a 4 dB sin aplastar.
- **Fallo conocido: transitorios aislados tras silencio.** Un impulso pierde 4,7 dB a DRIVE 5, porque el detector aún no ha subido. Hay que corregirlo en la iteración, con un suelo híbrido o una referencia R = max(RMS, picoLento − c).
- **Carácter armónico.** Impares de tanh más un segundo armónico controlado (H2 −39,7 dBc a DRIVE 5). Recupera la mezcla par/impar que ya usaba el generador MASTER R2 de HARMOHIT, sin salir de la identidad tanh.
- **Aliasing.** −100 dBc a DRIVE 10 con 7 y 11 kHz a −6 dBFS (E11). La amplitud normalizada u queda acotada por la cresta y no por el nivel absoluto. Hay que verificarlo con material denso en agudos.
- **Latencia.** Cero adicional: detector hacia delante sin lookahead. Se mantienen las 6 muestras del sobremuestreo actual.
- **Coste.** Por muestra 4×: un tanh, una división y una actualización de potencia. Por trama, una raíz, con la potencia de h calculada por bloque. Similar al actual, que usa tanh, división y dos envolventes.
- **M/S.** Cada núcleo se refiere a su propio nivel. SIDE pasa a ser útil: −25 dB de residuo a DRIVE 5 frente a −68 dB actual. En STEREO, el detector enlazado conserva la imagen.
- **Ventajas.** El mando significa lo mismo con cualquier mezcla. No hay detector de transitorios frágil, el sonido sigue siendo HARMOHIT y reutiliza toda la infraestructura existente.
- **Inconvenientes.** Introduce un proceso dinámico lento: la rodilla tarda unos 60 ms en reaccionar a subidas bruscas, y la primera entrada tras un silencio satura más. No reproduce la ley de "cuanto más fuerte entra, más distorsiona" del hardware.

### B. Saturador estático con punto de trabajo calibrado (estilo analógico)

```
G(d) = 0 … +18 dB           ref = −18 dBFS RMS (0 VU)
y = x + w(d)·[ G^(−α) · S(G·x/ref)·ref − x ]   con S asimétrica, α ≈ 0.6
```

- **Fundamento.** Ganancia previa fija hacia una curva asimétrica con compensación posterior **parcial y fija**, no automática. Al subir DRIVE, la salida sube algo de nivel, como en un equipo analógico.
- **Comportamiento.** Determinista y sin detector. Dependencia de nivel clásica: la misma ±20 dB del diseño actual, solo que desplazada al rango útil. A −25 dBFS RMS hacen falta unos 7 dB de G para alcanzar el punto de trabajo.
- **Transitorios.** Los picos saturan antes que el cuerpo, así que reduce los ataques desde intensidades medias si el material está caliente.
- **Carácter.** Componentes par e impar según la asimetría de S.
- **Aliasing.** Mayor que en A a niveles altos, porque G·x no está acotado. Puede requerir 8× a DRIVE alto.
- **Latencia y coste.** Latencia cero adicional y coste mínimo.
- **M/S.** SIDE sigue siendo débil, porque la referencia es fija.
- **Ventajas.** Simplicidad y comportamiento de hardware conocido.
- **Inconvenientes.** No resuelve la pregunta B. Cambia el nivel de salida, lo que sesga cualquier A/B, y la UI no tiene control de entrada para compensar la dependencia de nivel. **No se ha prototipado.** Se describe solo como contraste.

---

## 8. Recomendación principal

**Sustituir el núcleo de DRIVE de MASTER por la alternativa A, con rodilla referida al programa, y eliminar el detector de transitorios actual.**

Se mantienen sin cambios:
- sobremuestreo 4×;
- topología de residuo alineado;
- IDs de parámetro y rangos;
- `MasterEngine` y routing M/S;
- UI aprobada;
- HARMONICS y EXCITER, con la equivalencia bit a bit con MIX.

Sobre la decisión de producto: si la iteración concedida debe ser "del enfoque actual" en sentido estricto, es decir, reajustar k, w y la ley, la auditoría desaconseja gastarla así. Subir k corrige la sensibilidad a −25 dBFS pero multiplica la dependencia de nivel y deja SIDE inútil. Es justo lo que no se quiere. La alternativa A sí cabe en una iteración porque solo sustituye una función y su estado.

---

## 9. Plan de implementación (una iteración, pendiente de autorización)

0. **Antes de tocar código:** repetir la captura en Logic con el control positivo de §2 para cerrar H1, H2 y H4. Coste: una sesión.
1. **`MasterChannelCore`:** reemplazar `driveResidual` y su estado por la alternativa A.
   - Detector de potencia por núcleo, enlazado en estéreo.
   - Referencia híbrida T = max(R·10^(h/20), T_abs(d)), y referencia de pico lenta para transitorios aislados.
   - Componente par con bloqueo de DC.
   - DRIVE 0 devuelve un residuo exactamente 0, con el detector siempre activo para que esté caliente.
   - Eliminar `fast/slowEnvelope` y la ley (1 − I).
2. **Calibración:** fijar h(d), w(d), β(d) y T_abs con el banco `R12-audit` contra los criterios de §10. Nada se ajusta a oído antes de cumplirlos.
3. **Pruebas automáticas:** portar E2–E7 y E11 a `Tests/DriveTests.cpp` como regresiones con los criterios de §10. Mantener las 63 equivalencias HARMONICS/EXCITER y las pruebas de NaN, bloques y asignaciones.
4. **Build Release, auval y, con autorización, instalación.** Smoke en Logic con escucha ciega.
5. **Entregables:** informe R12.1 con antes/después sobre la misma matriz y el WAV de referencia.

Fuera de alcance: UI, IDs, sobremuestreo, HARMONICS, EXCITER, MIX y Gainer80.

---

## 10. Pruebas objetivas para decidir si la iteración funciona

Condiciones comunes: 48 y 96 kHz, bloques de 256, 1 s de calentamiento, trim 0, HARMONICS y EXCITER a 0 salvo indicación.
Materiales: seno a 1 kHz, 7 kHz y 11 kHz con bins exactos; impulsos; batería; el **WAV de referencia** a nivel nativo y escalado a −24, −18, −12 y −6 dBFS RMS; M/S con su Side real.
Se comparan siempre MIX, MASTER R1 y la nueva versión con `audit_drive`.

| # | Criterio | Umbral propuesto | Justificación |
|---|---|---|---|
| C1 | Neutralidad | DRIVE 0 idéntico bit a bit a R1 DRIVE 0. Las 63 equivalencias H/E con MIX siguen en error 0. | Requisito de producto, sin margen. |
| C2 | Progresión audible (WAV, nivel nativo, residuo frente a DRIVE 0) | DRIVE 3: −40…−33 dB. DRIVE 5: −31…−25. DRIVE 7: −24…−18. DRIVE 10: −16…−11. Al menos 5 dB entre pasos. | Ancla empírica: EXCITER 3 y 5 (−36,6 y −24,2 dB) son audibles según el usuario. 5 dB separa escalones perceptibles. El residuo es un indicador, no sonoridad: se confirma en C9. |
| C3 | Robustez de nivel | Dispersión del residuo entre −24, −18 y −12 dBFS RMS ≤ 6 dB para cada DRIVE. Hoy son 16–20 dB. | Las mezclas que llegan a mastering varían unos 12 dB. 6 dB mantiene el significado del mando dentro de un escalón de C2. |
| C4 | Ataques | DRIVE ≤ 5: ataque de batería y picos del 0,1 % superior de la música ≥ −1,0 dB. DRIVE 10: reducción de cresta entre 1,5 y 5 dB y cuerpo ≥ −1 dB. | Alrededor de 1 dB es el umbral de cambio de nivel perceptible en ataques. Más de 6 dB de cresta es comportamiento de limitador, que se excluye. |
| C5 | Transitorio aislado tras silencio | Pico de impulso o primer golpe a DRIVE 5 ≥ −3 dB. | A2 da −4,7 dB y falla. Cubre intros y cortes. |
| C6 | Aliasing | Contenido no armónico ≤ −80 dBc a DRIVE 10 con 7 y 11 kHz a −6 dBFS, y no peor que R1 con música en la banda de 16 a 24 kHz. | No empeorar el R1 actual. MIX (−28 dBc) no es referencia de mastering. |
| C7 | M/S | Residuo de SIDE solo, relativo al Side, a menos de 6 dB del de MID solo en el mismo DRIVE. Fuga a la otra componente ≤ −100 dB. | Bancos realmente independientes y útiles. |
| C8 | Sin compensación | Ganancia de pequeña señal a −40 dBFS: 0 ± 0,05 dB. Cambio de RMS con música a DRIVE 10 entre −2 y 0 dB. | Exigencia del producto: ni subida de nivel ni normalización. |
| C9 | Escucha | Captura en Logic con control positivo que muestre los escalones. ABX ciego DRIVE 0 frente a 5 con el material de referencia: ≥ 12/16, p ≈ 0,04. Evaluación musical de DRIVE 3/5/7/10 en tres mezclas. | La métrica no sustituye al oído. Esta es la decisión final de producto. |
| C10 | Tiempo real | NaN/Inf, sin asignaciones en el hilo de audio, automatización sin saltos > 0,001, carga de CPU ≤ 1,3× R1. | Como R1. |

**Regla de decisión:** si C1–C8 se cumplen y C9 es positivo, la iteración funciona. Si C2 a C4 no pueden cumplirse a la vez tras la calibración, el conflicto es estructural y procede descartar también esta arquitectura.

---

## 11. Reproducción

```sh
cd harmohit-master   # raíz del repositorio
B=/tmp/r12-audit   # cualquier directorio fuera del árbol del producto
cmake -S Documentation/R12-audit/tools -B "$B" -DCMAKE_BUILD_TYPE=Release
cmake --build "$B" -j8
"$B"/wav_ab evidencias/HARMOHIT_MASTER_DRIVE_AB.wav
c++ -std=c++20 -O2 Documentation/R12-audit/tools/wav_repeats.cpp -o "$B"/wav_repeats
"$B"/wav_repeats evidencias/HARMOHIT_MASTER_DRIVE_AB.wav
R12_VARIANT_A2=1 "$B"/r12_drive_audit evidencias/HARMOHIT_MASTER_DRIVE_AB.wav
```

Requisitos: JUCE 9.0.3 en `../vitaplug_gainer_80/external/JUCE`, MIX en `../harmohit` (solo lectura) y el WAV en `evidencias/`. Si el WAV se mueve, se pasa la ruta como argumento.
SHA-256 del AU instalado auditado: `0e83c28acb5ed3ecdcae922ae356c07035b1433823aaa18c6ff3767d5001f92f`.
