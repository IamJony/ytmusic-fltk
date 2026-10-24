# ytmusic-fltk

Cliente ligero de YouTube Music escrito en C++/FLTK + mpv + yt-dlp by IamJony.

![Captura de ytmusic-fltk](https://raw.githubusercontent.com/IamJony/ytmusic-fltk/5d9af15ac2d7e5a9d768deaf1af2685be42d540a/capturas/ui.png)

## Dependencias

- `g++`, `make`, `fltk1.3-dev`, `libcurl4-openssl-dev`
- `mplayer-cli` (reproducción de audio)
- `yt-dlp` (búsqueda y obtención de streams)

## Compilar

```bash
make

```
## Instalar en FLinux (paquete TCZ)
FLinux usa el mismo sistema de extensiones que Tiny Core Linux. Primero instala
las dependencias desde la tienda o por terminal:
```bash
tce-load -wi fltk-1.3.tcz curl.tcz mplayer-cli.tcz ffmpeg.tcz libwebp1.tcz gcc_libs.tcz &
tce-load -i ./ytmusic-fltk.tcz
```
