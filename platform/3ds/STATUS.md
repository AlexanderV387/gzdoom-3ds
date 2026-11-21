## Prueba 66
**Lo que se pidió:**
- En `v_colortables.cpp`, cuando no hay caché (en el bloque `#ifdef __3DS__` de `BuildTransTable`):
    - Calcular las tablas como ahora.
    - Crear un hilo aparte con `threadCreate` (libctru) para escribir el archivo en la SD.
    - El hilo debe usar el núcleo 0 y prioridad `hilo_principal + 4` (máximo `0x3F`).
    - Pasar una copia de los datos (`RGB32k` y `RGB256k`) al hilo para evitar problemas de concurrencia.
    - Si la escritura falla, borrar el archivo a medias.
- Logs:
    - Distinguir entre `"colortables: cache leída"` y `"colortables: calculadas, se guardan en segundo plano"`.
    - Usar `I_msTime()` para los tiempos (ya que `clock()` devuelve 0 en la 3DS).
- Mediciones adicionales ([load]):
    - En `GStrings::LoadStrings` (`src/common/strings/gstrings.cpp`).
    - En el constructor de `FIWadManager` (`src/common/wad/d_iwad.cpp`).

**Hipótesis:** Mover la escritura a un hilo permitirá que el juego arranque inmediatamente después de calcular las tablas, eliminando el retraso de 20 segundos causado por la lentitud de la SD al escribir el archivo nuevo.

**Autocomprobación:**
1. ¿El log muestra `"colortables: calculadas, se guardan en segundo plano"` y el juego arranca rápido sin esperar a la SD? (Pendiente de pasada A)
2. ¿Se crea correctamente el archivo en `sdmc:/3ds/gzdoom/cache/` una vez que el hilo termina? (Pendiente de pasada A)
3. ¿Aparecen las marcas `[load]` para `GStrings` e `FIWadManager` con sus tiempos? (Pendiente de pasada A)
4. ¿Cada hipótesis predice algo distinto en el log? (Ver si los tiempos [load] o el mensaje de colortables cambian según la situación)
5. ¿Lo que dices que se verá en pantalla puede verse? (No aplica, nos basamos en el log)
6. ¿Se resuelve en una sola compilación? (Sí)
7. ¿Cada afirmación sobre una API tiene su fuente? (Se usará libctru y citro3d de `~/work/refs/`)
8. ¿Lo que vas a añadir ya existe? (El hilo para la escritura es nuevo)
9. ¿Qué se ejecuta después de tu cambio? (El juego continúa tras lanzar el hilo)


**Pregunta:** ¿Se ha eliminado el retraso de 20 segundos en el primer arranque tras calcular la caché?

**Pasadas en la consola:**
| Pasada | Archivos de control | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Log "colortables..." y tiempos [load] en consola |

**Cómo se compila:** `platform/3ds/docker-build-test.sh 66`
