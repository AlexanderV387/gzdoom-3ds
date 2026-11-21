## Prueba 65
**Lo que se pidió:**
- [ ] En `BuildTransTable` (`src/common/engine/v_colortables.cpp:46`), bajo `#ifdef __3DS__`:
    - Calcular CRC32 de los 256 colores de la paleta (usar función existente como `CalcCRC32`).
    - Si existe `sdmc:/3ds/gzdoom/cache/colortables-<crc>.bin` con tamaño `sizeof(RGB32k)+sizeof(RGB256k)`, cargarlo.
    - Si no, calcular y guardar en ese archivo (crear carpeta si falta). Si falla la escritura, seguir sin error.
    - Mantener cálculo de `Col2RGB8*`.
    - Log `[load] "colortables: cache leída/creada"` con tiempo.

**Hipótesis:** El uso de un cache en la SD para las tablas RGB32k y RGB256k reducirá significativamente el tiempo de arranque al evitar los bucles de ColorMatcher.Pick.

**Autocomprobación:**
1. ¿El log muestra "[load] colortables: cache leída/creada" con un tiempo bajo cuando hay cache?
2. ¿El archivo se crea en `sdmc:/3ds/gzdoom/cache/` si no existe (o falla la escritura sin crashear)?
3. 
4. 
5. 
6. 
7. 
8. 
9. 

**Pregunta:** ¿Se ha reducido el tiempo de arranque en `BuildTransTable` usando el cache?

**Pasadas en la consola:**
| Pasada | Archivos de control | Qué mirar en pantalla |
|---|---|---|
| A | ninguno | Log "[load] colortables..." y mejora en tiempos de arranque |

**Cómo se compila:** `platform/3ds/docker-build-test.sh 65`
