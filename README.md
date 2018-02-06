## Build LLVM

Get our version of LLVM and Clang (closely tied to upstream, with just a couple
of changes) from GitHub:

```sh
$ git clone https://github.com/cadets/llvm
$ cd llvm/tools
$ git clone https://github.com/cadets/clang
$ cd ../..
$ mkdir -p build/Release    # or build/Debug, etc.
$ cd build/Release
$ cmake -G Ninja -D CMAKE_BUILD_TYPE=Release ../..    # or Debug
$ ninja
```

Then put the just-build LLVM at the front of your `PATH`:

```sh
$ export PATH=/path/to/your/LLVM/build/bin:$PATH
```


## Build Loom

Build the Loom instrumentation framework:

```sh
$ git clone https://github.com/cadets/loom
$ cd loom
$ mkdir -p build/Release    # or build/Debug, etc.
$ cd build/Release
$ cmake -G Ninja -D CMAKE_BUILD_TYPE=Release ../..    # or Debug
```


## Build llvm-prov

Assuming that you have set all of the environment variables above,
you should now be able to build the `llvm-prov` library:

```sh
$ git clone https://github.com/cadets/llvm-prov
$ cd llvm-prov
$ mkdir -p build/Release     # or Debug
$ cd build/Release
$ cmake -G Ninja -D CMAKE_BUILD_TYPE=Release \     # or Debug
    -D LOOM_PREFIX=/path/to/loom/build/Release \
    ../..
# or Debug
$ ninja
```

Then set an environment variable pointing at the `llvm-prov` library and extend your `PATH` to include the llvm-prov `scripts` directory:

```sh
$ export LLVM_PROV_PREFIX=/path/to/llvm-prov/build
$ export PATH=/path/to/llvm-prov/source/scripts:$PATH
```


## Build FreeBSD tools

Check out our version of FreeBSD with I/O metadata support and build `world`:

```sh
$ git clone https://github.com/cadets/freebsd -b llprov freebsd-llprov
$ cd freebsd-llprov
$ llvm-prov-make buildworld     # I often like to append: -j8 2>&1 | tee build.log
```

Then, to play with instrumentation of a specific tool, use the `llvm-prov-make`
script to enter a build environment within the FreeBSD source tree:

```sh
$ llvm-prov-make buildenv
```

After that, you should be able to enter any of the `bin` or `usr.bin`
tool directories and run `make foo.full.instrumented` to get an instrumented
binary, or `make foo.full.instrumented.ll` to build an instrumented textual IR
file (more readable than binaries or `.bc` files).
