# HARMOHIT MASTER — Plan técnico de integración DRIVE 670 R0.2

Fecha: 14‑oct‑2026. Punto de recuperación: `<VITAPLUG>/backups/HARMOHIT_MASTER_R1.2_source_snapshot_20261014`.

## Auditoría

- `MasterEngine` contiene tres `MasterChannelCore`: STEREO (2 canales), MID (1) y SIDE (1). Los tres se procesan siempre, con codificación y decodificación M/S, fundido de modo de 20 ms y OUTPUT TRIM global (−24…+12 dB).
- `MasterChannelCore`: sobremuestreo IIR 4× (2 etapas, half-band polifásico IIR, máxima calidad), HARMONICS y EXCITER, DRIVE R1.2 en serie sobre la señal coloreada, trim por banco (fijado a 0 dB) y bajada a la frecuencia base. Latencia de 6 muestras a 48 kHz.
- Parámetros `stereo|mid|side.drive`: Float 0–100 con paso 0,1. El mando muestra 0–10. `processBlock` divide por 100.
- LAB R0.2: `Drive670Engine` usa el **mismo** sobremuestreador con los mismos argumentos, con `Drive670Core` en el dominio 4×.

## Decisiones

1. `Drive670Core.h` y `Drive670PostGain.h` se copian sin cambios. Una prueba exige que sean idénticos a los del LAB.
2. **Sin sobremuestreo duplicado.** El núcleo trabaja dentro del dominio 4× de cada banco; la latencia no cambia.
3. **Routing paralelo diferencial.** El 670 recibe la señal seca del banco y la salida es `p670 + (coloured − dry)`, lo mismo que `dry + residuos H/E + (p670 − dry)`. Seco, H/E y 670 están en la misma muestra, así que la alineación es exacta. Con H/E desactivados, el término entre paréntesis es exactamente 0 y la salida es la del LAB. Con DRIVE 0 en reposo se usa `coloured`, que conserva bit a bit el camino HARMONICS/EXCITER.
4. **Calibración por banco**, aislada en `DriveBankCalibration.h`: ganancia de operación recíproca `p = 670(x·G)/G`. STEREO 0 dB, que es el LAB exacto. MID −3,01 dB, para que un programa mono procesado en M/S se comporte como en STEREO. SIDE +X dB, por calibrar con medidas.
5. **Mapeo de DRIVE:** paso = redondeo(valor/10) con histéresis de 0,05 pasos y arranque instantáneo al valor actual en `prepare`, como hace el LAB.
