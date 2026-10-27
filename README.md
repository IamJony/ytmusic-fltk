# ytmusic-fltk

Cliente ligero "YouTube Music" escrito en C++/FLTK + mplayer + yt-dlp by IamJony.

![Captura de ytmusic-fltk](https://raw.githubusercontent.com/IamJony/ytmusic-fltk/refs/heads/main/capturas/ui2.png)

## Dependencias

- `g++`, `make`, `fltk1.3-dev`, `libcurl4-openssl-dev`, `compiletc`, `curl-dev`
- `mplayer` (reproducción de audio)
- `yt-dlp` (búsqueda y obtención de streams)

## Compilar

```bash
make

```
## Instalar en FLinux (paquete TCZ)
FLinux usa el mismo sistema de extensiones que Tiny Core Linux. Primero instala
las dependencias desde la tienda o por terminal y luego instala el paquete que se encuentra en la carpeta tcz:

```bash
tce-load -wi curl.tcz mplayer-cli.tcz ffmpeg4.tcz libwebp1.tcz gcc_libs.tcz yt-dlp &
tce-load -wi ./tcz/ytmusic-fltk.tcz
```
## Playlist y musica descargada
Las playlist se guardan en $HOME/.config/ytmusic-fltk y la musica descargada se guarda en formato mp3 en tu carpeta de descarga

