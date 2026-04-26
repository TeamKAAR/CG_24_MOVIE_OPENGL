# 24: The OpenGL Cut

Fan-film style OpenGL animation project written in C++ with GLUT/freeglut.

The project renders multiple cinematic scenes (title card, rainy discovery, watch macro, stadium sequence, closed loop, and post-credit scene) and supports manual scene controls from keyboard input.

## Project structure

```
OPENGL_24_Movie/
|- 24_film.cpp
|- assets/
|  |- RCB_CSK.jpg
|  |- Hitler_flag.png
|- freeglut.dll   (Windows runtime DLL)
```

## Requirements

1. C++ compiler with C++17 support
2. OpenGL + GLU + GLUT/freeglut development libraries
3. On Windows: GDI+ (already available as part of Windows SDK/runtime)

## Dependencies by platform

### Windows (MinGW)

- `opengl32`
- `glu32`
- `freeglut`
- `gdiplus`

### Linux

- `GL`
- `GLU`
- `glut` (usually freeglut)

### macOS

- OpenGL framework
- GLUT framework

## Build and run

Important: some comments in source mention `24_the_opengl_cut.cpp`, but this repo file is named `24_film.cpp`. Use the commands below with the actual filename.

### Windows (MinGW)

```bash
g++ -std=c++17 -O2 24_film.cpp -lopengl32 -lglu32 -lfreeglut -lgdiplus -o 24_film.exe
./24_film.exe
```

### Linux

```bash
g++ -std=c++17 -O2 24_film.cpp -lGL -lGLU -lglut -o 24_film
./24_film
```

Install deps (Ubuntu/Debian):

```bash
sudo apt-get update
sudo apt-get install -y build-essential freeglut3-dev
```

### macOS

```bash
g++ -std=c++17 -O2 24_film.cpp -framework OpenGL -framework GLUT -o 24_film
./24_film
```

Optional package helper:

```bash
brew install freeglut
```

## Controls

- `Space`: skip to next scene
- `P`: pause/unpause
- `Esc`: quit

## Runtime assets

The app loads texture files at runtime:

- `assets/RCB_CSK.jpg`
- `assets/Hitler_flag.png`

Run the executable from the project root so relative paths resolve correctly.

## Do I need to push `freeglut.dll`?

Short answer: for a class/demo repo where others run your prebuilt `24_film.exe` directly on Windows, **yes, usually include it**.

More specifically:

1. If you only share source and everyone builds locally with their own freeglut install, you do not need to commit `freeglut.dll`.
2. If you share a prebuilt `.exe`, include `freeglut.dll` next to the `.exe` (or provide a clear install step), otherwise the program may fail to start with a missing DLL error.
3. If your instructor/teammates are non-technical, committing `freeglut.dll` is the most reliable option.

## Notes

- The source currently uses immediate-mode OpenGL for educational/demo rendering.
- On non-Windows platforms, texture loading helper in this file is Windows-specific (GDI+), so behavior can differ unless replaced with a cross-platform image loader.
