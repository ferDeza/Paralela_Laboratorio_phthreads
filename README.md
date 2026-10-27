# Laboratorio Pthreads

## Código fuente

- `actividad_1.c`: cálculo secuencial y paralelo de π.
- `actividad_2.c`: multiplicación secuencial y paralela matriz-vector.
- `actividad_3.c`: lista enlazada sincronizada y demostración sin sincronización.
- `actividad_4.c`: tres implementaciones de barreras.
- `lab_common.h`: utilidades compartidas.
- `ejecutar.ps1`: compila y ejecuta las cuatro actividades desde PowerShell.

## Requisitos

En Windows se requiere [MSYS2 UCRT64](https://www.msys2.org/) con GCC y Pthreads.
Si GCC no está instalado, abre la terminal **MSYS2 UCRT64** y ejecuta:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc
```

En Linux se necesita GCC y la biblioteca de threads POSIX. En Debian o Ubuntu,
instálalos con:

```sh
sudo apt update
sudo apt install build-essential
```

En Fedora, instala el grupo de herramientas de desarrollo:

```sh
sudo dnf group install "Development Tools"
```

## Compilar y ejecutar todas las actividades

En la terminal PowerShell de VS Code, desde la carpeta del proyecto:

```powershell
.\ejecutar.ps1
```

El script compila los cuatro programas y muestra sus salidas en la terminal.
Deja `actividad_1.exe` a `actividad_4.exe` en esta carpeta.

Para ajustar las pruebas:

```powershell
.\ejecutar.ps1 -Threads 8 -PiSteps 30000000 -MatrixSize 3000 -ListOperations 1000 -BarrierRounds 3000
```

En Linux, abre una terminal en la carpeta del proyecto y compila los cuatro
programas:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic actividad_1.c -o actividad_1 -pthread
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic actividad_2.c -o actividad_2 -pthread
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic actividad_3.c -o actividad_3 -pthread
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic actividad_4.c -o actividad_4 -pthread
```

## Compilación manual

Desde una terminal **MSYS2 UCRT64** en la carpeta del proyecto:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic actividad_1.c -o actividad_1.exe -pthread
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic actividad_2.c -o actividad_2.exe -pthread
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic actividad_3.c -o actividad_3.exe -pthread
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic actividad_4.c -o actividad_4.exe -pthread
```

## Ejecutar cada programa manualmente

En PowerShell:

```powershell
.\actividad_1.exe 4 30000000
.\actividad_2.exe 4 1500
.\actividad_3.exe 4 1000
.\actividad_4.exe 4 1000
```

En Linux:

```sh
./actividad_1 4 30000000
./actividad_2 4 1500
./actividad_3 4 1000
./actividad_4 4 1000
```

Los argumentos son:

- `actividad_1`: número de threads y cantidad de iteraciones.
- `actividad_2`: número de threads y dimensión de la matriz cuadrada.
- `actividad_3`: número de threads y operaciones por thread.
- `actividad_4`: número de threads y rondas de barrera.

La demostración intencional de la lista sin sincronización se ejecuta aparte:

```powershell
.\actividad_3.exe --sin-sync 4 1000
```

En Linux, ejecuta la misma demostración con:

```sh
./actividad_3 --sin-sync 4 1000
```

El resultado de esa demostración puede variar porque contiene una condición de
carrera; para la ejecución correcta usa el comando normal de `actividad_3`.
