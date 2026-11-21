# UZDoom 3DS: estado (leer al empezar cada conversación)

Es el único estado válido del port. Se actualiza al cerrar cada prueba: la tabla, "Lo que sabemos" y "Pregunta abierta". Lo escribe quien trabaja (Catequilla o Claude), en español, corto.

## Registro de pruebas
| Prueba | Pregunta | Resultado |
|---|---|---|
| 63 | ¿Se arregla el derretimiento y los logs de arranque? | Pendiente |

## Prueba 63

**Lo que se pidió:**
- [ ] Derretimiento: FWrapperTexture tamaño lógico 400x240. Aplicar matriz `texmat` para escalar (240/256, 400/512) y girar según CVAR `c3d_wipeorient` (0-7). Log orientación usada.
- [ ] Arranque: Arreglar marcas `[load] "nombre T+"` con `I_msTime()`. Marcas antes/después de llamadas específicas en `d_main.cpp` (`SetMapxxFlag`, `D_GrabCVarDefaults`, `InitPalette`, `GuesstimateNumTextures`, `DoKeySetup`, `ParseCVarInfo`, `ExecCommands`, `GStrings.LoadStrings`) y en `D_DoomMain` (entre fin de `M_LoadDefaults` y `W_Init`, alrededor de llamadas IWAD).

**Pregunta:** ¿Se logra un wipe correcto sin basura gráfica y con rotación ajustable, y se obtienen tiempos detallados del arranque?

**Lo que ya sabemos (con la prueba que lo mostró):**
- Prueba 62: el derretimiento sale chiquito, girado y con basura porque la textura es 256x512 y el juego la trata como si toda la textura fuera la pantalla.
- Las marcas `[load]` no tienen tiempo.

**Hipótesis A:** Los cambios en el tamaño de la textura, el uso del shader para escala/rotación y las nuevas marcas permiten un wipe correcto y tiempos detallados.
- Si A es cierta, el log mostrará: `[WIPE] orient=N` y marcas `[load] nombre T+ms`.

**Única variable que cambia:** 1. Tamaño de `FWrapperTexture` en `c3d_framebuffer.cpp` (400x240). 2. Uso de CVAR `c3d_wipeorient` para modificar la matriz `texmat` en el shader. 3. Inyección de marcas y uso de `I_msTime()` en `d_main.cpp` y `D_DoomMain`.

**Pasadas en la consola:**
| Pasada | Archivos de control | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Wipe correcto (sin basura) y rotación con CVAR; logs con tiempos detallados |

**Marcadores que se quedan:** Los de la pregunta abierta.
**Marcadores que se quitan:** Ninguno.

**Cómo se compila:** `platform/3ds/docker-build-test.sh 63`

**Autocomprobación:**
1. **Tabla de diferencias.** Se modifican: tamaño textura, escala/rotación en shader y marcas de log.
2. **¿Cada hipótesis predice algo distinto en el log?** Sí: A muestra `[WIPE] orient=N` y `[load] T+ms`.
3. **¿Lo que dices que se verá en pantalla puede verse?** Sí, el wipe sin basura y su rotación son visibles; los tiempos están en el log.
4. **¿Se resuelve en una sola compilación?** Sí, con un solo build se prueban todos los cambios.
5. **¿Cada afirmación sobre una API tiene su fuente?** Sí: `FWrapperTexture` en `c3d_framebuffer.cpp`, `texmat` en `c3d_framebuffer.cpp`, `I_msTime()` en `i_time.h`.
6. **Si todas las variantes dieran el mismo resultado, ¿qué sabría?** Si no hay cambio en la basura o rotación, algo falló en el shader o en el CVAR.
7. **¿Puede Alex preparar cada pasada sin equivocarse?** Sí, solo necesita un `ini` para la orientación si quiere probar diferentes.
8. **¿Lo que vas a añadir ya existe?** No, se añaden nuevas marcas de log y un nuevo CVAR.
9. **¿Qué se ejecuta después de tu cambio?** El flujo del motor hasta el gameplay, con las nuevas marcas en el log.

## Lo que sabemos (con la prueba que lo mostró)

(No hay datos nuevos todavía)

## Pregunta abierta

**¿Se logra un wipe correcto sin basura gráfica y con rotación ajustable, y se obtienen tiempos detallados del arranque?**
