# macOS Specific Issues
## Attempting to build the Cocoa frontend fails with NSInternalInconsistencyException

When building on macOS, the build system will make a native Cocoa app by default. In this case, the build system uses the Xcode `ibtool` command to build user interface files. If this command fails, you can fix this issue by starting Xcode and letting it install components. After this is done, you should be able to close Xcode and build successfully.

## Attempting to build the SDL frontend on macOS fails on linking

SameBoy on macOS expects you to have SDL2 installed via Brew, and not as a framework. Older versions expected it to be installed as a framework, but this is no longer the case.

# Windows Build Process

## Tools and Libraries Installation

For the various tools and libraries, follow the below guide to ensure easy, proper configuration for the build environment:

### SDL2

For [libSDL2](https://libsdl.org/download-2.0.php), download the Visual C++ Development Library pack. Place the extracted files within a known folder for later. Both the `\x64\` and `\include\` paths will be needed.  

The following examples will be referenced later: 

- `C:\SDL2\lib\x64\*`
- `C:\SDL2\include\*`

### rgbds

After downloading [rgbds](https://github.com/gbdev/rgbds/releases/), ensure that it is added to the `%PATH%`. This may be done by adding it to the user's or SYSTEM's Environment Variables, or may be added to the command line at compilation time via `set path=%path%;C:\path\to\rgbds`.  

### Git Bash & Make

Ensure that the `Git\usr\bin` directory is included in `%PATH%`. Like rgbds above, this may instead be manually included on the command line before installation: `set path=%path%;C:\path\to\Git\usr\bin`. Similarly, make sure that the directory containing `make.exe` is also included.

## Building

Within a command prompt in the project directory:

```
vcvars64
set lib=%lib%;C:\SDL2\lib\x64
set include=%include%;C:\SDL2\include
make
```
On some versions of Visual Studio, you might need to use `vcvarsx86_amd64` instead of `vcvars64`. Please note that these directories (`C:\SDL2\*`) are the examples given within the "SDL Port" section above. Ensure that your `%PATH%` properly includes `rgbds` and `Git\usr\bin`, and that the `lib` and `include` paths include the appropriate SDL2 directories.

