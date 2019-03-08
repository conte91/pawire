"""
PaWire: Simple mic-to-speaker link using PortAudio.
Copyright (c) 2018 Simone Baratta.

This software is beerware: if we meet, you owe me a beer.

setup.py script "freely adapted" (i.e. mostly copy-pasted)
from portaudio's.
"""

import os
import platform
import sys
from setuptools import setup, Extension

__version__ = "0.1"

# distutils will try to locate and link dynamically against portaudio.
#
# If you would rather statically link in the portaudio library (e.g.,
# typically on Microsoft Windows), run:
#
# % python setup.py build --static-link
#
# Specify the environment variable PORTAUDIO_PATH with the build tree
# of PortAudio.

PORTAUDIO_BUILTIN_PATH=os.path.join('.', 'portaudio-bin')

# Use the provided/system-wide portaudio distribution.
use_binary_portaudio = True
include_dirs = []
if "PORTAUDIO_PATH" in os.environ:
    use_binary_portaudio = False;
    portaudio_path = os.environ.get("PORTAUDIO_PATH")
    include_dirs = [os.path.join(portaudio_path, 'include')]

pyaudio_module_sources = ['_pawire.c']
external_libraries = []
extra_compile_args = []
extra_link_args = []
scripts = []
defines = []

# Provide a binary distribution for portaudio for windows,
# as it's a mess to compile it.
portaudio_dll = None
portaudio_lib_path = None
if sys.platform == 'win32':
    if use_binary_portaudio:
        # TODO provide a win64 library.
        portaudio_dll = os.path.abspath(os.path.join(PORTAUDIO_BUILTIN_PATH, 'portaudio_x86.dll'))
        include_dirs = os.path.join(PORTAUDIO_BUILTIN_PATH, 'include')
        print("Will use the provided binary distribution of portaudio for Windows.")
        bits = platform.architecture()[0]
        portaudio_lib_path = os.path.join(portaudio_path, "build", "msvc", "Win32", "Release")
        if '64' in bits:
            portaudio_lib_name = 'portaudio_x64'
        else:
            portaudio_lib_name = 'portaudio_x86'
        portaudio_dll = os.path.abspath(os.path.join(portaudio_lib_path, portaudio_lib_name + '.dll'))
        include_dirs = os.path.join(portaudio_path, "include")
    print("Portaudio include path: {}".format(include_dirs))
    print("Portaudio DLL: {}".format(portaudio_dll))
else:
    if use_binary_portaudio:
        print("Will use the system-wide distribution of portaudio.")

if sys.platform == 'win32':
    print("Using portaudio for windows: {}".format(portaudio_dll))
    external_libraries = [portaudio_dll]
else:
    external_libraries = ['portaudio']

# Platform-specifid defines
if sys.platform == 'win32':
    defines.append('__PAWIRE_WIN32')

setup(name='pawire',
      version=__version__,
      author="Simone Baratta",
      url="https://github.com/Conte91/pawire",
      description='Simple mic-to-speaker playback using PortAudio.',
      long_description=__doc__.lstrip(),
      scripts=scripts,
      py_modules=['pawire'],
      package_dir={'': '.'},
      package_data={'': portaudio_dll} if portaudio_dll is not None else {},
      ext_modules=[
    Extension('_pawire',
              sources=pyaudio_module_sources,
              include_dirs=include_dirs,
              define_macros=defines,
              libraries=external_libraries,
              library_dirs= [portaudio_lib_path] if portaudio_lib_path else [],
              extra_compile_args=extra_compile_args,
              extra_link_args=extra_link_args)
    ])
