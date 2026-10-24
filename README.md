# ytmusic-fltk

Cliente ligero de YouTube Music escrito en C++/FLTK + mpv + yt-dlp by IamJony.

![Captura de ytmusic-fltk](https://raw.githubusercontent.com/IamJony/ytmusic-fltk/refs/heads/main/capturas/ui.png)

## Dependencias

- `g++`, `make`, `fltk1.3-dev`, `libcurl4-openssl-dev`
- `mplayer` (reproducción de audio)
- `yt-dlp` (búsqueda y obtención de streams)

## Compilar

```bash
make

```
## Instalar en FLinux (paquete TCZ)
FLinux usa el mismo sistema de extensiones que Tiny Core Linux. Primero instala
las dependencias desde la tienda o por terminal:
```bash
tce-load -wi curl.tcz mplayer-cli.tcz ffmpeg7.tcz libwebp1.tcz gcc_libs.tcz &
tce-load -i ./ytmusic-fltk.tcz
```
