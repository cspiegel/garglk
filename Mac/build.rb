#!/usr/bin/ruby -w
# frozen_string_literal: true

require "tmpdir"

NPROC = `sysctl -n hw.ncpu`.chomp

AUTOCONF_VERSION = "2.69"
AUTOMAKE_VERSION = "1.16.5"
CMAKE_VERSION = "3.25.2"
FREETYPE_VERSION = "2.13.0"
LIBJPEG_TURBO_VERSION = "2.1.5.1"
LIBOPENMPT_MODPLUG_VERSION = "0.8.9.0-openmpt1"
LIBOPENMPT_VERSION = "0.6.9+release.autotools"
LIBPNG_VERSION = "1.6.39"
LIBTOOL_VERSION = "2.4.7"
LLVM_VERSION = "15.0.7"
NASM_VERSION = "2.16.01"
PKGCONF_VERSION = "1.8.0"
SDL2_MIXER_VERSION = "2.6.3"
SDL2_VERSION = "2.26.4"
ZLIB_VERSION = "1.2.13"

ENV["PATH"] = "/Users/Shared/Gargoyle/bin:/usr/bin:/bin:/usr/sbin:/sbin"
ENV["PKG_CONFIG_PATH"] = "/Users/Shared/Gargoyle/lib/pkgconfig"
ENV["CPPFLAGS"] = "-I/Users/Shared/Gargoyle/include"
ENV["LDFLAGS"] = "-L/Users/Shared/Gargoyle/lib"
ENV["LIBRARY_PATH"] = "/Users/Shared/Gargoyle/lib"

def run(*args)
  puts args.join(" ")
  system(*args) || abort
end

class Builder
  def initialize(name, version)
    @archive = Dir["#{name}-#{version}*.tar.*"].first
    @name = name
    @version = version
  end

  def build
    Dir.mktmpdir do |dir|
      run "tar", "xvf", @archive, "-C", dir
      Dir.chdir(Dir["#{dir}/*"].first) do
        build_command
      end
    end
  end

  def make
    run "make", "-j#{NPROC}"
  end
end

class Autoconf < Builder
  def initialize(name, version, options: [])
    super(name, version)
    @options = options
  end

  def build_command
    run "./configure", "--prefix=/Users/Shared/Gargoyle", *@options
    make
    run "make", "install"
  end
end

class CMake < Builder
  def initialize(name, version, options: [])
    super(name, version)
    @options = options
  end

  def build_command
    Dir.mkdir("build")
    Dir.chdir("build") do
      run "cmake", "..", "-DCMAKE_INSTALL_PREFIX=/Users/Shared/Gargoyle", "-DCMAKE_BUILD_TYPE=Release", *@options
      make
      run "make", "install"
    end
  end
end

class LLVM < CMake
  def build_command
    super
    links = {
      "cc" => "clang",
      "gcc" => "clang",
      "c++" => "clang++",
      "g++" => "clang++",
    }

    links.each do |target, source|
      begin
        File.symlink source, "/Users/Shared/Gargoyle/bin/#{target}"
      rescue Errno::EEXIST
      end
    end
  end
end

class PkgConf < Autoconf
  def build_command
    run "./autogen.sh"
    super
    begin
      File.symlink "pkgconf", "/Users/Shared/Gargoyle/bin/pkg-config"
    rescue Errno::EEXIST
    end
  end
end

Autoconf.new("cmake-#{CMAKE_VERSION}.tar.gz", "cmake-#{CMAKE_VERSION}", options: %W(--no-system-libs --parallel=#{NPROC})).build
LLVM.new("llvm-project", LLVM_VERSION, options: %w(-DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON -DLLVM_INSTALL_UTILS=ON -DLLVM_ENABLE_BINDINGS=OFF -DLLVM_ENABLE_RTTI=ON -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_ENABLE_PROJECTS=clang -DFFI_INCLUDE_DIR=/System/Volumes/Data/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/ffi -DFFI_LIBRARY_DIR=/System/Volumes/Data/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib -DLLVM_ENABLE_LIBCXX=ON -DDEFAULT_SYSROOT=/System/Volumes/Data/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi;compiler-rt -DBUILTINS_CMAKE_ARGS=-DCOMPILER_RT_ENABLE_IOS=OFF)).build
Autoconf.new("autoconf", AUTOCONF_VERSION).build
Autoconf.new("automake", AUTOMAKE_VERSION).build
Autoconf.new("libtool", LIBTOOL_VERSION).build
PkgConf.new("pkgconf-pkgconf", PKGCONF_VERSION).build
Autoconf.new("zlib", ZLIB_VERSION).build
CMake.new("libpng", LIBPNG_VERSION).build
Autoconf.new("nasm", NASM_VERSION).build
CMake.new("libjpeg-turbo", LIBJPEG_TURBO_VERSION).build
Autoconf.new("libopenmpt", LIBOPENMPT_VERSION, options: %w(--without-mpg123 --without-ogg --without-vorbis --without-vorbisfile --without-portaudio --without-portaudiocpp --without-sndfile --without-flac --disable-examples --disable-tests)).build
Autoconf.new("libopenmpt-modplug", LIBOPENMPT_MODPLUG_VERSION, options: %w(--enable-libmodplug)).build
CMake.new("SDL2", SDL2_VERSION).build
CMake.new("SDL2_mixer", SDL2_MIXER_VERSION, options: %w(-DCMAKE_BUILD_TYPE=Release -DSDL2MIXER_OPUS=OFF -DSDL2MIXER_FLAC=OFF -DSDL2MIXER_MIDI_FLUIDSYNTH=OFF -DSDL2MIXER_MOD_MODPLUG_SHARED=OFF)).build
Autoconf.new("freetype", FREETYPE_VERSION, options: %w(--enable-freetype-config)).build
