mkdir -p build
BUILD_DIR=build bear --append --output build/compile_commands.json -- mingw32-make $@
