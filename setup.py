#!/usr/bin/env python

from setuptools import setup, Extension

ext = Extension(
    "poqaio._poqaio",
    sources=[
        "extension/module.c",
        "extension/protocol.c",
        "extension/types.c",
    ],
    depends=["protocol.h", "poqaio.h", "types.h"],
)

setup(ext_modules=[ext])
