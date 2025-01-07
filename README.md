# git-websocket-notifier

Notifies a WebSocket connection about git changes.

Relies on [libgit2](https://libgit2.org/), [efsw](https://github.com/SpartanJ/efsw) for detecting filesystem changes in a cross-platform way, and [Simple-WebSocket-Server](https://gitlab.com/eidheim/Simple-WebSocket-Server) for a WebSocket client.

## Usage

```
Usage: <watch_dir> <websocket_host_port_path>
    repo_dir - git repository directory to recursively watch for changes
    websocket_host_port_path - host, port and path of WebSocket server to write changes to.
    Example: ./src localhost:8080
```

## Limitations

Currently doesn't write to a secure WebSocket connection (i.e. `wss://`).

## Prerequisites

1. Install [Conan](https://conan.io/downloads) - a free and open-source C/C++ package manager.
2. Create a default Conan profile:

       conan profile detect --force

3. `conan install . --output-folder=build --build=missing`

## How to Build

1. `cd build`

Linux, macOS:

2. `cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release`
3. `cmake --build .`

Windows (assuming Visual Studio 15 2017 is your VS version and that it matches your default profile):

2. `cmake .. -G "Visual Studio 15 2017" -DCMAKE_TOOLCHAIN_FILE="conan_toolchain.cmake"`
3. `cmake --build . --config Release`

This should create a `git-websocket-notifier` executable in `build/`.

See Also:
https://docs.conan.io/2/tutorial/consuming_packages/build_simple_cmake_project.html

## Built With

This project was successfully built with the following Conan profile:

``` 
$ conan profile detect --force ↵
detect_api: Found cc=gcc- 11.4.0
detect_api: gcc>=5, using the major as version
detect_api: gcc C++ standard library: libstdc++11

Detected profile:
[settings]
arch=x86_64
build_type=Release
compiler=gcc
compiler.cppstd=gnu17
compiler.libcxx=libstdc++11
compiler.version=11
os=Linux
```

## WebSocket Server

For testing purposes, a basic WebSocket server written in Node.js has been included.

1. Install [Node.js](https://nodejs.org/en).
2. Install dependencies:

       npm install

3. Start WebSocket server on port 8080:

       npm start
