# Mid/Side Mastering Workflow — HARMOHIT MASTER v0.2.0

Este flujo se aprobó en escucha real con HARMOHIT MASTER R2 en Logic Pro, sobre una mezcla completa. Se documenta como **ejemplo musical**, no como preset ni como recomendación obligatoria. No se dan valores de parámetros porque dependen de la mezcla y no forman parte de la validación.

## El flujo

| Banco | Motor | Propósito |
|---|---|---|
| **MID** | DRIVE 670 | Control dinámico, cohesión y color del centro |
| **SIDE** | HARMONICS + EXCITER | Enriquecer la información lateral |

Resultado observado en la escucha de validación:
- control dinámico excelente;
- transitorios musicales;
- apertura estéreo notable;
- más energía y presencia.

## Cómo configurarlo

1. Selecciona **M/S** en MODE.
2. Pulsa **M** para editar el banco MID y ajusta DRIVE. HARMONICS y EXCITER del MID pueden quedarse a 0 o usarse con moderación.
3. Pulsa **S** para editar el banco SIDE y ajusta HARMONICS y EXCITER, con RANGE o PUSH ALL según el material.
4. Ajusta el nivel final con **OUTPUT TRIM**.

Pulsar M o S solo cambia el banco que ves y editas. **Los dos bancos se procesan siempre a la vez.**

## Por qué funciona

- **El MID concentra lo que sostiene el master:** bombo, bajo, voz principal y caja. La compresión variable-mu del DRIVE 670 actúa ahí con detección por realimentación, ataque rápido y recuperación según el régimen. Controla los picos del centro, añade densidad y color armónico, y deja que el material lateral respire sin que el compresor reaccione a él.
- **El SIDE contiene reverberaciones, dobles, ambientes y elementos panoramizados.** HARMONICS y EXCITER añaden contenido armónico y presencia en esa zona, lo que aumenta la sensación de amplitud y detalle sin comprimirla. En HARMOHIT MASTER esos motores no pasan por el compresor ni por el POST GAIN, así que su efecto llega intacto.
- **Al reconstruir L/R:** `L = (M + S)/√2` y `R = (M − S)/√2`. El centro controlado y coloreado aparece igual en ambos canales; el Side enriquecido aparece con signo opuesto. Por eso aumenta la diferencia entre canales, que se percibe como apertura, mientras el centro conserva su estabilidad.

## Nivel

- **POST GAIN del MID:** el DRIVE del MID aplica +1 dB por paso. Como el MID suele dominar la energía del master, la salida puede subir varios dB y superar 0 dBFS internamente. Compensa con OUTPUT TRIM. El plugin no limita.
- **SIDE:** HARMONICS sube el nivel del Side según la cantidad aplicada.
- **Comparar con nivel igualado:** cualquier cambio más fuerte tiende a sonar "mejor". Iguala el nivel con OUTPUT TRIM o con una ganancia posterior antes de comparar con el bypass.

## Riesgos y comprobaciones

| Riesgo | Por qué | Cómo comprobarlo |
|---|---|---|
| **Exceso de Side** | Un Side demasiado alto o brillante produce una imagen hueca, un centro débil y una sensación artificial en los agudos | Alterna con el bypass a nivel igualado; vigila un correlador de fase |
| **Compatibilidad mono** | En mono, `L + R = √2·M`: **todo lo añadido al Side desaparece**. Si el balance depende del Side, el master pierde elementos en mono | Escucha en mono; revisa la correlación (valores negativos sostenidos son una alarma) |
| **Balance estéreo** | Un MID muy comprimido junto a un Side enriquecido cambia la relación entre centro y laterales | Compara la proporción Mid/Side con la mezcla original y con referencias |
| **Fatiga en agudos** | EXCITER en 8 kHz o PUSH ALL sobre un Side ya brillante | Escucha a volumen moderado y en varios sistemas |

## Otras combinaciones posibles

El mismo procesador permite el reparto inverso, como un DRIVE suave en el Side, o el uso simultáneo de los tres motores en ambos bancos. La calibración del DRIVE en el banco SIDE es **provisional** (§6 de [MASTER_R2_TECHNICAL.md](MASTER_R2_TECHNICAL.md)) y no ha tenido aún una escucha específica.
