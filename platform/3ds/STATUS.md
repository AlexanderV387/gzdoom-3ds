# UZDoom 3DS: estado (leer al empezar cada conversación)

Es el único estado válido del port. Se actualiza al cerrar cada prueba: la tabla, "Lo que sabemos" y "Pregunta abierta". Lo escribe quien trabaja (Catequilla o Claude), en español, corto.

**Rama de trabajo:** `uzdoom-3ds-test25` (parte de `catequilla-test24`, que guarda las pruebas 17 a 24 tal como quedaron).
**Compilar:** `platform/3ds/build-test.sh NN` en Ubuntu (ver README). Una vez antes: `platform/3ds/build-sdl2-3ds.sh`.
**Pruebas guardadas:** `~/work/3ds-ports/pruebas/NN/` (`.3dsx`, `.elf`, `info.txt`).

## Lo que sabemos (con la prueba que lo mostró)

1. El motor arranca entero: IWAD, ZScript, menús, playsim y backend (`Init complete`, pruebas 7 y 13).
2. La prueba 8 se cerraba en `I_Rumble`: el gestor del joystick está vacío en la 3DS (pruebas 11 y 12, arreglado). **Ojo:** eso también significa que quizá la entrada de mandos nunca se inicia. Revisarlo cuando haya imagen.
3. La imagen de la pantalla de carga a 1280x960 no cabía en la memoria: ahora es de 640x480 (prueba 13).
4. **El primer fotograma con un dibujo cuelga la GPU.** El fotograma se envía (`C3D_FrameEnd`), pero la GPU nunca termina: el siguiente `C3D_FrameBegin` falla durante 2 s y todos los siguientes también (pruebas 16 a 23).
5. Ese primer fotograma tiene **un solo dibujo**: un `DrawElements` 2D de 12 índices, stride 24, tres atributos (posición, coordenadas de textura, color) y con textura (pruebas 17 y 21).
6. Saltar el trabajo del fotograma 0 solo **mueve** ese dibujo al fotograma 1, que se cuelga igual (prueba 22). Eso **no** demuestra que el contenido no importe: el dibujo seguía ahí.
7. Los logs pueden perder el final si se apaga la consola: antes solo se hacía `fflush`, que no escribe en la SD (`hechos-3ds.md`, S2). Desde la prueba 25 hay `fsync`.

## Pregunta abierta

**¿El cuelgue lo provoca el comando de dibujo, o algo del fotograma que no es el dibujo (limpiar, presentar en pantalla, el estado)?** Y, si es el dibujo, ¿qué parte: la textura o los vértices e índices?

## Prueba 25 (preparada, por probar)

**Hipótesis:**
- **H1:** el comando de dibujo del motor cuelga la GPU.
- **H2:** el cuelgue viene de otra parte del fotograma (limpieza, salida a pantalla, configuración).
- **H3:** si es el dibujo, el problema es la **textura**.
- **H4:** si es el dibujo, el problema son los **vértices, índices o atributos**.

**Única variable por pasada:** un archivo vacío en `sdmc:/3ds/gzdoom/` (se lee al arrancar).

| Pasada | Archivo de control | Si sale esto en el log `[C3D]`… | …significa |
|---|---|---|---|
| A | ninguno | `GPU HANG` tras `frame 0` con `elements=1` | lo de siempre (referencia) |
| B | `c3d-nodraw.txt` | `frame N: begin=ok` en todos los fotogramas y sin `GPU HANG` | **H1**: el dibujo lo cuelga |
| B | ídem | `GPU HANG` igual que en A | **H2**: no es el dibujo |
| C | `c3d-triangle.txt` | sin `GPU HANG` y **un triángulo rojo** en la pantalla de arriba | lo básico funciona (shader, atributos, combinadores, salida). Apuntar hacia dónde mira el triángulo |
| C | ídem | `GPU HANG` | falla algo básico de citro3d en este backend (shader, inicio, render target) |
| D | `c3d-notex.txt` | sin `GPU HANG` | **H3**: la textura |
| D | ídem | `GPU HANG` | **H4**: vértices, índices o atributos |

**Cómo probar (Alex):**
- Una pasada cada vez, con un solo archivo de control (o ninguno en la A). Hay que borrar el de la pasada anterior.
- Esperar **60 segundos** después de que termine de cargar.
- Foto de la pantalla de arriba.
- Renombrar `log.txt` a `log-A.txt`, `log-B.txt`… antes de la siguiente pasada.

**Siguiente paso según el resultado:**
- H1 + H4 → la prueba 26 cambia solo el formato del dibujo: `DrawArrays` en lugar de `DrawElements`, o atributos sin color.
- H1 + H3 → la prueba 26 prueba con una textura pequeña fija, de 64x64.
- H2 → la prueba 26 quita la limpieza de mitad de fotograma (`Clear` de `Draw2D`) y la salida a pantalla, de una en una.

## Registro de pruebas

| Prueba | Pregunta | Resultado |
|---|---|---|
| 8 | Primera imagen | Se cerró: `I_Rumble` con el joystick vacío (visto en la 11–12) |
| 9–10 | Memoria al arrancar | OOM de la imagen de carga (`malloc(4915200)`) |
| 11–12 | Cierre en `S_ClearSoundData` | Arreglado: comprobar el gestor del joystick en `I_Rumble` |
| 13 | Imagen de carga de 640x480 | Llega a `Init complete`; luego pantallas negras y consola colgada |
| 14–15 | ¿Dónde se para? | Dentro de `C3D_FrameBegin`, en el segundo render de la pantalla de carga |
| 16 | `FrameBegin` sin bloquear | `FB_out=1` en el primer fotograma, 0 en los demás |
| 17 | Interruptores y padding | 1.ª versión inválida (ELF renombrado). 2.ª: `GPU HANG` confirmado; interruptores **nunca probados** |
| 18 | ¿Se ejecuta `FrameEnd`? | Sí, en el fotograma 0 |
| 19 | Tiempos y espera P3D | Inválida: compilada con archivos viejos |
| 19b | Espera P3D | Se queda en `gspWaitForP3D`: no concluyente (`hechos-3ds.md`, F10) |
| 20 | Sondeo con `FrameBegin` | El log se corta en el arranque (¿apagado temprano?) |
| 21–22 | Saltar el fotograma 0 | El fotograma 1 se cuelga igual (mal interpretado; ver "Lo que sabemos", 6) |
| 23 | Comprobación de salud de la GPU al iniciar | Rompió el arranque; 23b sin datos nuevos |
| 24 | Buffer de comandos normal y limpieza | Log cortado en las fuentes; sin marcadores: no aporta |
| 25 | Ver arriba | **Pendiente** |

## Pendientes que no son la pregunta abierta

- Una línea del log de la prueba 24 tiene bytes basura antes de `Selecting OpenGL backend...` (no son códigos de color: esos ya se quitan). Mirar si se repite con la 25.
- Entrada de mandos: el gestor del joystick está vacío (punto 2).
- Líneas y puntos (automapa), niebla, luz, portales, sonido, controles 3DS, pantalla de abajo, `.cia` (unique ID 0xF3D5C).
- La carga de las fuentes TrueType latinas genera atlas de 1 MB cada una con el juego de caracteres completo: lento y caro en memoria. Recortar el juego de caracteres cuando el resto funcione.
