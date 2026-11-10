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

Pausa el nivel y pulsa el botón del mod (abajo a la derecha).

- **Load Macro**: importa la macro. Se recuerda por nivel y se vuelve a cargar sola la próxima vez.
- **Mode**
  - **Early Fix**: juegas normal. Los clicks/releases que des *antes* (hasta `Max early frames`) se retrasan al frame exacto de la macro. Los tardíos se dejan tal cual. Te dice cuánto te adelantaste (`Fixed 3f early`).
  - **Watch**: la macro juega el nivel con click sounds. Tus inputs se ignoran.
  - **Trace**: la macro juega el nivel, tus clicks solo se miden. Precisión en vivo y reporte final (perfect / early / late / missed / extra, offset medio en frames y ms).
- **Restart Level**: reinicia desde el principio con el modo ya activo (Watch y Trace lo necesitan).
- **Last Results**: reporte del intento actual o del último terminado.

El panel solo muestra los ajustes del modo elegido:
- **Early Fix**: `Max early frames`, `Scoring window`, `Macro frame offset`, `Game TPS`.
- **Watch**: `Macro frame offset`, `Game TPS`, `Click volume`.
- **Trace**: `Scoring window`, `Macro frame offset`, `Game TPS`, `Click volume`.

**Click Pack**: elige una carpeta con sonidos (wav/mp3/ogg/flac); se usa la misma para todas las macros y se recuerda. Los sonidos de release son archivos o carpetas con `release` en el nombre. `Default` vuelve al sonido incluido. Solo suenan los clicks de la macro (Watch/Trace); tus clicks no hacen sonido. `Click volume` en 0 = sin sonido.

**Indicators** (botón en el panel): carril y puntos on/off, opacidad, color de press y de release (ambos son círculos), y **hold line**: una línea del indicador de press al de release para ver cuánto hay que mantener (color y opacidad configurables). En el carril se dibuja entre los círculos; en el nivel entre las posiciones grabadas.

**Stats**: con cualquier modo activo no se guarda nada (progreso, attempts, stats, completions). Al llegar al final del nivel sale el reporte y el juego se pausa.

## Si algo no cuadra

- **En Watch la macro muere o se desfasa**: prueba `Macro frame offset` en `-1` o `+1`. Cada bot cuenta el frame con una convención algo distinta; el mod usa la de zBot (`m_currentProgress - 1`).
- **Macro grabada con otro TPS** (por ejemplo 360 con physics bypass): pon `Game TPS` igual al TPS al que corre tu juego. En `Auto` se asume que son iguales.
- Si el archivo no se reconoce, el mod lo dice. En zBot y xdBot el significado del flag de player 1/2 se tomó de las fuentes de nat-converter; si P1 y P2 salen invertidos con tu macro, avísame.

## Estructura

```
src/core/Macro.*      lectores de macros (GDR2 + otros formatos, C++ puro)
src/core/Engine.hpp   corrección de early inputs, emparejado y puntuación (C++ puro, probado)
src/Session.*         estado global, ajustes, sonidos, bloqueo de stats
src/hooks.cpp         hooks de Geode (inputs, nivel, stats, menú de pausa)
src/ui/               HUD, panel, popup de indicadores, popup de resultados
tests/test_core.cpp   tests de la lógica (se compilan aparte, sin Geode)
```

Tests de la lógica (necesitan g++ 13+ y los headers de GDReplayFormat):

```
g++ -std=c++23 -Isrc/core -I<ruta a GDReplayFormat>/include tests/test_core.cpp src/core/Macro.cpp -o test_core && ./test_core
```
