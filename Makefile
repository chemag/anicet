

all: none
	echo "use cmake to build"


format:
	clang-format -i -style=google ./include/*h
	clang-format -i -style=google ./src/*cc
	black ./tools/*py


.PHONY: build

build:
	\rm -rf build
	mkdir build
	cd build && cmake -DCMAKE_BUILD_TYPE=Release -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 -DCMAKE_TOOLCHAIN_FILE=$$ANDROID_NDK/build/cmake/android.toolchain.cmake -DCMAKE_INSTALL_PREFIX=../install ..
	cd build && make -j 8 && make install
