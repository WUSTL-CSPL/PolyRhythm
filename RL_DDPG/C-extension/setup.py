#!/usr/bin/env python3

from distutils.core import setup, Extension

setup(
	name = "shmextension",
	version = "1.0",
	ext_modules = [Extension("shmextension", ["bind.c", "libshm.c"])]
	);
