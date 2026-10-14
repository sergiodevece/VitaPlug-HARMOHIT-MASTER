# VitaPlug HARMOHIT MASTER v0.2.0 — Drive 670 Engine

**R2. Estable de referencia.** Aprobada en escucha real en Logic Pro y validada con pruebas DSP.

## Novedades

- **Motor DRIVE 670.** Compresión variable-mu con detección por realimentación, coloración push-pull de armónicos pares e impares y saturación de transformador. Es un macroprocesador de un solo mando, **inspirado** en compresores clásicos de válvulas como el Fairchild 670. No es una emulación ni está afiliado a sus titulares.
- **Tres regímenes dinámicos:**

  | DRIVE | Régimen | Ataque / recuperación |
  |---|---|---|
  | 1–3 | TC5 | 0,2 ms; recuperación dependiente del programa |
  | 4–7 | TC3 | 0,4 ms / 2 s |
  | 8–10 | TC2 | 0,2 ms / 0,8 s |

- **POST GAIN integrado:** +1 dB por paso. DRIVE comprime, colorea y empuja el nivel. No hay limitador ni normalización; el nivel final se controla con **OUTPUT TRIM** (−24 a +12 dB).
- **Procesamiento STEREO** con detección enlazada que preserva la imagen.
- **Procesamiento MID/SIDE independiente:** MID y SIDE se procesan a la vez, cada uno con su motor DRIVE, HARMONICS y EXCITER.
- **HARMONICS y EXCITER:** sin cambios respecto a HARMOHIT MIX y fuera del compresor (routing paralelo).
- **Flujo destacado:** M/S con DRIVE en MID y HARMONICS + EXCITER en SIDE. Véase `Documentation/MID_SIDE_MASTERING_WORKFLOW.md`.

## Cambios de sonido respecto a R1.2

DRIVE conserva su ID y su rango, pero su motor y su significado son nuevos. Una sesión antigua con DRIVE a 70 aplica ahora compresión TC3 y +7 dB de POST GAIN.
- Abre **copias** de tus sesiones.
- Revisa los parámetros y baja el nivel de salida antes de reproducir.
- No sobrescribas presets antiguos.

## Compatibilidad verificada

| Plataforma | Estado |
|---|---|
| macOS 26.5, Apple Silicon (M5), arm64 | Verificado |
| Logic Pro 12.4, AU | Verificado |
| VST3 | Compilado y cargado en un host de pruebas JUCE; no probado en un DAW |
| 44,1, 48 y 96 kHz | Verificado (pruebas automáticas) |
| macOS Intel | No probado |
| Windows | No probado |

## Validación técnica

| Prueba | Resultado |
|---|---|
| Compilación limpia | Correcta |
| CTest | 5/5 |
| Integración | 62/62 comprobaciones |
| HARMONICS/EXCITER frente a MIX | 63 equivalencias |
| Matriz de DRIVE | 648 casos |
| Medidas | Idénticas a las de la R2 aprobada |
| STEREO frente a DRIVE 670 LAB R0.2 aprobado | Bit a bit a 44,1, 48 y 96 kHz, también bajo automatización |
| Sesión v0.1.0 (R2 aprobada) cargada en v0.2.0 | 0 diferencias de parámetros; audio idéntico muestra a muestra |
| Binario frente a R2 aprobada | Código y datos idénticos; solo cambia la hora de compilación |
| Interfaz | Idéntica píxel a píxel |

## Artefactos (macOS arm64)

| Archivo | SHA-256 del ejecutable |
|---|---|
| `VitaPlug HARMOHIT MASTER.component` (AU, aufx/HmR1/Vtpl, 0.2.0) | `dc6cbd1a9b90711f94aab0af4def50938dfb98130b6f37bef783d3b8e31bd985` |
| `VitaPlug HARMOHIT MASTER.vst3` (VST3, 0.2.0) | `9cc05036bce2491f4b671b3e6acf7c61383b018d1d70a286dc6c8881660b39b7` |

Firmados **ad hoc**: sin firma Developer ID ni notarización de Apple. Las instrucciones de instalación están en `INSTALL.md`, dentro del paquete.

## Limitaciones conocidas

- CPU de unos 6,3 % de un núcleo a 48 kHz (M5).
- Calibración SIDE del DRIVE provisional.
- Desfase del puntero del mando de unos 3°.
- Solo arm64, sin notarizar.
