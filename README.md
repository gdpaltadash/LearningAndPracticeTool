# PaltaDash's Practice Tool (Geode, GD 2.2081)

Mod de práctica para aprenderse niveles a partir de una macro. UI en inglés, hecha con nodos de Cocos desde cero.

Formatos de macro soportados (se detectan por contenido, da igual la extensión):
GDR2 (`.gdr` / `.gdr2`), **MegaHack** (`.mhr` binario y `.mhr.json`), zBot (`.zbf`), xdBot (texto con `|`), TASBot (`.json`), Echo (`.json`), y texto plano (`fps` en la primera línea, luego `frame hold p2`).

## Cómo compilarlo en GitHub

1. El `id` es `paltadash.practice-tool` y el developer `PaltaDash` (cámbialos en `mod.json` si quieres otros).
2. Crea un repositorio en GitHub y sube **todo el contenido de esta carpeta** (incluida la carpeta oculta `.github`).
3. Ve a la pestaña **Actions**. El workflow *Build Mod* corre solo en cada push, o lo lanzas a mano con *Run workflow*.
4. Cuando termine, descarga el artefacto **PaltaDash's Practice Tool (.geode)** y mete el `.geode` en la carpeta `geode/mods` de tu GD (o ábrelo con Geode).

Usa Geode **v5.11.2** (la última para GD 2.2081) y la librería [GDReplayFormat](https://github.com/maxnut/GDReplayFormat) fijada al commit `d8d2f0b`.

## Cómo se usa

Pausa el nivel y pulsa el botón del mod (abajo a la derecha). La ventana tiene:

- **Enable Mod** (arriba a la derecha): interruptor general. Apagado, el mod no hace nada.
- **Load / Unload**: importa o quita la macro (se detecta el formato solo). Se recuerda por nivel.
- **Practice Mode**: **Watch** (la macro juega con click sounds, tus inputs se ignoran) y **Trace** (la macro juega y tus clicks solo se miden: precisión en vivo y reporte). Pulsar el modo ya elegido lo apaga. Al elegir un modo, el panel **Settings** de la derecha muestra solo sus ajustes: `Scoring window` (Trace), `Macro frame offset`, `Game TPS`, `Click volume` (0 = sin sonido, hasta 250%), `StartPos adjust` y el **Click pack** (una carpeta de sonidos para todas las macros; los releases son archivos/carpetas con `release` en el nombre).
- **Click Indicators** (switch + engrane): dos tipos, elegibles en el engrane (`Lane` / `Dot` / `Both`).
  - **Lane**: carril abajo en pantalla con un círculo por press y otro por release (mismo tamaño, distinto color).
  - **Dot**: un punto dentro del nivel, en el lugar exacto donde ocurre cada input (press o release), con `Dot size`. La **hold line** (opcional) une el punto de press con el de release en el nivel, con color, opacidad y grosor (`Hold line width`) configurables; en el carril también se dibuja entre los círculos.
  - Además: `Also show with no mode active` (los muestra con la macro cargada aunque no estés en Watch/Trace, para ver dónde clickear), opacidad y colores de press, release y hold line.
- **Predictive Buffer** (switch + engrane): sirve **con y sin macro**.
  - Con macro cargada (y `Aim at the loaded macro` activo): lo que clickeas antes de tiempo (hasta `Buffer window` frames) se retrasa al frame exacto de la macro; lo tardío se deja igual. Te dice cuánto te adelantaste.
  - Sin macro: un click que das cuando todavía no puedes actuar (cubo en el aire, sin orbe) se retiene hasta que puedas, hasta `Buffer window` frames, y conserva la duración del hold.
  - El engrane también elige en qué esquina sale su marca de agua en la pantalla final (una sola a la vez; la marca no se puede apagar).
- **Restart Level** y **Last Results**.

**Pantalla final**: sale la real, con la marca **"Practice tool used"** arriba a la izquierda (y debajo el modo / precisión de Trace) y, si el buffer está activo, la marca **Predictive Input Buffer** en la esquina que elijas. Las marcas de agua no se pueden desactivar.

**Stats**: mientras un modo o el buffer estén activos no se guarda nada (progreso, attempts, stats, completions, logros). Los indicadores solos no bloquean nada.

**StartPos**: las macros se graban desde el inicio del nivel, así que desde un StartPos iban desfasadas. El mod mide en segundo plano por dónde pasa el jugador mientras juegas el nivel **desde el principio** (en cualquier modo; se guarda en disco por nivel) y con esa tabla calcula a qué frame de la macro corresponde el StartPos. Mientras no haya una pasada desde el inicio, el panel avisa `StartPos: play this level from the start once`. `StartPos adjust` corrige ±1 frame si hiciera falta. "Lock delta" no hace falta: el mod cuenta pasos de física, no tiempo.

**Macro grabada desde un StartPos**: en el panel de Watch/Trace hay la fila **Macro starts at** (`Level start` / `StartPos`). Si tu macro se grabó desde un StartPos, inicia el nivel desde ese mismo StartPos, abre el panel y pulsa **StartPos** (se guarda por macro). A partir de ahí el frame 0 de la macro es ese punto: si juegas desde ese StartPos sincroniza directo; si juegas desde antes, la macro espera y arranca sola cuando el jugador llega a esa posición; si juegas desde un StartPos posterior, usa la tabla de trayectoria (hace falta haber pasado por ahí una vez desde el inicio). `Level start` vuelve al comportamiento normal.

Los puntos dentro del nivel y la sincronización de StartPos salen de esa misma tabla, así que no hace falta una pasada en Watch: basta con haber jugado el nivel desde el inicio (o haber llegado más lejos que el StartPos).

## Si algo no cuadra

- **En Watch la macro muere o se desfasa**: prueba `Macro frame offset` en `-1` o `+1`. Cada bot cuenta el frame con una convención algo distinta; el mod usa la de zBot (`m_currentProgress - 1`).
- **La macro iba más rápida que el juego**: `m_currentProgress` no cuenta un paso por tick (también avanza en half ticks), así que ahora el contador de ticks sale del tiempo del nivel (`m_levelTime`) por el TPS de la macro (o `Game TPS`). Si aun así notas un desfase constante, ajusta `Macro frame offset`.
- **Desde un StartPos queda 1-2 frames corrida**: usa `StartPos adjust`.
- **Macro grabada con otro TPS** (por ejemplo 360 con physics bypass): pon `Game TPS` igual al TPS al que corre tu juego. En `Auto` se asume que son iguales.
- Si el archivo no se reconoce, el mod lo dice. En zBot y xdBot el significado del flag de player 1/2 se tomó de las fuentes de nat-converter; si P1 y P2 salen invertidos con tu macro, avísame.

- **Los puntos del nivel desaparecen**: la tabla de trayectoria ya no se borra sola (antes se vaciaba si tu recorrido se desviaba de la ruta guardada). Ahora, si la macro está jugando (Watch/Trace) desde el inicio del nivel, su recorrido reemplaza al guardado, y si falta un punto se usa el más cercano.

- **Trace dice "No match"**: los clicks tuyos ahora se fechan con el mismo reloj con el que la macro se reproduce (el último paso completado), no con el tiempo del nivel en el instante del click. Además el mensaje muestra cuántos frames te separa del input de la macro más cercano, p. ej. `No match (+37f)`: si ese número es siempre parecido, es un desfase constante y se corrige con `Macro frame offset`; si cambia mucho, es que clickeas lejos de la macro.

- **No salen los puntos dentro del nivel (por ejemplo desde un StartPos)**: los puntos necesitan saber por dónde pasa la macro. Ahora, mientras estás en Watch o Trace, el mod guarda el recorrido de la macro (por macro y por nivel, también desde un StartPos). La **primera pasada** en Watch va dibujando los puntos solo hacia atrás; a partir de la segunda (o tras ver la macro hasta donde quieras practicar) ya aparecen todos por delante. Si además juegas el nivel desde el inicio, ese recorrido sirve de respaldo.

- **Trace desde el 0% dice "No match" siempre**: casi seguro la macro se grabó desde un StartPos y el mod cree que empieza en el 0%, así que ninguno de tus clicks cae cerca de sus inputs (el aviso muestra algo como `No match (+3000f)`). Solución: inicia el nivel desde el StartPos con el que se grabó, abre el panel, y en **Macro starts at** pulsa **StartPos**. Desde entonces puedes jugar desde el 0%: la macro espera y arranca sola cuando llegas a ese punto (mientras espera, tus clicks no se puntúan). Si varios clicks seguidos caen muy lejos, el HUD de Trace pone `OUT OF SYNC` para avisarte de esto.

- **Opacidad muy alta**: el dibujo ahora mezcla con alpha premultiplicado (antes un valor bajo seguía viéndose brillante), el carril también escala con la opacidad, y el mínimo bajó de 5% a 1% (pasos de 1 por debajo de 10%).

- **Detección automática (Auto)**: la fila **Macro starts at** tiene ahora `Auto` (por defecto), `Level start` y `StartPos`. En Auto el mod decide solo, por macro, si empieza en el 0% o en un StartPos, y lo recuerda: si empiezas desde un StartPos y no hay recorrido guardado para sincronizar, asume que la macro se grabó desde ese StartPos; si luego ve que no encaja (varios clicks de Trace muy lejos, o la macro muere en los primeros 2.5 s), cambia de hipótesis y te avisa con `Restart the level`. Si prefieres decidirlo tú, pulsa `Level start` o `StartPos`.

- **Trace: "No match" aunque clickees justo en el input** (y el mensaje sin número): era el flag de jugador. En niveles de 1 jugador el mod ya no distingue P1 y P2 al emparejar, y el gemelo P2 de un input P1 no se puntúa dos veces. El mensaje "No match" / "Perfect" sale ahora centrado en pantalla.

## Estructura

```
src/core/Macro.*       lectores de macros (GDR2 + otros formatos, C++ puro)
src/core/Engine.hpp    emparejado, buffer con macro, puntuación, shift de StartPos (C++ puro, probado)
src/core/Buffer.hpp    predictive buffer sin macro (C++ puro, probado)
src/core/PathTable.hpp trayectoria del jugador por paso de física (C++ puro, probado)
src/Session.*          estado global, ajustes, sonidos, bloqueo de stats
src/hooks.cpp          hooks de Geode (inputs, nivel, stats, pantalla final, menú de pausa)
src/ui/                HUD, ventana principal, popup de ajustes (engranes), popup de resultados
tests/test_core.cpp    tests de la lógica (se compilan aparte, sin Geode)
```

Tests de la lógica (necesitan g++ 13+ y los headers de GDReplayFormat):

```
g++ -std=c++23 -Isrc/core -I<ruta a GDReplayFormat>/include tests/test_core.cpp src/core/Macro.cpp -o test_core && ./test_core
```
